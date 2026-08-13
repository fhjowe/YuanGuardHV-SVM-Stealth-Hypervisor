# svm_trampoline.S — AMD SVM VMRUN trampoline for YuanGuardHV
# Resident-mode: full guest GPR save/restore via vcpu->regs each cycle.
# On entry: RCX = vcpu pointer. Returns VMCB exitcode in RAX.
#
# Stack layout on entry:
#   [rsp+0x40] host rbx  [rsp+0x38] host rbp  [rsp+0x30] host r12
#   [rsp+0x28] host r13  [rsp+0x20] host r14  [rsp+0x18] host r15
#   [rsp+0x10] host rdi  [rsp+0x08] host rsi  [rsp+0x00] saved vcpu
#
# vcpu->regs offsets:
#   +0x00 r15  +0x08 r14  +0x10 r13  +0x18 r12  +0x20 r11  +0x28 r10
#   +0x30 r9   +0x38 r8   +0x40 rdi  +0x48 rsi  +0x50 rbp  +0x58 rbx
#   +0x60 rdx  +0x68 rcx  +0x70 rax

    .intel_syntax noprefix
    .text

# --- Guest test code ---
    .globl svm_trampoline_test_guest
    .globl svm_trampoline_test_guest_resume
    .globl svm_trampoline_test_guest_end

svm_trampoline_test_guest:
    mov     rax, 1                  # YGHV_CMD_HEARTBEAT
    vmmcall
svm_trampoline_test_guest_resume:
    jmp svm_trampoline_test_guest
svm_trampoline_test_guest_end:

# --- NPT permission test guest ---
    .globl svm_trampoline_test_npt_guest
    .globl svm_trampoline_test_npt_guest_resume
    .globl svm_trampoline_test_npt_guest_end

svm_trampoline_test_npt_guest:
    mov     rax, qword ptr [rdi]    # access protected page -> NPF
    mov     rax, 2                  # YGHV_CMD_STOP_INTERNAL
    vmmcall
svm_trampoline_test_npt_guest_resume:
    jmp svm_trampoline_test_npt_guest
svm_trampoline_test_npt_guest_end:

# --- Protect write test guest ---
    .globl svm_trampoline_test_prot_write
    .globl svm_trampoline_test_prot_write_resume
    .globl svm_trampoline_test_prot_write_end
svm_trampoline_test_prot_write:
    mov     qword ptr [rdi], 1      # write protected page -> NPF
    mov     rax, 2                  # YGHV_CMD_STOP_INTERNAL
    vmmcall
svm_trampoline_test_prot_write_resume:
    jmp svm_trampoline_test_prot_write
svm_trampoline_test_prot_write_end:

# --- Resident hook guest: call hooked dummy, capture stub result ---
    .globl svm_trampoline_test_hook_guest
    .globl svm_trampoline_test_hook_guest_end
svm_trampoline_test_hook_guest:
    call    rsi
    mov     rdx, rax
    mov     rax, 2                  # YGHV_CMD_STOP_INTERNAL
    vmmcall
svm_trampoline_test_hook_guest_end:

# --- Minimal bare-metal bring-up guest: STOP_INTERNAL only ---
    .globl svm_trampoline_test_min_guest
    .globl svm_trampoline_test_min_guest_end
svm_trampoline_test_min_guest:
    mov     rax, 2                  # YGHV_CMD_STOP_INTERNAL
    vmmcall
svm_trampoline_test_min_guest_end:

# --- Resident CPUID stealth guest ---
    .globl svm_trampoline_test_cpuid_guest
    .globl svm_trampoline_test_cpuid_guest_end
