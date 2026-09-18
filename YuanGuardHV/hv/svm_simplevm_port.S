# svm_simplevm_port.S — faithful port of SimpleSvm SvLaunchVm (x64.asm) for step205.
# Only compiled-in meaningfully when YGHV_BAREMETAL_STEP==205 (the C caller is gated).
# Mirrors SimpleSvm control flow exactly: enter host loop on the VpData-embedded
# host stack; guest resumes at the RIP captured before VMRUN and, on re-execution,
# sees "hypervisor installed" and returns normally (the per-CPU virtualize routine
# unwinds, letting the affinity loop continue to the next CPU). The loop below
# only runs when THIS cpu is the one that just called yghv_sv_launch; it services
# synchronous VMEXITs (CPUID/VMRUN/MSR) via yghv_sv_handle_vmexit and re-VMRUNs.
#
# HostStackLayout offsets from HostRsp (== &vpd->hs.guest_vmcb_pa):
#   [rsp+0x00] guest_vmcb_pa
#   [rsp+0x08] host_vmcb_pa
#   [rsp+0x10] self (vpd)
#   [rsp+0x18] shared
# KTRAP_FRAME_SIZE = 0x190 (matches Windows x64 KTRAP_FRAME).
    .intel_syntax noprefix
    .text

    .set KTRAP_FRAME_SIZE, 0x190

# void yghv_sv_launch(void *HostRsp)  — HostRsp passed in RCX.
    .globl yghv_sv_launch
yghv_sv_launch:
    mov     rsp, rcx                    # switch to host stack (inside VpData)

yghv_sv_lv10:
    mov     rax, qword ptr [rsp]        # guest_vmcb_pa
    .byte   0x0F, 0x01, 0xDA            # VMLOAD rax  (load guest FS/GS/TR/LDTR/KernelGs)
    clgi
    .byte   0x0F, 0x01, 0xD8            # VMRUN rax   (enter guest until #VMEXIT)

    # ---- #VMEXIT: host state (RIP/RSP/CRx/RAX) restored from HSAVE by hardware.
    # rax == guest_vmcb_pa again (host's value at VMRUN time).
    .byte   0x0F, 0x01, 0xDB            # VMSAVE rax  (save guest FS/GS/TR/LDTR/KernelGs)

    sub     rsp, KTRAP_FRAME_SIZE       # room for guest trap frame (unused content)

    # PUSHAQ — save all guest GPRs (they are NOT saved by hardware on #VMEXIT).
    push    rax
    push    rcx
    push    rdx
    push    rbx
    push    -1                          # dummy for rsp
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

    mov     rdx, rsp                                    # GuestRegisters ptr
    # self sits at host_rsp_base + 0x10. Here rsp = base - KTRAP_FRAME_SIZE - 128(PUSHAQ),
    # so self = [rsp + 128 + KTRAP_FRAME_SIZE + 0x10] = [rsp + 0x220].
    mov     rcx, qword ptr [rsp + 0x220]                # rcx = vpd->hs.self

    sub     rsp, 0x80                                   # homing + volatile XMM0-5
    movaps  xmmword ptr [rsp + 0x20], xmm0
    movaps  xmmword ptr [rsp + 0x30], xmm1
    movaps  xmmword ptr [rsp + 0x40], xmm2
    movaps  xmmword ptr [rsp + 0x50], xmm3
    movaps  xmmword ptr [rsp + 0x60], xmm4
    movaps  xmmword ptr [rsp + 0x70], xmm5

    call    yghv_sv_handle_vmexit

    movaps  xmm5, xmmword ptr [rsp + 0x70]
    movaps  xmm4, xmmword ptr [rsp + 0x60]
    movaps  xmm3, xmmword ptr [rsp + 0x50]
    movaps  xmm2, xmmword ptr [rsp + 0x40]
    movaps  xmm1, xmmword ptr [rsp + 0x30]
    movaps  xmm0, xmmword ptr [rsp + 0x20]
    add     rsp, 0x80

    test    al, al
    # POPAQ (do NOT disturb flags before the test — test already read al).
    pop     r15
    pop     r14
    pop     r13
    pop     r12
    pop     r11
    pop     r10
    pop     r9
    pop     r8
    pop     rdi
    pop     rsi
    pop     rbp
    pop     rbx                          # dummy for rsp
    pop     rbx
    pop     rdx
    pop     rcx
    pop     rax

    jnz     yghv_sv_lv20
    add     rsp, KTRAP_FRAME_SIZE
    jmp     yghv_sv_lv10

yghv_sv_lv20:
    # Virtualization ended. RBX=return addr, RCX=guest rsp, EDX:EAX=vpd (set by handler).
    mov     rsp, rcx
    mov     ecx, 0x504D5653             # 'SVMP' (magic: port hypervisor unloaded)
    jmp     rbx
