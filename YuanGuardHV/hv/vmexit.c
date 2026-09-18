#include <ntddk.h>
#include "svm_defs.h"
#include "svm_vcpu.h"
#include "vmcb.h"
#include "protect.h"
#include "debug.h"

void yghv_trace(const char *msg);
void yghv_trace_u64(const char *label, uint64_t value);

#define SVM_EVENTINJ_VALID        (1ULL << 31)
#define SVM_EVENTINJ_TYPE_EXC     (3ULL << 8)
#define SVM_EVENTINJ_ERROR_VALID  (1ULL << 11)

static void svm_emulate_cpuid(svm_vcpu_t *vcpu);
static int  svm_handle_msr(svm_vcpu_t *vcpu);
static int  svm_handle_cr(svm_vcpu_t *vcpu);

extern int vmmcall_dispatch(svm_vcpu_t *vcpu);
extern npt_mgr_t g_npt;
extern uint64_t g_npt_test_pa;
extern volatile int g_npt_test_active;
extern volatile LONG g_os_guest_test_active;
extern volatile BOOLEAN g_os_resident_mode;
extern volatile ULONG64 g_os_resident_exits;
extern volatile BOOLEAN g_os_guest_avic_timer_emu;
extern volatile BOOLEAN g_v98_apic_shadow;
extern volatile BOOLEAN g_os_guest_stop_requested;
/* 9.229 self-diagnosis flags for step203-lean (defined in main.c). */
extern volatile BOOLEAN g_s203_lean;
extern volatile LONG g_s203_exit_log;
extern volatile uint64_t g_s203_unexpected_exit;
/* 9.180 MSV: minimal shadow validation flags (defined in main.c). */
extern volatile BOOLEAN g_msv_test;
extern volatile BOOLEAN g_msv_cr3_seen;
extern volatile ULONG64 g_msv_cr3_writes;
extern volatile BOOLEAN g_v100_monitor_active;
extern volatile BOOLEAN g_v101_gp_seen;
extern volatile uint64_t g_v101_gp_exitcode;
extern volatile uint64_t g_v101_gp_err;
extern volatile uint64_t g_v101_gp_rip;
extern volatile uint64_t g_v101_gp_rsp;
extern volatile uint64_t g_v101_gp_cr3;
extern volatile uint64_t g_v101_gp_gs_base;
extern volatile BOOLEAN g_v102_catchall;
extern volatile BOOLEAN g_r1_diag;
extern volatile ULONG g_r1_diag_count;
extern volatile ULONG g_r1_entry_seq;
extern void *g_v98_apic_shadow_va;
extern void *g_v98_real_apic_va;
extern volatile ULONG g_v98_last_tpr;
extern volatile ULONG g_v98_last_icrl;
extern volatile ULONG g_v98_last_icrh;
extern volatile ULONG g_v98_last_lvtt;
extern volatile ULONG g_v98_last_tmict;
extern volatile ULONG g_v98_last_tdcr;
extern volatile ULONG64 g_v98_apic_mmio_count;
extern volatile ULONG64 g_npf_count;
extern volatile uint64_t g_last_npf_gpa;
extern volatile uint64_t g_last_npf_err;
extern volatile uint64_t g_last_npf_rip;
extern volatile ULONG64 g_intr_exit_count;

#define YGHV_OS_GUEST_EXIT_LIMIT 10000ULL

static void svm_advance_rip(svm_vcpu_t *vcpu) {
    vcpu->vmcb->state.rip = vcpu->vmcb->control.next_rip;
}

static void svm_writeback_gprs(svm_vcpu_t *vcpu) {
    /* RAX is loaded from VMCB state.rax by VMRUN; other regs from vcpu->regs */
    vcpu->vmcb->state.rax = vcpu->regs.rax;
}

static void svm_finish_exit(svm_vcpu_t *vcpu) {
    svm_advance_rip(vcpu);
    svm_writeback_gprs(vcpu);
}

static void yghv_v100_record(svm_vcpu_t *vcpu) {
    uint64_t seq;
    svm_v100_ring_entry_t *e;

    if (!g_v100_monitor_active)
        return;
    seq = vcpu->v100_seq + 1;
    vcpu->v100_seq = seq;
    e = &vcpu->v100_ring[seq % YGHV_V100_RING_ENTRIES];
    e->seq = seq;
    e->exitcode = vcpu->vmcb->control.exitcode;
    e->exitinfo1 = vcpu->vmcb->control.exitinfo1;
    e->exitinfo2 = vcpu->vmcb->control.exitinfo2;
    e->rip = vcpu->vmcb->state.rip;
    e->cr3 = vcpu->vmcb->state.cr3;
    e->rsp = vcpu->vmcb->state.rsp;
    e->rflags = vcpu->vmcb->state.rflags;
    e->cpl = vcpu->vmcb->state.cpl;
}

static BOOLEAN yghv_avic_trap_offset(uint32_t offset) {
    switch (offset) {
    case 0x20:   /* APIC ID */
    case APIC_OFFSET_EOI:
    case 0xC0:   /* remote read */
    case APIC_OFFSET_LDR:
    case APIC_OFFSET_DFR:
    case APIC_OFFSET_SPIV:
    case APIC_OFFSET_ESR:
    case APIC_OFFSET_ICRL:
    case APIC_OFFSET_LVTT:
    case 0x330:  /* thermal LVT */
    case 0x340:  /* performance counter LVT */
    case 0x350:  /* LINT0 */
    case 0x360:  /* LINT1 */
    case 0x370:  /* error LVT */
    case APIC_OFFSET_TMICT:
    case APIC_OFFSET_TDCR:
        return TRUE;
    default:
        return FALSE;
    }
}

static void yghv_avic_forward_trap_write(svm_vcpu_t *vcpu, uint32_t offset) {
    uint32_t value;

    if (!vcpu->avic_host_apic_va || offset >= HV_PAGE_SIZE)
        return;
    if (g_os_guest_avic_timer_emu &&
        (offset == APIC_OFFSET_LVTT || offset == APIC_OFFSET_TMICT ||
         offset == APIC_OFFSET_TDCR)) {
        return;
    }
    if (offset != APIC_OFFSET_LDR && offset != APIC_OFFSET_DFR &&
        offset != APIC_OFFSET_SPIV && offset != APIC_OFFSET_ESR &&
        offset != APIC_OFFSET_LVTT && offset != APIC_OFFSET_TMICT &&
        offset != APIC_OFFSET_TDCR &&
        offset != 0x330 && offset != 0x340 &&
        offset != 0x350 && offset != 0x360 && offset != 0x370) {
        return;
    }
    value = *(volatile uint32_t *)((ULONG_PTR)vcpu->avic_backing_page + offset);
    WRITE_REGISTER_ULONG((PULONG)((ULONG_PTR)vcpu->avic_host_apic_va + offset), value);
}