svm_trampoline_test_cpuid_guest:
    mov     r15, rcx                # save cookie; cpuid clobbers rcx
    mov     rax, 1
    cpuid
    mov     r14, rcx                # leaf 1 ecx: bit31 = hypervisor present
    mov     rax, 0x40000000
    cpuid
    mov     r8, rax
    mov     r9, rbx
    mov     r10, rcx
    mov     r11, rdx
    mov     rax, 0x80000001
    cpuid
    mov     r12, rcx                # ecx bit 2 = SVM
    mov     rax, 0x8000000A
    cpuid
    mov     r13, rax
    mov     rax, 2                  # YGHV_CMD_STOP_INTERNAL
    mov     rcx, r15
    vmmcall
svm_trampoline_test_cpuid_guest_end:

# --- Resident workload guest: protected write + hooked call + heartbeat ---
    .globl svm_trampoline_test_resident_guest
    .globl svm_trampoline_test_resident_guest_end
svm_trampoline_test_resident_guest:
    test    rdi, rdi
    jz      1f
    mov     qword ptr [rdi], 0x1234
1:
    test    rsi, rsi
    jz      2f
    call    rsi
    mov     rdx, rax
2:
    mov     rax, 1                  # YGHV_CMD_HEARTBEAT
    vmmcall
    jmp     svm_trampoline_test_resident_guest
svm_trampoline_test_resident_guest_end:

# --- Minimal host EOI interrupt handler (step32) ---
# Only acknowledges the physical APIC; the guest handles the virtual interrupt
# delivered by AVIC. Preserves every register it touches.
    .globl yghv_avic_eoi_isr
yghv_avic_eoi_isr:
    push    rax
    push    rcx
    push    rdx
    push    rsi
    push    rdi
    push    r8
    push    r9
    push    r10
    push    r11
    mov     rax, qword ptr [rip + g_avic_eoi_apic_va]
    test    rax, rax
    jz      1f
    mov     dword ptr [rax + 0xB0], 0        # physical APIC EOI
1:
    pop     r11
    pop     r10
    pop     r9
    pop     r8
    pop     rdi
    pop     rsi
    pop     rdx
    pop     rcx
    pop     rax
    iretq

# ============================================================
# uint64_t svm_vmrun_trampoline(svm_vcpu_t *vcpu)
#   RCX = vcpu pointer
#   Returns VMCB exitcode in RAX.
# ============================================================
    .globl svm_vmrun_trampoline

