#ifndef YGHV_SVM_VCPU_H
#define YGHV_SVM_VCPU_H

#include "svm_defs.h"
#include "vmcb.h"
#include "npt.h"

#define SVM_MSRPM_PAGES  2
#define SVM_IOPM_PAGES   3
#define SVM_HOST_STACK_PAGES  4
#define SVM_GUEST_STACK_PAGES 2

#define SVM_RESIDENT_OFF      0
#define SVM_RESIDENT_STARTING 1
#define SVM_RESIDENT_ACTIVE   2
#define SVM_RESIDENT_STOPPING 3
#define SVM_RESIDENT_STOPPED  4
#define SVM_RESIDENT_FAILED   5

/* VCPU struct field offsets (used in assembly trampoline) */
#define SVM_VCPU_VMCB_VA_OFFSET          0x00
#define SVM_VCPU_VMCB_PA_OFFSET          0x08
#define SVM_VCPU_HOST_VMCB_VA_OFFSET     0x10
#define SVM_VCPU_HOST_VMCB_PA_OFFSET     0x18
#define SVM_VCPU_HSave_VA_OFFSET         0x20
#define SVM_VCPU_HSave_PA_OFFSET         0x28
#define SVM_VCPU_HOST_STACK_OFFSET       0x30
#define SVM_VCPU_HOST_STACK_TOP_OFFSET   0x38
#define SVM_VCPU_HOST_RSP_OFFSET         0x40
#define SVM_VCPU_OLD_HSave_OFFSET        0x48
#define SVM_VCPU_OLD_EFER_OFFSET         0x50
#define SVM_VCPU_MSRPM_OFFSET            0x58
#define SVM_VCPU_MSRPM_PA_OFFSET         0x60
#define SVM_VCPU_IOPM_OFFSET             0x68
#define SVM_VCPU_IOPM_PA_OFFSET          0x70
#define SVM_VCPU_REGS_OFFSET             0x78
#define SVM_VCPU_CONTEXT_PROOF_OFFSET    0xF0

#define SVM_VMCB_STATE_RFLAGS_OFFSET     0x570
#define SVM_VMCB_STATE_RIP_OFFSET        0x578
#define SVM_VMCB_STATE_RSP_OFFSET        0x5D8
#define SVM_VMCB_STATE_RAX_OFFSET        0x5F8

/* v100: freeze-site ring buffer. 64 entries keeps the hot-path record tiny
   while giving the monitor a meaningful last-64-exits snapshot. */
#define YGHV_V100_RING_ENTRIES 64

typedef struct {
    volatile uint64_t seq;
    uint64_t exitcode;
    uint64_t exitinfo1;
    uint64_t exitinfo2;
    uint64_t rip;
    uint64_t cr3;
    uint64_t rsp;
    uint64_t rflags;
    uint64_t cpl;
} svm_v100_ring_entry_t;

/* Guest register context (for saving/restoring guest GPRs) */
typedef struct {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rdi, rsi, rbp, rbx, rdx, rcx, rax;
} __attribute__((packed)) svm_guest_regs_t;

#define SVM_GUEST_REGS_SIZE          sizeof(svm_guest_regs_t)
#define SVM_REG_R15_OFFSET           0x00
#define SVM_REG_R14_OFFSET           0x08
#define SVM_REG_R13_OFFSET           0x10
#define SVM_REG_R12_OFFSET           0x18
#define SVM_REG_R11_OFFSET           0x20
#define SVM_REG_R10_OFFSET           0x28
#define SVM_REG_R9_OFFSET            0x30
#define SVM_REG_R8_OFFSET            0x38
#define SVM_REG_RDI_OFFSET           0x40
#define SVM_REG_RSI_OFFSET           0x48
#define SVM_REG_RBP_OFFSET           0x50
#define SVM_REG_RBX_OFFSET           0x58
#define SVM_REG_RDX_OFFSET           0x60
#define SVM_REG_RCX_OFFSET           0x68
#define SVM_REG_RAX_OFFSET           0x70

