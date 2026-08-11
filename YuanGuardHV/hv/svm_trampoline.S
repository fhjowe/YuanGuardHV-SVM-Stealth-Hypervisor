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