svm_vmrun_trampoline:
    # --- Save host callee-saved registers ---
    push    rbx
    push    rbp
    push    r12
    push    r13
    push    r14
    push    r15
    push    rdi
    push    rsi
    push    rcx                 # [rsp] = vcpu pointer

    # --- Set VM_HSAVE PA (before loading guest GPRs — wrmsr clobbers rax/rcx/rdx) ---
    mov     rax, qword ptr [rsp]      # vcpu
    mov     rax, qword ptr [rax + 0x28] # vcpu->hsave_pa
    mov     ecx, 0xC0010117           # MSR_VM_HSAVE
    mov     edx, eax
    shr     rdx, 32
    wrmsr

    # --- Load all guest GPRs from vcpu->regs ---
    mov     rax, qword ptr [rsp]      # vcpu
    mov     r15, qword ptr [rax + 0x78 + 0x00]
    mov     r14, qword ptr [rax + 0x78 + 0x08]
    mov     r13, qword ptr [rax + 0x78 + 0x10]
    mov     r12, qword ptr [rax + 0x78 + 0x18]
    mov     r11, qword ptr [rax + 0x78 + 0x20]
    mov     r10, qword ptr [rax + 0x78 + 0x28]
    mov     r9,  qword ptr [rax + 0x78 + 0x30]
    mov     r8,  qword ptr [rax + 0x78 + 0x38]
    mov     rdi, qword ptr [rax + 0x78 + 0x40]
    mov     rsi, qword ptr [rax + 0x78 + 0x48]
    mov     rbp, qword ptr [rax + 0x78 + 0x50]
    mov     rbx, qword ptr [rax + 0x78 + 0x58]
    mov     rdx, qword ptr [rax + 0x78 + 0x60]
    mov     rcx, qword ptr [rax + 0x78 + 0x68]
    # Guest RAX is loaded from VMCB state.rax by VMRUN hardware

    mov     rax, qword ptr [rax + 0x08]  # vcpu->vmcb_pa

    .byte   0x0F, 0x01, 0xDA             # VMLOAD rax — guest FS/GS/TR/LDTR + system MSRs
    clgi
    .byte   0x0F, 0x01, 0xD8             # VMRUN rax

    # --- VMEXIT return point ---
    # Save guest rdi / rsi to stack (they are guest values; we need rdi/rsi as temps)
    push    rdi                     # [rsp+0x08] = guest rdi
    push    rsi                     # [rsp+0x00] = guest rsi
    # vcpu is now at [rsp + 0x10]

    mov     rdi, qword ptr [rsp + 0x10]  # vcpu
    mov     rax, qword ptr [rdi + 0x08]  # vcpu->vmcb_pa
    .byte   0x0F, 0x01, 0xDB             # VMSAVE rax — save guest FS/GS/TR/LDTR + system MSRs
    mov     rax, qword ptr [rdi + 0x18]  # vcpu->host_vmcb_pa
    .byte   0x0F, 0x01, 0xDA             # VMLOAD rax — restore host FS/GS/TR/LDTR + system MSRs

    # Read guest RAX from VMCB state area (CPU saves it there on exit)
    mov     rsi, qword ptr [rdi]         # vcpu->vmcb
    mov     rsi, qword ptr [rsi + 0x5F8] # vmcb->state.rax
    mov     qword ptr [rdi + 0x78 + 0x70], rsi  # vcpu->regs.rax

    # --- Save remaining guest GPRs to vcpu->regs ---
    mov     qword ptr [rdi + 0x78 + 0x68], rcx
    mov     qword ptr [rdi + 0x78 + 0x60], rdx
    mov     qword ptr [rdi + 0x78 + 0x58], rbx
    mov     qword ptr [rdi + 0x78 + 0x50], rbp
    mov     qword ptr [rdi + 0x78 + 0x38], r8
    mov     qword ptr [rdi + 0x78 + 0x30], r9
    mov     qword ptr [rdi + 0x78 + 0x28], r10
    mov     qword ptr [rdi + 0x78 + 0x20], r11
    mov     qword ptr [rdi + 0x78 + 0x18], r12
    mov     qword ptr [rdi + 0x78 + 0x10], r13
    mov     qword ptr [rdi + 0x78 + 0x08], r14
    mov     qword ptr [rdi + 0x78 + 0x00], r15

    # Recover guest rsi / rdi from stack
    pop     rsi                     # guest rsi
    mov     qword ptr [rdi + 0x78 + 0x48], rsi
    pop     rsi                     # guest rdi
    mov     qword ptr [rdi + 0x78 + 0x40], rsi
    stgi                            # guest GPRs are saved; re-enable interrupts

    # --- Read exitcode from VMCB ---
    mov     rax, qword ptr [rdi]         # vcpu->vmcb
    mov     rax, qword ptr [rax + 0x70]  # control.exitcode

    # --- Restore host context ---
    pop     rcx                     # discard saved vcpu
    pop     rsi                     # host rsi
    pop     rdi                     # host rdi
    pop     r15
    pop     r14
    pop     r13
    pop     r12
    pop     rbp
    pop     rbx
    ret

# ============================================================
# uint64_t svm_trampoline_os_enter(svm_vcpu_t *vcpu)
#   Seamless OS-as-guest entry: capture the current thread context
#   as the guest, switch to the dedicated host stack, then run the
#   resident loop inside this trampoline.  On final stop the
#   trampoline returns to yghv_os_guest_host_done via a planted
#   return address.
# ============================================================
    .globl svm_trampoline_os_enter