#define SVM_REG_XMM0_OFFSET          0x78
#define SVM_REG_XMM1_OFFSET          0x88
#define SVM_REG_XMM2_OFFSET          0x98
#define SVM_REG_XMM3_OFFSET          0xA8
#define SVM_REG_XMM4_OFFSET          0xB8
#define SVM_REG_XMM5_OFFSET          0xC8

typedef struct {
    uint64_t cr0;
    uint64_t cr3;
    uint64_t cr4;
    uint64_t efer;
    uint8_t  xmm[6][16];
} svm_context_proof_frame_t;

#define SVM_CONTEXT_PROOF_CR0_OFFSET   0x00
#define SVM_CONTEXT_PROOF_CR3_OFFSET   0x08
#define SVM_CONTEXT_PROOF_CR4_OFFSET   0x10
#define SVM_CONTEXT_PROOF_EFER_OFFSET  0x18
#define SVM_CONTEXT_PROOF_XMM_OFFSET   0x20
#define SVM_CONTEXT_PROOF_XMM_SLOT_SIZE 16

/* Per-VCPU structure */
typedef struct {
    /* VMCB pointers */
    vmcb_t *vmcb;
    uint64_t vmcb_pa;
    vmcb_t *host_vmcb;
    uint64_t host_vmcb_pa;
    void *hsave;
    uint64_t hsave_pa;
    void *host_stack;
    uint64_t host_stack_top;
    uint64_t host_rsp;

    /* Saved MSR values */
    uint64_t old_hsave;
    uint64_t old_efer;

    /* Intercept bitmaps */
    void *msrpm;
    uint64_t msrpm_pa;
    void *iopm;
    uint64_t iopm_pa;

    /* Guest register save area */
    svm_guest_regs_t regs;

    /* Context proof (CRx + EFER + XMM verification frame) */
    svm_context_proof_frame_t context_proof;

    /* AVIC state (step25: vAPIC backing + APIC ID tables) */
    void *avic_backing_page;
    uint64_t avic_backing_pa;
    void *avic_logical_id_table;
    uint64_t avic_logical_id_pa;
    void *avic_physical_id_table;
    uint64_t avic_physical_id_pa;
    void *avic_host_apic_va;
    void *avic_host_idt;
    uint64_t avic_old_idt_base;
    uint16_t avic_old_idt_limit;
    volatile ULONG avic_apic_id;
    KTIMER avic_timer;
    KDPC avic_timer_dpc;
    BOOLEAN avic_timer_initialized;
    volatile LONG avic_timer_armed;
    volatile ULONG avic_timer_vector;
    volatile ULONG avic_timer_period_ms;
    volatile ULONG64 avic_noaccel_exits;
    volatile ULONG64 avic_incomplete_ipi_exits;
    volatile ULONG64 avic_irr_injections;

    /* v100: exit-site ring buffer (monitor reads it when the core stalls). */
    volatile uint64_t v100_seq;
    svm_v100_ring_entry_t v100_ring[YGHV_V100_RING_ENTRIES];

    /* Resident state */
    volatile int32_t resident_state;
    uint32_t resident_index;
    uint64_t resident_exits;
    uint64_t resident_msr_exits;
    uint64_t resident_cr_exits;
    uint64_t resident_interrupt_exits;
    volatile uint64_t last_exitcode;   /* 9.152 diag: last VMEXIT code handled */
    volatile LONG npt_flush_pending;
    volatile LONG rearm_pending;
    volatile uint64_t rearm_gpa;
    volatile LONG pause_requested;
    volatile LONG pause_ack;
    KEVENT pause_done_event;
    KEVENT resume_event;

    /* Synthetic resident guests need a stack that stays mapped in NPT
       (the host stack is hypervisor-private and R1-excluded). Kept at the
       end so earlier fixed offsets used by the trampoline stay stable. */
    void *guest_stack;
    uint64_t guest_stack_pa;
    /* Per-vcpu VMMCALL control key (P0 auth hardening). */
    volatile uint64_t auth_key;
} svm_vcpu_t;

extern svm_vcpu_t *g_vcpus[SVM_MAX_CORES];
extern ULONG g_vcpu_count;