static int svm_handle_avic_unaccelerated(svm_vcpu_t *vcpu) {
    uint64_t info1 = vcpu->vmcb->control.exitinfo1;
    uint32_t offset = (uint32_t)((info1 >> 4) & 0xFF0);
    BOOLEAN write = (info1 & AVIC_UNACCEL_ACCESS_WRITE_MASK) != 0;

    vcpu->avic_noaccel_exits++;
    if (vcpu->avic_noaccel_exits <= 8) {
        LOG_ERROR("AVIC noaccel: exits=%llu offset=0x%x write=%u info1=0x%llx",
            vcpu->avic_noaccel_exits, offset, write ? 1 : 0, info1);
    }

    if (write && yghv_avic_trap_offset(offset)) {
        if (g_os_guest_avic_timer_emu &&
            (offset == APIC_OFFSET_LVTT || offset == APIC_OFFSET_TMICT ||
             offset == APIC_OFFSET_TDCR)) {
            svm_avic_update_timer(vcpu, offset);
            if (vcpu->avic_host_apic_va) {
                WRITE_REGISTER_ULONG(
                    (PULONG)((ULONG_PTR)vcpu->avic_host_apic_va + APIC_OFFSET_TMICT), 0);
                WRITE_REGISTER_ULONG(
                    (PULONG)((ULONG_PTR)vcpu->avic_host_apic_va + APIC_OFFSET_TDCR), 0);
            }
        } else {
            yghv_avic_forward_trap_write(vcpu, offset);
        }
        return 0;
    }

    /* Fault-like access: instruction did not complete; skip it to avoid a loop. */
    svm_finish_exit(vcpu);
    return 0;
}

static int svm_handle_avic_incomplete_ipi(svm_vcpu_t *vcpu) {
    uint64_t info1 = vcpu->vmcb->control.exitinfo1;

    vcpu->avic_incomplete_ipi_exits++;
    if (vcpu->avic_incomplete_ipi_exits <= 8) {
        LOG_ERROR("AVIC incomplete IPI: exits=%llu info1=0x%llx info2=0x%llx",
            vcpu->avic_incomplete_ipi_exits, info1,
            vcpu->vmcb->control.exitinfo2);
    }

    if (vcpu->avic_host_apic_va) {
        WRITE_REGISTER_ULONG(
            (PULONG)((ULONG_PTR)vcpu->avic_host_apic_va + APIC_OFFSET_ICRH),
            (uint32_t)(info1 >> 32));
        WRITE_REGISTER_ULONG(
            (PULONG)((ULONG_PTR)vcpu->avic_host_apic_va + APIC_OFFSET_ICRL),
            (uint32_t)info1);
    }
    return 0;
}

static uint32_t yghv_avic_scan_pending_vector(svm_vcpu_t *vcpu) {
    int group;

    for (group = 7; group >= 0; group--) {
        volatile uint32_t *irr =
            (volatile uint32_t *)((ULONG_PTR)vcpu->avic_host_apic_va +
                                  0x200 + 16 * group);
        uint32_t bits = READ_REGISTER_ULONG((PULONG)irr);
        int bit;

        if (!bits)
            continue;
        for (bit = 31; bit >= 0; bit--) {
            if (bits & (1U << bit))
                return (uint32_t)(group * 32 + bit);
        }
    }
    return 0;
}

static void yghv_avic_ring_doorbell(uint32_t apic_id) {
    uint32_t low = apic_id;
    uint32_t high = 0;
    __asm__ volatile("wrmsr" : :
        "c"(MSR_AMD64_SVM_AVIC_DOORBELL), "a"(low), "d"(high));
}

static VOID yghv_avic_timer_dpc(KDPC *dpc, PVOID context,
                                PVOID arg1, PVOID arg2) {
    svm_vcpu_t *vcpu = (svm_vcpu_t *)context;
    ULONG vector = vcpu->avic_timer_vector;
    volatile uint32_t *irr;

    (void)dpc;
    (void)arg1;
    (void)arg2;

    if (vector && vcpu->avic_backing_page) {
        irr = (volatile uint32_t *)((ULONG_PTR)vcpu->avic_backing_page +
                                    0x200 + 16 * (vector >> 5));
        *irr |= (1U << (vector & 31));
        yghv_avic_ring_doorbell(vcpu->avic_apic_id);
    }

    if (vcpu->avic_timer_armed) {
        LARGE_INTEGER due;
        due.QuadPart = -(LONGLONG)(vcpu->avic_timer_period_ms * 10000);
        KeSetTimer(&vcpu->avic_timer, due, &vcpu->avic_timer_dpc);
    }
}

int svm_avic_timer_init(svm_vcpu_t *vcpu) {
    if (!vcpu)
        return 0;
    if (!vcpu->avic_timer_initialized) {
        KeInitializeTimer(&vcpu->avic_timer);
        KeInitializeDpc(&vcpu->avic_timer_dpc, yghv_avic_timer_dpc, vcpu);
        vcpu->avic_timer_initialized = TRUE;
    }
    return 0;
}

int svm_avic_start_timer(svm_vcpu_t *vcpu, uint32_t vector, uint32_t period_ms) {
    LARGE_INTEGER due;

    if (!vcpu || !vcpu->avic_timer_initialized)
        return 0;
    vcpu->avic_timer_vector = vector & 0xFF;
    vcpu->avic_timer_period_ms = period_ms ? period_ms : 10;
    if (vcpu->avic_timer_armed)
        return 0;

    due.QuadPart = -(LONGLONG)(vcpu->avic_timer_period_ms * 10000);
    vcpu->avic_timer_armed = TRUE;
    KeSetTimer(&vcpu->avic_timer, due, &vcpu->avic_timer_dpc);
    return 0;
}