svm_trampoline_os_enter:
    push    rax
    push    rdx
    push    rcx
    push    rbx
    push    rbp
    push    rsi
    push    rdi
    push    r8
    push    r9
    push    r10
    push    r11
    push    r12
    push    r13
    push    r14
    push    r15

    mov     rax, rcx                   # vcpu
    mov     r11, rdx                   # if1 (second arg)
    mov     rdx, rsp
    add     rdx, 120                   # original thread rsp
    mov     qword ptr [rax + 0x40], rdx # vcpu->host_rsp = guest stack base

    # guest RFLAGS: preserve current; clear IF only when if1 == 0
    pushfq
    pop     rdx
    test    r11, r11
    jnz     1f
    and     rdx, 0xFFFFFFFFFFFFFDFF
1:
    mov     r11, qword ptr [rax]       # vcpu->vmcb
    mov     qword ptr [r11 + 0x570], rdx

    pop     r15
    mov     qword ptr [rax + 0x78 + 0x00], r15
    pop     r14
    mov     qword ptr [rax + 0x78 + 0x08], r14
    pop     r13
    mov     qword ptr [rax + 0x78 + 0x10], r13
    pop     r12
    mov     qword ptr [rax + 0x78 + 0x18], r12
    pop     r11
    mov     qword ptr [rax + 0x78 + 0x20], r11
    pop     r10
    mov     qword ptr [rax + 0x78 + 0x28], r10
    pop     r9
    mov     qword ptr [rax + 0x78 + 0x30], r9
    pop     r8
    mov     qword ptr [rax + 0x78 + 0x38], r8
    pop     rdi
    mov     qword ptr [rax + 0x78 + 0x40], rdi
    pop     rsi
    mov     qword ptr [rax + 0x78 + 0x48], rsi
    pop     rbp
    mov     qword ptr [rax + 0x78 + 0x50], rbp
    pop     rbx
    mov     qword ptr [rax + 0x78 + 0x58], rbx
    pop     rcx
    mov     qword ptr [rax + 0x78 + 0x68], rcx
    pop     rdx
    mov     qword ptr [rax + 0x78 + 0x60], rdx
    pop     rax
    mov     qword ptr [rcx + 0x78 + 0x70], rax  # regs.rax; rcx = vcpu

    mov     r11, qword ptr [rcx]
    mov     qword ptr [r11 + 0x5F8], rax        # VMCB state.rax
    mov     rdx, qword ptr [rcx + 0x40]
    mov     qword ptr [r11 + 0x5D8], rdx        # VMCB state.rsp

    mov     rsp, qword ptr [rcx + 0x38]         # host_stack_top
    push    rbx
    push    rbp
    push    r12
    push    r13
    push    r14
    push    r15
    push    rdi
    push    rsi
    push    rcx
    lea     rax, [rip + svm_os_guest_host_done]
    push    rax

os_enter_resume:
    mov     rax, qword ptr [rsp + 8]            # vcpu
    mov     rax, qword ptr [rax + 0x28]         # hsave_pa
    mov     ecx, 0xC0010117
    mov     edx, eax
    shr     rdx, 32
    wrmsr

    mov     rax, qword ptr [rsp + 8]            # vcpu
    mov     rdx, qword ptr [rax]                # vcpu->vmcb
    mov     dword ptr [rdx + 0xC0], 0           # vmcb_clean_bits = 0 (v94)
    mov     r15, qword ptr [rax + 0x78 + 0x00]
    mov     r14, qword ptr [rax + 0x78 + 0x08]
    mov     r13, qword ptr [rax + 0x78 + 0x10]
    mov     r12, qword ptr [rax + 0x78 + 0x18]
    mov     r11, qword ptr [rax + 0x78 + 0x20]
    mov     r10, qword ptr [rax + 0x78 + 0x28]
    mov     r9,  qword ptr [rax + 0x78 + 0x30]
    mov     r8,  qword ptr [rax + 0x78 + 0x38]
    mov     rdi, qword ptr [rax + 0x78 + 0x40]
    mov     rsi, qword ptr [rax + 0x78 + 0x48]
    mov     rbp, qword ptr [rax + 0x78 + 0x50]
    mov     rbx, qword ptr [rax + 0x78 + 0x58]
    mov     rdx, qword ptr [rax + 0x78 + 0x60]
    mov     rcx, qword ptr [rax + 0x78 + 0x68]
    mov     rax, qword ptr [rax + 0x08]         # vmcb_pa

    .byte   0x0F, 0x01, 0xDA                    # VMLOAD
    clgi
    .byte   0x0F, 0x01, 0xD8                    # VMRUN