/* Static offset assertions */
_Static_assert(offsetof(svm_vcpu_t, vmcb) == SVM_VCPU_VMCB_VA_OFFSET, "VCPU VMCB VA offset");
_Static_assert(offsetof(svm_vcpu_t, vmcb_pa) == SVM_VCPU_VMCB_PA_OFFSET, "VCPU VMCB PA offset");
_Static_assert(offsetof(svm_vcpu_t, host_vmcb) == SVM_VCPU_HOST_VMCB_VA_OFFSET, "VCPU host VMCB VA offset");
_Static_assert(offsetof(svm_vcpu_t, host_vmcb_pa) == SVM_VCPU_HOST_VMCB_PA_OFFSET, "VCPU host VMCB PA offset");
_Static_assert(offsetof(svm_vcpu_t, hsave) == SVM_VCPU_HSave_VA_OFFSET, "VCPU HSave VA offset");
_Static_assert(offsetof(svm_vcpu_t, hsave_pa) == SVM_VCPU_HSave_PA_OFFSET, "VCPU HSave PA offset");
_Static_assert(offsetof(svm_vcpu_t, host_stack) == SVM_VCPU_HOST_STACK_OFFSET, "VCPU host stack offset");
_Static_assert(offsetof(svm_vcpu_t, host_stack_top) == SVM_VCPU_HOST_STACK_TOP_OFFSET, "VCPU host stack top offset");
_Static_assert(offsetof(svm_vcpu_t, host_rsp) == SVM_VCPU_HOST_RSP_OFFSET, "VCPU host RSP offset");
_Static_assert(offsetof(svm_vcpu_t, old_hsave) == SVM_VCPU_OLD_HSave_OFFSET, "VCPU old HSave offset");
_Static_assert(offsetof(svm_vcpu_t, old_efer) == SVM_VCPU_OLD_EFER_OFFSET, "VCPU old EFER offset");
_Static_assert(offsetof(svm_vcpu_t, msrpm) == SVM_VCPU_MSRPM_OFFSET, "VCPU MSRPM VA offset");
_Static_assert(offsetof(svm_vcpu_t, msrpm_pa) == SVM_VCPU_MSRPM_PA_OFFSET, "VCPU MSRPM PA offset");
_Static_assert(offsetof(svm_vcpu_t, iopm) == SVM_VCPU_IOPM_OFFSET, "VCPU IOPM VA offset");
_Static_assert(offsetof(svm_vcpu_t, iopm_pa) == SVM_VCPU_IOPM_PA_OFFSET, "VCPU IOPM PA offset");
_Static_assert(offsetof(svm_vcpu_t, regs) == SVM_VCPU_REGS_OFFSET, "VCPU regs offset");
_Static_assert(offsetof(svm_vcpu_t, context_proof) == SVM_VCPU_CONTEXT_PROOF_OFFSET, "VCPU context proof offset");
_Static_assert(sizeof(svm_guest_regs_t) == 0x78, "Guest regs size");
_Static_assert(offsetof(svm_guest_regs_t, r15) == SVM_REG_R15_OFFSET, "REG R15 offset");
_Static_assert(offsetof(svm_guest_regs_t, rcx) == SVM_REG_RCX_OFFSET, "REG RCX offset");
_Static_assert(offsetof(svm_guest_regs_t, rdx) == SVM_REG_RDX_OFFSET, "REG RDX offset");
_Static_assert(offsetof(svm_guest_regs_t, rax) == SVM_REG_RAX_OFFSET, "REG RAX offset");
_Static_assert(SVM_MSRPM_PAGES * HV_PAGE_SIZE == 0x2000, "SVM MSRPM size");
_Static_assert(SVM_IOPM_PAGES * HV_PAGE_SIZE == 0x3000, "SVM IOPM size");
_Static_assert(offsetof(vmcb_t, state) + offsetof(vmcb_state_t, rflags) == SVM_VMCB_STATE_RFLAGS_OFFSET,
               "VMCB RFLAGS assembly offset");
_Static_assert(offsetof(vmcb_t, state) + offsetof(vmcb_state_t, rip) == SVM_VMCB_STATE_RIP_OFFSET,
               "VMCB RIP assembly offset");