int svm_avic_update_timer(svm_vcpu_t *vcpu, uint32_t offset) {
    uint32_t tmict;
    uint32_t lvtt;

    if (!vcpu || !vcpu->avic_timer_initialized || !vcpu->avic_backing_page)
        return 0;

    tmict = *(volatile uint32_t *)((ULONG_PTR)vcpu->avic_backing_page +
                                   APIC_OFFSET_TMICT);
    lvtt = *(volatile uint32_t *)((ULONG_PTR)vcpu->avic_backing_page +
                                  APIC_OFFSET_LVTT);
    (void)offset;

    if (tmict == 0) {
        if (vcpu->avic_timer_armed) {
            KeCancelTimer(&vcpu->avic_timer);
            vcpu->avic_timer_armed = FALSE;
        }
        return 0;
    }

    return svm_avic_start_timer(vcpu, lvtt & 0xFF, 10);
}

int svm_avic_record_pending_intr(svm_vcpu_t *vcpu) {
    uint32_t vector;
    volatile uint32_t *irr;

    if (!vcpu->avic_backing_page || !vcpu->avic_host_apic_va)
        return 1;
    vector = yghv_avic_scan_pending_vector(vcpu);
    if (vector == 0)
        return 0;

    vcpu->avic_irr_injections++;
    irr = (volatile uint32_t *)((ULONG_PTR)vcpu->avic_backing_page +
                                0x200 + 16 * (vector >> 5));
    *irr |= (1U << (vector & 31));
    return 0;
}

static void yghv_v98_apic_scan_forward(void) {
    ULONG val;

    if (!g_v98_apic_shadow || !g_v98_apic_shadow_va || !g_v98_real_apic_va)
        return;

    val = *(volatile ULONG *)((ULONG_PTR)g_v98_apic_shadow_va + APIC_OFFSET_TPR);
    if (val != g_v98_last_tpr) {
        WRITE_REGISTER_ULONG((PULONG)((ULONG_PTR)g_v98_real_apic_va + APIC_OFFSET_TPR), val);
        g_v98_last_tpr = val;
    }

    val = *(volatile ULONG *)((ULONG_PTR)g_v98_apic_shadow_va + APIC_OFFSET_ICRL);
    if (val != g_v98_last_icrl) {
        WRITE_REGISTER_ULONG((PULONG)((ULONG_PTR)g_v98_real_apic_va + APIC_OFFSET_ICRH),
                             *(volatile ULONG *)((ULONG_PTR)g_v98_apic_shadow_va + APIC_OFFSET_ICRH));
        WRITE_REGISTER_ULONG((PULONG)((ULONG_PTR)g_v98_real_apic_va + APIC_OFFSET_ICRL), val);
        g_v98_last_icrl = val;
        g_v98_last_icrh = *(volatile ULONG *)((ULONG_PTR)g_v98_apic_shadow_va + APIC_OFFSET_ICRH);
    }

    val = *(volatile ULONG *)((ULONG_PTR)g_v98_apic_shadow_va + APIC_OFFSET_LVTT);
    if (val != g_v98_last_lvtt) {
        WRITE_REGISTER_ULONG((PULONG)((ULONG_PTR)g_v98_real_apic_va + APIC_OFFSET_LVTT), val);
        g_v98_last_lvtt = val;
    }
    val = *(volatile ULONG *)((ULONG_PTR)g_v98_apic_shadow_va + APIC_OFFSET_TMICT);
    if (val != g_v98_last_tmict) {
        WRITE_REGISTER_ULONG((PULONG)((ULONG_PTR)g_v98_real_apic_va + APIC_OFFSET_TMICT), val);
        g_v98_last_tmict = val;
    }
    val = *(volatile ULONG *)((ULONG_PTR)g_v98_apic_shadow_va + APIC_OFFSET_TDCR);
    if (val != g_v98_last_tdcr) {
        WRITE_REGISTER_ULONG((PULONG)((ULONG_PTR)g_v98_real_apic_va + APIC_OFFSET_TDCR), val);
        g_v98_last_tdcr = val;
    }
}

/* 9.190 B-1full: xAPIC MMIO write trap.  The APIC page is write-protected in NPT
   (present+readable, W cleared) so guest writes to 0xFEE00000+offset cause NPF.
   Decode the write from Decode-Assist, handle the register (EOI -> forward to the
   real APIC so the LAPIC timer keeps re-firing; TPR/ICR/timer/LDR/DFR/SPIV/ESR ->
   update the shadow and forward to the real APIC), then the caller advances RIP.
   Reads are served from the shadow page directly (no NPF).  Returns 0 on success
   (handled, caller must svm_finish_exit), 1 on decode failure (unhandled). */