os_enter_exit:
    push    rdi
    push    rsi
    mov     rdi, qword ptr [rsp + 0x18]         # vcpu
    mov     rax, qword ptr [rdi + 0x08]
    .byte   0x0F, 0x01, 0xDB                    # VMSAVE guest
    mov     rax, qword ptr [rdi + 0x18]
    .byte   0x0F, 0x01, 0xDA                    # VMLOAD host

    mov     rsi, qword ptr [rdi]
    mov     rsi, qword ptr [rsi + 0x5F8]
    mov     qword ptr [rdi + 0x78 + 0x70], rsi
    mov     qword ptr [rdi + 0x78 + 0x68], rcx
    mov     qword ptr [rdi + 0x78 + 0x60], rdx
    mov     qword ptr [rdi + 0x78 + 0x58], rbx
    mov     qword ptr [rdi + 0x78 + 0x50], rbp
    mov     qword ptr [rdi + 0x78 + 0x38], r8
    mov     qword ptr [rdi + 0x78 + 0x30], r9
    mov     qword ptr [rdi + 0x78 + 0x28], r10
    mov     qword ptr [rdi + 0x78 + 0x20], r11
    mov     qword ptr [rdi + 0x78 + 0x18], r12
    mov     qword ptr [rdi + 0x78 + 0x10], r13
    mov     qword ptr [rdi + 0x78 + 0x08], r14
    mov     qword ptr [rdi + 0x78 + 0x00], r15
    pop     rsi
    mov     qword ptr [rdi + 0x78 + 0x48], rsi
    pop     rsi
    mov     qword ptr [rdi + 0x78 + 0x40], rsi

    # Step29: for INTR exits, record the physical IRR into the vAPIC IRR, then
    # fall through to the host ISR so Windows takes and EOIs the interrupt.
    movzx   eax, byte ptr [rip + g_os_guest_avic_irr_inject]
    test    al, al
    jz      os_enter_check_step21
    mov     rax, qword ptr [rdi]
    mov     rax, qword ptr [rax + 0x70]      # control.exitcode
    cmp     eax, 0x60                        # SVM_EXIT_INTR
    je      os_enter_avic_irr
    cmp     eax, 0x61                        # SVM_EXIT_NMI
    jne     os_enter_check_step21
    movzx   eax, byte ptr [rip + g_os_guest_avic_eoi_only]
    test    al, al
    jz      os_enter_check_step21
    jmp     os_enter_inject_intr

os_enter_avic_irr:
    mov     rcx, rdi
    call    svm_avic_record_pending_intr
    test    eax, eax
    jnz     svm_os_guest_host_done
    movzx   eax, byte ptr [rip + g_os_guest_avic_eoi_only]
    test    al, al
    jz      os_enter_check_host_isr
    lea     rax, [rip + g_avic_eoi_idtr]
    lidt    fword ptr [rax]
    stgi
    hlt
    clgi
    lea     rax, [rip + g_avic_old_idtr]
    lidt    fword ptr [rax]
    jmp     os_enter_resume

os_enter_check_step21:
    # Step21: for INTR/NMI exits, re-inject the intercepted event into guest.
    movzx   eax, byte ptr [rip + g_os_guest_inject_intr]
    test    al, al
    jz      os_enter_check_host_isr
    mov     rax, qword ptr [rdi]
    mov     rax, qword ptr [rax + 0x70]      # control.exitcode
    cmp     eax, 0x60                        # SVM_EXIT_INTR
    je      os_enter_inject_intr
    cmp     eax, 0x61                        # SVM_EXIT_NMI
    je      os_enter_inject_intr