_Static_assert(offsetof(vmcb_t, state) + offsetof(vmcb_state_t, rsp) == SVM_VMCB_STATE_RSP_OFFSET,
               "VMCB RSP assembly offset");
_Static_assert(offsetof(vmcb_t, state) + offsetof(vmcb_state_t, rax) == SVM_VMCB_STATE_RAX_OFFSET,
               "VMCB RAX assembly offset");
_Static_assert(offsetof(svm_context_proof_frame_t, cr0) == SVM_CONTEXT_PROOF_CR0_OFFSET, "Context proof CR0");
_Static_assert(sizeof(svm_context_proof_frame_t) == 0x80, "Context proof frame size");

/* Probe result for multi-core testing */
typedef struct {
    uint32_t index;
    uint16_t group;
    uint8_t  number;
    uint8_t  observed;
    int      status;
    uint64_t exitcode;
    uint64_t exitinfo1;
    uint64_t exitinfo2;
    uint64_t old_hsave;
    uint64_t old_efer;
    uint64_t restored_hsave;
    uint64_t restored_efer;
} svm_probe_result_t;

/* Assembly trampoline entry points */
uint64_t svm_vmrun_trampoline(svm_vcpu_t *vcpu);
uint64_t svm_trampoline_os_enter(svm_vcpu_t *vcpu, int if1);
int  svm_vmrun_context_proof(svm_vcpu_t *vcpu);
void svm_vmrun_trampoline_poisoned(svm_vcpu_t *vcpu);
int  svm_resident_enter(svm_vcpu_t *vcpu);
int  svm_resident_vmmcall_stop(void);
int  svm_avic_record_pending_intr(svm_vcpu_t *vcpu);
int  svm_avic_timer_init(svm_vcpu_t *vcpu);
int  svm_avic_start_timer(svm_vcpu_t *vcpu, uint32_t vector, uint32_t period_ms);
int  svm_avic_update_timer(svm_vcpu_t *vcpu, uint32_t offset);

/* Guest test labels (for determining guest code region bounds) */
extern const uint8_t svm_trampoline_test_guest[];
extern const uint8_t svm_trampoline_test_guest_resume[];
extern const uint8_t svm_trampoline_test_guest_end[];

/* SVM core API */
void svm_prepare_vcpu(svm_vcpu_t *vcpu, uint64_t guest_rip);
int  svm_core_init(void);
int  svm_core_cleanup(void);
int  svm_alloc_vcpu(uint32_t core_id, svm_vcpu_t **out);
int  svm_core_prepare_probe(uint32_t count);
int  svm_core_probe_current(uint32_t index, svm_probe_result_t *result);
int  svm_core_run_trampoline_once(uint32_t core_id);
int  svm_core_run_trampoline_test(uint32_t core_id);
int  svm_core_set_npt(uint32_t core_id, uint64_t ncr3);
svm_vcpu_t *svm_core_get_vcpu(uint32_t index);

/* Resident lifecycle */
int  svm_core_prepare_resident(uint32_t count);
int  svm_resident_try_activate(svm_vcpu_t *vcpu);
int  svm_core_enter_resident_current(uint32_t index);
int  svm_core_stop_resident_current(uint32_t index);
int  svm_core_resident_state(uint32_t index);
uint64_t svm_core_resident_exit_count(uint32_t index);

/* Multi-core */
int      svm_core_prepare_vcpu_other(uint32_t core_id);
ULONG_PTR svm_core_ipi_prepare_vcpu(ULONG_PTR arg);
ULONG_PTR svm_core_ipi_set_npt(ULONG_PTR arg);
NTSTATUS svm_core_start_remote_residents(ULONG online);
NTSTATUS svm_core_start_persistent_residents(ULONG online);
void     svm_core_stop_all_residents(void);
void     svm_core_wait_all_stopped(ULONG online);
void     svm_core_wait_remote_ready(ULONG online);
NTSTATUS svm_core_pause_residents_for_patch(void);
void     svm_core_resume_residents(void);

#endif