static int yghv_apic_mmio_npf(svm_vcpu_t *vcpu) {
    const uint8_t *ib = vcpu->vmcb->control.instruction_bytes;
    uint8_t n = vcpu->vmcb->control.byte_fetched;
    int i = 0;
    int rex_b = 0;
    uint8_t opcode, modrm, rm, reg;
    uint32_t offset, value;
    uint64_t *slot;

    if (!g_v98_apic_shadow || !g_v98_apic_shadow_va || !g_v98_real_apic_va)
        return 0;
    if (!(vcpu->vmcb->control.exitinfo1 & NPF_INFO1_WRITE))
        return 0;   /* reads shouldn't NPF; fall through defensively */

    /* skip REX prefix (0x40-0x4F); REX.B extends reg to r8-r15 */
    while (i < n && (ib[i] & 0xF0) == 0x40) {
        if (ib[i] & 0x01)
            rex_b = 1;
        i++;
    }
    if (i + 1 >= n)
        return 1;
    opcode = ib[i];
    modrm = ib[i + 1];
    reg = (modrm >> 3) & 7;
    rm = modrm & 7;
    /* 9.207: relax addressing — the faulting GPA (exitinfo2) already identifies
       the xAPIC target, so we do NOT need to decode the effective address.
       Windows APIC writes are typically `mov [rax+disp32], r32` (mod=2, rm=0),
       which the old `rm!=5 || mod!=0` check rejected, causing decode failure and
       a VM stall.  Take the page offset from the GPA instead. */
    (void)rm;
    offset = (uint32_t)(vcpu->vmcb->control.exitinfo2 & 0xFFFULL);
    if (offset >= HV_PAGE_SIZE)
        return 1;

    if (opcode == 0xC7) {                 /* mov [m32], imm32: C7 /0 */
        uint32_t ilen;
        if (reg != 0)
            return 1;
        /* imm32 is the trailing 4 bytes of the instruction; next_rip-rip is the
           instruction length (Decode-Assist), valid on NPF. */
        if (vcpu->vmcb->control.next_rip <= vcpu->vmcb->state.rip)
            return 1;
        ilen = (uint32_t)(vcpu->vmcb->control.next_rip - vcpu->vmcb->state.rip);
        if (ilen < 4 || ilen > 15 || (int)ilen > n)
            return 1;
        value = (uint32_t)(ib[ilen - 4] | (ib[ilen - 3] << 8) |
                           (ib[ilen - 2] << 16) | (ib[ilen - 1] << 24));
    } else if (opcode == 0x89) {          /* mov [m32], r32: 89 /r */
        switch (reg | (rex_b << 3)) {
            case 0x00: slot = &vcpu->regs.rax; break;
            case 0x01: slot = &vcpu->regs.rcx; break;
            case 0x02: slot = &vcpu->regs.rdx; break;
            case 0x03: slot = &vcpu->regs.rbx; break;
            case 0x05: slot = &vcpu->regs.rbp; break;
            case 0x06: slot = &vcpu->regs.rsi; break;
            case 0x07: slot = &vcpu->regs.rdi; break;
            case 0x08: slot = &vcpu->regs.r8;  break;
            case 0x09: slot = &vcpu->regs.r9;  break;
            case 0x0A: slot = &vcpu->regs.r10; break;
            case 0x0B: slot = &vcpu->regs.r11; break;
            case 0x0C: slot = &vcpu->regs.r12; break;
            case 0x0D: slot = &vcpu->regs.r13; break;
            case 0x0E: slot = &vcpu->regs.r14; break;
            case 0x0F: slot = &vcpu->regs.r15; break;
            default: return 1;
        }
        value = (uint32_t)(*slot);
    } else {
        return 1;
    }

    g_v98_apic_mmio_count++;

    switch (offset) {
    case APIC_OFFSET_EOI:   /* write-only: forward EOI so the LAPIC timer keeps
                               re-firing; no shadow store (unreadable) */
        WRITE_REGISTER_ULONG((PULONG)((ULONG_PTR)g_v98_real_apic_va + APIC_OFFSET_EOI), value);
        return 0;
    case APIC_OFFSET_TPR:
        g_v98_last_tpr = value;
        WRITE_REGISTER_ULONG((PULONG)((ULONG_PTR)g_v98_real_apic_va + APIC_OFFSET_TPR), value);
        break;
    case APIC_OFFSET_ICRH:
        g_v98_last_icrh = value;
        WRITE_REGISTER_ULONG((PULONG)((ULONG_PTR)g_v98_real_apic_va + APIC_OFFSET_ICRH), value);
        break;
    case APIC_OFFSET_ICRL:  /* write ICRH (pending) then ICRL (initiate IPI) */
        g_v98_last_icrl = value;
        WRITE_REGISTER_ULONG((PULONG)((ULONG_PTR)g_v98_real_apic_va + APIC_OFFSET_ICRH),
                             g_v98_last_icrh);
        WRITE_REGISTER_ULONG((PULONG)((ULONG_PTR)g_v98_real_apic_va + APIC_OFFSET_ICRL), value);
        break;
    case APIC_OFFSET_LVTT:
        g_v98_last_lvtt = value;
        WRITE_REGISTER_ULONG((PULONG)((ULONG_PTR)g_v98_real_apic_va + APIC_OFFSET_LVTT), value);
        break;
    case APIC_OFFSET_TMICT:
        g_v98_last_tmict = value;
        WRITE_REGISTER_ULONG((PULONG)((ULONG_PTR)g_v98_real_apic_va + APIC_OFFSET_TMICT), value);
        break;
    case APIC_OFFSET_TDCR:
        g_v98_last_tdcr = value;
        WRITE_REGISTER_ULONG((PULONG)((ULONG_PTR)g_v98_real_apic_va + APIC_OFFSET_TDCR), value);
        break;
    case APIC_OFFSET_LDR:
    case APIC_OFFSET_DFR:
    case APIC_OFFSET_SPIV:
    case APIC_OFFSET_ESR:
        WRITE_REGISTER_ULONG((PULONG)((ULONG_PTR)g_v98_real_apic_va + offset), value);
        break;
    default:
        break;   /* unknown register: shadow store only, no forward */
    }
    /* store the written value into the shadow so guest reads see it */
    *(volatile uint32_t *)((ULONG_PTR)g_v98_apic_shadow_va + offset) = value;
    return 0;
}