os_enter_check_host_isr:
    # Default: keep GIF=0 while running the host dispatch so a clock
    # interrupt can never be serviced on the dedicated host stack.
    movzx   eax, byte ptr [rip + g_os_guest_host_isr]
    test    al, al
    jz      os_enter_dispatch_default

    # Step20: for INTR/NMI exits, switch to the thread stack and enable GIF
    # so Windows services the physical interrupt in host context (v52 path).
    mov     rax, qword ptr [rdi]
    mov     rax, qword ptr [rax + 0x70]      # control.exitcode
    cmp     eax, 0x60                        # SVM_EXIT_INTR
    je      os_enter_host_isr
    cmp     eax, 0x61                        # SVM_EXIT_NMI
    je      os_enter_host_isr

os_enter_inject_intr:
    mov     rax, qword ptr [rdi]
    mov     rdx, qword ptr [rax + 0x88]      # control.exitintinfo
    mov     qword ptr [rax + 0xA8], rdx      # control.event_injection
    jmp     os_enter_dispatch_default

os_enter_dispatch_default:
    mov     rcx, rdi                           # rcx = vcpu
    call    svm_dispatch_exit
    test    eax, eax
    jnz     svm_os_guest_host_done
    jmp     os_enter_resume

os_enter_host_isr:
    mov     rbx, rdi                           # vcpu in callee-saved reg
    mov     rax, qword ptr [rbx]               # vmcb
    movzx   ecx, byte ptr [rax + 0x4CB]        # state.cpl
    test    cl, cl
    jnz     os_enter_host_isr_old_stack
    mov     rsp, qword ptr [rax + 0x5D8]       # guest current RSP (CPL0)
    jmp     os_enter_host_isr_stgi
os_enter_host_isr_old_stack:
    mov     rsp, qword ptr [rbx + 0x40]        # host_rsp fallback
os_enter_host_isr_stgi:
    stgi
    sub     rsp, 8                             # x64 call alignment
    mov     rcx, rbx
    call    svm_dispatch_exit
    add     rsp, 8
    clgi
    test    eax, eax
    jnz     os_enter_host_isr_done
    mov     rsp, qword ptr [rbx + 0x38]        # host_stack_top
    sub     rsp, 0x50                          # restore dedicated trampoline frame
    jmp     os_enter_resume

os_enter_host_isr_done:
    mov     rcx, rbx
    lea     rax, [rip + yghv_os_guest_host_done]
    jmp     rax

svm_os_guest_host_done:
    stgi
    # Windows kernel APIs must run on the current thread's real stack;
    # restore it before jumping to the C cleanup path.
    mov     rax, qword ptr [rsp + 8]            # vcpu
    mov     rsp, qword ptr [rax + 0x40]         # vcpu->host_rsp (thread stack)
    mov     rcx, rax
    lea     rax, [rip + yghv_os_guest_host_done]
    jmp     rax

# --- Seamless guest continuation ---
# When entered with this RIP, the guest simply returns to the caller of
# svm_trampoline_os_enter, so that caller's code continues in guest mode.
    .globl svm_os_seamless_cont
svm_os_seamless_cont:
    ret

# --- Context proof trampoline (stub) ---
    .globl svm_vmrun_context_proof
svm_vmrun_context_proof:
    ud2

# --- Poisoned trampoline ---
    .globl svm_vmrun_trampoline_poisoned
svm_vmrun_trampoline_poisoned:
    ud2

# --- Resident stubs ---
    .globl svm_resident_enter
    .globl svm_resident_vmmcall_stop

svm_resident_enter:
    xor     eax, eax
    ret

svm_resident_vmmcall_stop:
    xor     eax, eax
    ret