int svm_dispatch_exit(svm_vcpu_t *vcpu) {
    uint64_t exitcode = vcpu->vmcb->control.exitcode;
    vcpu->last_exitcode = exitcode;   /* 9.152 diag: for freeze/stop localization */
    vcpu->last_rip = vcpu->vmcb->state.rip;   /* 9.153 diag */
    vcpu->last_rsp = vcpu->vmcb->state.rsp;   /* 9.153 diag */
    vcpu->last_cr3 = vcpu->vmcb->state.cr3;   /* 9.153 diag */
    /* 9.165 diag: GS base / KERNEL_GS_BASE at the last VMEXIT — Windows
       context-switch/exception-dispatch uses GS base (KPCR) to find the
       exception stack; the v100/v100b 0x139 dumps both fault in
       KiAbProcessContextSwitch -> KiAbEntryGetLockedHeadEntry with
       MISSING_GSFRAME_STACKPTR_ERROR.  Recording the guest GS state lets the
       watchdog capture what GS base was live right before a freeze. */
    vcpu->last_gs_base = vcpu->vmcb->state.gs_base;
    vcpu->last_kgs_base = vcpu->vmcb->state.kernel_gs_base;

    /* 9.162: clean-unload for OS-as-guest resident.  Once stop is requested
       (sc stop -> DriverUnload), every OS-as-guest VMEXIT returns 1 so the
       trampoline's jnz svm_os_guest_host_done terminates the guest thread on
       its next exit.  Spin guests VMEXIT on every CPUID, so they respond
       immediately; this also stops the resident exit counter so the watchdog
       does not keep it pinned. */
    if (g_os_guest_stop_requested)
        return 1;

    yghv_v100_record(vcpu);
    if (g_r1_diag && g_r1_diag_count++ < 200) {
        yghv_trace_u64("r1 entry", g_r1_entry_seq);
        yghv_trace_u64("r1 deny exit", exitcode);
        yghv_trace_u64("r1 deny info2", vcpu->vmcb->control.exitinfo2);
        yghv_trace_u64("r1 deny rip", vcpu->vmcb->state.rip);
    }
    if (g_v102_catchall &&
        (((exitcode >= SVM_EXIT_EXCEPTION_BASE) &&
          (exitcode < SVM_EXIT_EXCEPTION_BASE + 32)) ||
         (exitcode == SVM_EXIT_HLT))) {
        yghv_trace("r1 catchall fault");
        yghv_trace_u64("r1 catchall exit", exitcode);
        yghv_trace_u64("r1 catchall err", vcpu->vmcb->control.exitinfo1);
        yghv_trace_u64("r1 catchall info2", vcpu->vmcb->control.exitinfo2);
        yghv_trace_u64("r1 catchall rip", vcpu->vmcb->state.rip);
        yghv_trace_u64("r1 catchall cr3", vcpu->vmcb->state.cr3);
        yghv_trace_u64("r1 catchall rsp", vcpu->vmcb->state.rsp);
        yghv_trace_u64("r1 catchall gsbase", vcpu->vmcb->state.gs_base);
        g_v101_gp_exitcode = exitcode;
        g_v101_gp_err = vcpu->vmcb->control.exitinfo1;
        g_v101_gp_rip = vcpu->vmcb->state.rip;
        g_v101_gp_rsp = vcpu->vmcb->state.rsp;
        g_v101_gp_cr3 = vcpu->vmcb->state.cr3;
        g_v101_gp_gs_base = vcpu->vmcb->state.gs_base;
        g_v101_gp_seen = TRUE;
        return 1;
    }
    vcpu->resident_exits++;
    /* 9.229 self-diagnosis (203-lean). C1 (all-core) and C1s (single-core) both
     * hard-froze this machine within seconds..15s of entering guest, with zero
     * exit evidence on disk (the memory ring is lost on a power cycle). This
     * block turns a frozen boot into a diagnosable one:
     *   - any exit other than the expected three is a config deviation: record
     *     it write-through, then fail-close ONLY this guest core (the stop flag
     *     makes every core exit at its next poll) so the host survives and
     *     progress.log keeps the cause;
     *   - the first 40 exits (expected CPUID storm) are logged with guest RIP;
     *   - after that, one heartbeat every 1024 exits lets the log tail date the
     *     moment of death relative to guest polling. */
    if (g_s203_lean) {
        /* 9.229c2 lesson: the FIRST version of this block logged trace INSIDE
         * the VMEXIT window -> ZwWriteFile blocked on a file lock -> the switch
         * out happened on a half-guest context -> KiAbProcessContextSwitch #GP
         * -> 0x139 fastfail (the exact v100b landmine this file already
         * documents).  The window must stay pure memory: unexpected exits are
         * detected here but only set flags; the ring (yghv_v100_record above)
         * carries the evidence and the host-side observer thread flushes it to
         * the progress log from normal context. */
        if (exitcode != SVM_EXIT_CPUID && exitcode != SVM_EXIT_VMRUN &&
            exitcode != SVM_EXIT_VMMCALL) {
            g_s203_unexpected_exit = exitcode;
            g_os_resident_mode = FALSE;      /* watchdog must not kill a healthy host */
            g_os_guest_stop_requested = TRUE;
            return 1;
        }
        InterlockedDecrement(&g_s203_exit_log);   /* ring budget bookkeeping; observer
                                                   * reads last_exitcode/rip, no
                                                   * in-window logging (c2 lesson) */
    }
    if (g_os_guest_test_active &&
        vcpu->resident_exits >= YGHV_OS_GUEST_EXIT_LIMIT)
        return 1;
    if (g_os_resident_mode)
        g_os_resident_exits++;

    yghv_v98_apic_scan_forward();

    switch (exitcode) {

    case SVM_EXIT_VMMCALL: {
        int stop = vmmcall_dispatch(vcpu);
        if (stop) return 1;
        svm_finish_exit(vcpu);
        return 0;
    }

    case SVM_EXIT_CPUID:
        svm_emulate_cpuid(vcpu);
        svm_finish_exit(vcpu);
        return 0;

    case SVM_EXIT_MSR:
        svm_handle_msr(vcpu);
        svm_finish_exit(vcpu);
        return 0;

    case SVM_EXIT_CR0_READ:  case SVM_EXIT_CR0_WRITE:
    case SVM_EXIT_CR3_READ:  case SVM_EXIT_CR3_WRITE:
    case SVM_EXIT_CR4_READ:  case SVM_EXIT_CR4_WRITE:
    case SVM_EXIT_CR8_READ:  case SVM_EXIT_CR8_WRITE:
        /* svm_handle_cr is fail-closed (REV-009): non-emulated CR exits and the
           MSV CR3-write stop return 1, which must stop the resident loop. */
        if (svm_handle_cr(vcpu))
            return 1;
        svm_finish_exit(vcpu);
        return 0;

    case SVM_EXIT_EXCEPTION_DB:
        /* 9.152: consume the #DB whenever rearm_pending.  REV-047's TF check
           is unreliable: the CPU clears TF before the single-step #DB is
           delivered, so the check fails and the #DB is re-injected into the
           guest.  Under the shared kernel CR3 that ran the host IDT's handler
           in guest mode (a freeze/corruption vector); under the dedicated guest
           CR3 the re-injection triple-faults (IDT unmapped).  The synthetic
           guests have no hardware breakpoints, so unconditional consumption of
           the rearm #DB is correct. */
        if (vcpu->rearm_pending) {
            yghv_protect_rearm(vcpu);
            vcpu->vmcb->state.rflags &= ~0x100ULL;
            return 0;
        }
        /* Guest-visible #DB (INT1, TF, HW breakpoints): re-inject, no error code */
        vcpu->vmcb->control.event_injection =
            SVM_EVENTINJ_VALID | SVM_EVENTINJ_TYPE_EXC | 0x01;
        return 0;

    /* NPF = fault-like, no RIP advance — instruction re-executes */
    case SVM_EXIT_NPF: {
        static uint64_t npf_logged = 0;
        uint64_t info1 = vcpu->vmcb->control.exitinfo1;
        uint64_t pf_ec = 0;
        yghv_npf_result_t npf_result = YGHV_NPF_NONE;

        /* D1: NPF diagnostics — count every NPF, record the last fault site.
           In-memory only (no file write in the VMEXIT path). */
        g_npf_count++;
        g_last_npf_gpa = vcpu->vmcb->control.exitinfo2;
        g_last_npf_err = vcpu->vmcb->control.exitinfo1;
        g_last_npf_rip = vcpu->vmcb->state.rip;

        /* 9.190 B-1full: APIC MMIO write trap.  If the faulting GPA is the
           virtualized xAPIC page, emulate the write (EOI/TPR/ICR/timer forward
           to the real APIC) and advance RIP — never inject #PF on the APIC
           page. */
        if (g_v98_apic_shadow &&
            (vcpu->vmcb->control.exitinfo2 & ~0xFFFULL) == 0xFEE00000ULL) {
            if (yghv_apic_mmio_npf(vcpu) == 0) {
                svm_finish_exit(vcpu);
                return 0;
            }
            LOG_ERROR("APIC MMIO decode failed RIP=0x%llx GPA=0x%llx nf=%u ib=%02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x",
                vcpu->vmcb->state.rip, vcpu->vmcb->control.exitinfo2,
                vcpu->vmcb->control.byte_fetched,
                vcpu->vmcb->control.instruction_bytes[0],
                vcpu->vmcb->control.instruction_bytes[1],
                vcpu->vmcb->control.instruction_bytes[2],
                vcpu->vmcb->control.instruction_bytes[3],
                vcpu->vmcb->control.instruction_bytes[4],
                vcpu->vmcb->control.instruction_bytes[5],
                vcpu->vmcb->control.instruction_bytes[6],
                vcpu->vmcb->control.instruction_bytes[7],
                vcpu->vmcb->control.instruction_bytes[8],
                vcpu->vmcb->control.instruction_bytes[9],
                vcpu->vmcb->control.instruction_bytes[10],
                vcpu->vmcb->control.instruction_bytes[11],
                vcpu->vmcb->control.instruction_bytes[12],
                vcpu->vmcb->control.instruction_bytes[13],
                vcpu->vmcb->control.instruction_bytes[14]);
        }

        if (info1 & NPF_INFO1_WRITE)
            npf_result = yghv_protect_on_npf_write(vcpu,
                vcpu->vmcb->control.exitinfo2);

        if (npf_result == YGHV_NPF_ALLOW) {
            static uint64_t ring0_logged = 0;
            vcpu->vmcb->state.rflags |= 0x100ULL;  /* TF */
            if (vcpu->vmcb->state.cpl == 0 && ring0_logged++ < 32)
                LOG_ERROR("protect: ring0 write allowed gpa=0x%llx",
                    vcpu->vmcb->control.exitinfo2);
            return 0;
        }
        if (npf_result == YGHV_NPF_DENY) {
            /* foreign user-mode write: inject #PF with accurate error code */
            if (info1 & NPF_INFO1_PRESENT) pf_ec |= (1ULL << 0);
            if (info1 & NPF_INFO1_WRITE)   pf_ec |= (1ULL << 1);
            if (info1 & NPF_INFO1_USER)    pf_ec |= (1ULL << 2);
            if (info1 & NPF_INFO1_RSVD)    pf_ec |= (1ULL << 3);
            if (info1 & NPF_INFO1_EXEC)    pf_ec |= (1ULL << 4);
            vcpu->vmcb->control.event_injection =
                SVM_EVENTINJ_VALID | SVM_EVENTINJ_TYPE_EXC |
                SVM_EVENTINJ_ERROR_VALID | 0x0E | (pf_ec << 32);
            LOG_ERROR("protect: foreign write denied gpa=0x%llx cr3=0x%llx",
                vcpu->vmcb->control.exitinfo2, vcpu->vmcb->state.cr3);
            return 0;
        }

        if (g_npt_test_active) {
            LOG_ERROR("NPT test NPF: GPA=0x%llx RIP=0x%llx info1=0x%llx",
                vcpu->vmcb->control.exitinfo2, vcpu->vmcb->state.rip, info1);
            yghv_trace("npf test");
            npt_set_page_perm(&g_npt, g_npt_test_pa, NPT_PERM_PRESENT | NPT_PERM_WRITABLE);
            g_npt_test_active = 0;
            return 0;
        }

        if (npf_logged < 5) {
            LOG_ERROR("NPF early: exits=%llu np=0x%llx ncr3=0x%llx info1=0x%llx GPA=0x%llx RIP=0x%llx",
                vcpu->resident_exits, vcpu->vmcb->control.np_enable,
                vcpu->vmcb->control.ncr3, info1, vcpu->vmcb->control.exitinfo2,
                vcpu->vmcb->state.rip);
            npf_logged++;
        }

        if (info1 & NPF_INFO1_PRESENT) pf_ec |= (1ULL << 0);
        if (info1 & NPF_INFO1_WRITE)   pf_ec |= (1ULL << 1);
        if (info1 & NPF_INFO1_USER)    pf_ec |= (1ULL << 2);
        if (info1 & NPF_INFO1_RSVD)    pf_ec |= (1ULL << 3);
        if (info1 & NPF_INFO1_EXEC)    pf_ec |= (1ULL << 4);

        vcpu->vmcb->control.event_injection =
            SVM_EVENTINJ_VALID | SVM_EVENTINJ_TYPE_EXC | SVM_EVENTINJ_ERROR_VALID | 0x0E | (pf_ec << 32);

        return 0;
    }

    case SVM_EXIT_AVIC_INCOMPLETE_IPI:
        return svm_handle_avic_incomplete_ipi(vcpu);

    case SVM_EXIT_AVIC_UNACCELERATED_ACCESS:
        return svm_handle_avic_unaccelerated(vcpu);

    /* Interrupts — no RIP advance, guest interrupt handler will execute */
    case SVM_EXIT_INTR:
    case SVM_EXIT_NMI:
        vcpu->resident_interrupt_exits++;
        g_intr_exit_count++;   /* D1b: INTR/NMI VMEXITs (= injection attempts) */
        return 0;

    /* Instruction-boundary exits — advance RIP */
    case SVM_EXIT_HLT:
    case SVM_EXIT_PAUSE:
    case SVM_EXIT_INVD:
    case SVM_EXIT_INVLPG:
    case SVM_EXIT_INVLPGA:
    case SVM_EXIT_WBINVD:
    case SVM_EXIT_MONITOR:
    case SVM_EXIT_MWAIT:
    case SVM_EXIT_MWAIT_COND:
    case SVM_EXIT_XSETBV:
    case SVM_EXIT_RDTSC:
    case SVM_EXIT_RDTSCP:
        svm_finish_exit(vcpu);
        return 0;

    case SVM_EXIT_EXCEPTION_UD:
        LOG_ERROR("Guest #UD at RIP=0x%llx", vcpu->vmcb->state.rip);
        return 1;

    /* v101: guest fault vectors that precede the context-switch stack crash.
       Capture the site and stop instead of letting Windows bugcheck/freeze. */
    case SVM_EXIT_EXCEPTION_DF:
    case SVM_EXIT_EXCEPTION_NP:
    case SVM_EXIT_EXCEPTION_SS:
    case SVM_EXIT_EXCEPTION_GP:
        g_v101_gp_exitcode = exitcode;
        g_v101_gp_err = vcpu->vmcb->control.exitinfo1;
        g_v101_gp_rip = vcpu->vmcb->state.rip;
        g_v101_gp_rsp = vcpu->vmcb->state.rsp;
        g_v101_gp_cr3 = vcpu->vmcb->state.cr3;
        g_v101_gp_gs_base = vcpu->vmcb->state.gs_base;
        g_v101_gp_seen = TRUE;
        return 1;

    case SVM_EXIT_SHUTDOWN:
        LOG_ERROR("Guest shutdown");
        return 1;

    default:
        if (g_os_resident_mode) {
            static ULONG resident_unknown_logged = 0;
            if (resident_unknown_logged++ < 8)
                LOG_ERROR("resident unknown exit: 0x%llx", exitcode);
            return 0;
        }
        LOG_ERROR("Unhandled exit: 0x%llx, info1=0x%llx, RIP=0x%llx",
            exitcode, vcpu->vmcb->control.exitinfo1, vcpu->vmcb->state.rip);
        LOG_ERROR("VMCB ctl: np=0x%llx ncr3=0x%llx asid=%u tlb=%u evinj=0x%llx nrip=0x%llx clean=0x%x",
            vcpu->vmcb->control.np_enable, vcpu->vmcb->control.ncr3,
            vcpu->vmcb->control.guest_asid, vcpu->vmcb->control.tlb_control,
            vcpu->vmcb->control.event_injection, vcpu->vmcb->control.next_rip,
            vcpu->vmcb->control.vmcb_clean_bits);
        LOG_ERROR("VMCB state: rip=0x%llx rsp=0x%llx rflags=0x%llx rax=0x%llx cr0=0x%llx cr3=0x%llx cr4=0x%llx efer=0x%llx cpl=%u",
            vcpu->vmcb->state.rip, vcpu->vmcb->state.rsp, vcpu->vmcb->state.rflags,
            vcpu->vmcb->state.rax, vcpu->vmcb->state.cr0, vcpu->vmcb->state.cr3,
            vcpu->vmcb->state.cr4, vcpu->vmcb->state.efer, vcpu->vmcb->state.cpl);
        LOG_ERROR("VMCB seg: cs=%x/%x/%x base=0x%llx ss=%x/%x/%x base=0x%llx ds=%x/%x/%x base=0x%llx",
            vcpu->vmcb->state.cs_selector, vcpu->vmcb->state.cs_attrib, vcpu->vmcb->state.cs_limit,
            vcpu->vmcb->state.cs_base,
            vcpu->vmcb->state.ss_selector, vcpu->vmcb->state.ss_attrib, vcpu->vmcb->state.ss_limit,
            vcpu->vmcb->state.ss_base,
            vcpu->vmcb->state.ds_selector, vcpu->vmcb->state.ds_attrib, vcpu->vmcb->state.ds_limit,
            vcpu->vmcb->state.ds_base);
        return 1;
    }
}

static void svm_emulate_cpuid(svm_vcpu_t *vcpu) {
    int cpu_info[4];
    uint32_t leaf = (uint32_t)vcpu->regs.rax;
    uint32_t subleaf = (uint32_t)vcpu->regs.rcx;

    /* Stealth: hide hypervisor presence */
    if (leaf >= 0x40000000 && leaf <= 0x400000FF) {
        /* Hypervisor CPUID range — return zeros */
        cpu_info[0] = cpu_info[1] = cpu_info[2] = cpu_info[3] = 0;
    } else if (leaf == 0x80000001) {
        /* Extended features: clear SVM bit */
        __cpuidex(cpu_info, leaf, subleaf);
        cpu_info[2] &= ~(1 << 2); /* clear SVM feature bit */
    } else if (leaf == 0x8000000A) {
        /* SVM revision/features: return zeros */
        cpu_info[0] = cpu_info[1] = cpu_info[2] = cpu_info[3] = 0;
    } else if (leaf == 1) {
        /* Feature flags: clear hypervisor present bit */
        __cpuidex(cpu_info, leaf, subleaf);
        cpu_info[2] &= ~(1U << 31);
    } else {
        __cpuidex(cpu_info, leaf, subleaf);
    }

    vcpu->regs.rax = cpu_info[0];
    vcpu->regs.rbx = cpu_info[1];
    vcpu->regs.rcx = cpu_info[2];
    vcpu->regs.rdx = cpu_info[3];
}

static uint64_t svm_host_read_msr(uint32_t msr) {
    uint32_t low, high;
    __asm__ volatile("rdmsr" : "=a"(low), "=d"(high) : "c"(msr));
    return ((uint64_t)high << 32) | low;
}

static int svm_handle_msr(svm_vcpu_t *vcpu) {
    uint32_t msr = (uint32_t)vcpu->regs.rcx;
    uint64_t data;
    BOOLEAN write = (vcpu->vmcb->control.exitinfo1 & 1) != 0;

    vcpu->resident_msr_exits++;

    if (msr == MSR_VM_CR) {
        if (write)
            return 0; /* drop guest writes to VM_CR */
        data = VM_CR_SVMDIS;
        vcpu->regs.rax = (uint32_t)data;
        vcpu->regs.rdx = (uint32_t)(data >> 32);
        return 0;
    }

    /* 9.174: virtualize MSR_GS_BASE / MSR_KERNEL_GS_BASE.  The guest's GS
       state lives ONLY in the VMCB (svm_prepare_vcpu sets MSRPM read+write
       intercept on both).  A guest wrmsr updates the VMCB state; a guest
       rdmsr returns the VMCB state.  This keeps the guest's GS base isolated
       from the physical MSR the host uses between VMEXITs — without it the
       guest's context-switch wrmsr pollutes the host GS base (and VMEXIT
       VMSAVE/VMLOAD fights over it), which the 9.165-9.170 dumps implicated
       in the OS-as-guest freeze. */
    if (msr == 0xC0000101u) {   /* MSR_GS_BASE */
        if (write) {
            data = (vcpu->regs.rdx << 32) | vcpu->regs.rax;
            vcpu->vmcb->state.gs_base = data;
        } else {
            data = vcpu->vmcb->state.gs_base;
            vcpu->regs.rax = (uint32_t)data;
            vcpu->regs.rdx = (uint32_t)(data >> 32);
        }
        return 0;
    }
    if (msr == 0xC0000102u) {   /* MSR_KERNEL_GS_BASE */
        if (write) {
            data = (vcpu->regs.rdx << 32) | vcpu->regs.rax;
            vcpu->vmcb->state.kernel_gs_base = data;
        } else {
            data = vcpu->vmcb->state.kernel_gs_base;
            vcpu->regs.rax = (uint32_t)data;
            vcpu->regs.rdx = (uint32_t)(data >> 32);
        }
        return 0;
    }

    if (write)
        return 0; /* REV-008: drop guest writes to un-emulated MSRs without
                     clobbering guest RAX/RDX with a host read value */

    data = svm_host_read_msr(msr);
    vcpu->regs.rax = (uint32_t)data;
    vcpu->regs.rdx = (uint32_t)(data >> 32);

    return 0;
}

static int svm_handle_cr(svm_vcpu_t *vcpu) {
    uint64_t exitcode = vcpu->vmcb->control.exitcode;
    vcpu->resident_cr_exits++;

    /* 9.175: emulate guest CR3 writes (mov cr3, r/m64).  OS-as-guest's guest
       IS Windows, so process switch writes CR3 constantly; without an
       intercept that CR3 write runs naked in guest mode and its TLB effect can
       race the host — the last untested candidate for the whole-machine
       freeze.  Decode the source register from the VMCB Decode-Assist
       instruction bytes; guest GPRs were saved to vcpu->regs by the trampoline
       before dispatch.  All other CR intercepts stay fail-closed (REV-009). */
    if (exitcode == SVM_EXIT_CR3_WRITE) {
        const uint8_t *ib = vcpu->vmcb->control.instruction_bytes;
        uint8_t n = vcpu->vmcb->control.byte_fetched;
        int i = 0;
        int rex_b = 0;
        uint8_t modrm, rm, reg;
        uint64_t *slot;

        /* 9.180 MSV fail-close: under the minimal shadow validation the guest
           must stay on the deep-copied independent CR3; a process-switch CR3
           write would drag it back to the shared host tables (the 9.179
           failure mode), so stop the guest cleanly on the first one instead
           of letting it fall back / freeze.  No trace here: a file write in
           the GIF=0 dispatch path can block and 0x139 (v100b lesson); the
           host-side step200 block reports g_msv_cr3_seen after the stop. */
        if (g_msv_test) {
            g_msv_cr3_seen = TRUE;
            g_msv_cr3_writes++;
            g_os_guest_stop_requested = TRUE;
            return 1;
        }

        if (n >= 3) {
            /* skip REX prefix (0x40-0x4F); REX.B extends rm to r8-r15 */
            while (i < n && (ib[i] & 0xF0) == 0x40) {
                if (ib[i] & 0x02)
                    rex_b = 1;
                i++;
            }
            if (i + 2 < n && ib[i] == 0x0F && ib[i + 1] == 0x22) {
                modrm = ib[i + 2];
                reg = (modrm >> 3) & 7;
                rm = modrm & 7;
                if (reg == 3 /* CR3 */ && (modrm & 0xC0) == 0xC0) {
                    switch (rm | (rex_b << 3)) {
                        case 0x00: slot = &vcpu->regs.rax; break;
                        case 0x01: slot = &vcpu->regs.rcx; break;
                        case 0x02: slot = &vcpu->regs.rdx; break;
                        case 0x03: slot = &vcpu->regs.rbx; break;
                        case 0x05: slot = &vcpu->regs.rbp; break;
                        case 0x06: slot = &vcpu->regs.rsi; break;
                        case 0x07: slot = &vcpu->regs.rdi; break;
                        case 0x08: slot = &vcpu->regs.r8;  break;
                        case 0x09: slot = &vcpu->regs.r9;  break;
                        case 0x0A: slot = &vcpu->regs.r10; break;
                        case 0x0B: slot = &vcpu->regs.r11; break;
                        case 0x0C: slot = &vcpu->regs.r12; break;
                        case 0x0D: slot = &vcpu->regs.r13; break;
                        case 0x0E: slot = &vcpu->regs.r14; break;
                        case 0x0F: slot = &vcpu->regs.r15; break;
                        default:
                            LOG_ERROR("CR3 write src rm=%u unsupported", rm);
                            return 1;
                    }
                    vcpu->vmcb->state.cr3 = *slot;
                    yghv_trace_u64("os guest cr3 write", vcpu->vmcb->state.cr3);
                    return 0;
                }
            }
        }
        LOG_ERROR("CR3 write not decoded (exit=0x%llx n=%u)", exitcode, n);
        return 1;
    }

    LOG_ERROR("CR intercept not emulated (exit=0x%llx)", exitcode);
    return 1;
}
