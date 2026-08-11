#ifndef YGHV_SVM_DEFS_H
#define YGHV_SVM_DEFS_H

/* Minimal types for cross-compiler (MSVC cl.exe vs clang-cl) */
#ifndef _STDINT
typedef unsigned char      uint8_t;
typedef unsigned short     uint16_t;
typedef unsigned int       uint32_t;
typedef unsigned long long uint64_t;
typedef signed int         int32_t;
#endif

/* MSVC compatibility: _Static_assert → static_assert */
#ifdef _MSC_VER
#include <stddef.h>
#define _Static_assert(expr, msg) static_assert(expr, msg)
#define ATTR_ALIGNED(n) __declspec(align(n))
#else
#define ATTR_ALIGNED(n) __attribute__((aligned(n)))
#endif

/* === AMD SVM MSR addresses === */
#define MSR_EFER          0xC0000080
#define MSR_IA32_PAT      0x277
#define MSR_VM_CR         0xC0010114
#define MSR_VM_HSAVE      0xC0010117

/* EFER bits */
#define EFER_SVME          (1ULL << 12)

/* VM_CR bits */
#define VM_CR_SVMDIS       (1ULL << 4)

/* CPUID leaves for SVM detection */
#define CPUID_AMD_EXTENDED 0x80000000
#define CPUID_AMD_SVM      0x80000001
#define CPUID_AMD_NPT      0x8000000A

/* SVM CPUID feature bits (CPUID 0x80000001.ECX) */
#define CPUID_SVM_FEATURE_SVM  (1 << 2)

/* SVM CPUID feature bits (CPUID 0x8000000A.EDX) */
#define CPUID_NPT_FEATURE_NPT  (1 << 0)
#define CPUID_NPT_FEATURE_FLUSHBYASID (1 << 6)

/* VMCB control area intercept bit offsets */
#define SVM_INTERCEPT_INTR       0
#define SVM_INTERCEPT_NMI        1
#define SVM_INTERCEPT_SMI        2
#define SVM_INTERCEPT_INIT       3
#define SVM_INTERCEPT_VINTR      4
#define SVM_INTERCEPT_CR0_SEL_WRITE 5
#define SVM_INTERCEPT_IDTR_READ  6
#define SVM_INTERCEPT_GDTR_READ  7
#define SVM_INTERCEPT_LDTR_READ  8
#define SVM_INTERCEPT_TR_READ    9
#define SVM_INTERCEPT_IDTR_WRITE 10
#define SVM_INTERCEPT_GDTR_WRITE 11
#define SVM_INTERCEPT_LDTR_WRITE 12
#define SVM_INTERCEPT_TR_WRITE   13
#define SVM_INTERCEPT_RDTSC      14
#define SVM_INTERCEPT_RDPMC      15
#define SVM_INTERCEPT_PUSHF      16
#define SVM_INTERCEPT_POPF       17
#define SVM_INTERCEPT_CPUID      18
#define SVM_INTERCEPT_RSM        19
#define SVM_INTERCEPT_IRET       20
#define SVM_INTERCEPT_SWINT      21
#define SVM_INTERCEPT_INVD       22
#define SVM_INTERCEPT_PAUSE      23
#define SVM_INTERCEPT_HLT        24
#define SVM_INTERCEPT_INVLPG     25
#define SVM_INTERCEPT_INVLPGA    26
#define SVM_INTERCEPT_IOIO_PROT  27
#define SVM_INTERCEPT_MSR_PROT   28
#define SVM_INTERCEPT_TASKSR     29
#define SVM_INTERCEPT_FERR_FREEZE 30
#define SVM_INTERCEPT_SHUTDOWN   31
#define SVM_INTERCEPT_VMRUN      32
#define SVM_INTERCEPT_VMMCALL    33
#define SVM_INTERCEPT_VMLOAD     34
#define SVM_INTERCEPT_VMSAVE     35
#define SVM_INTERCEPT_STGI       36
#define SVM_INTERCEPT_CLGI       37
#define SVM_INTERCEPT_SKINIT     38
#define SVM_INTERCEPT_RDTSCP     39
#define SVM_INTERCEPT_ICEBP      40
#define SVM_INTERCEPT_WBINVD     41
#define SVM_INTERCEPT_MONITOR    42
#define SVM_INTERCEPT_MWAIT      43
#define SVM_INTERCEPT_MWAIT_COND 44
#define SVM_INTERCEPT_XSETBV     45
#define SVM_INTERCEPT_EFER_WRITE_TRAP 46

#define INTERCEPT_INTR       (1ULL << SVM_INTERCEPT_INTR)
#define INTERCEPT_NMI        (1ULL << SVM_INTERCEPT_NMI)
#define INTERCEPT_CPUID      (1ULL << SVM_INTERCEPT_CPUID)
#define INTERCEPT_MSR_PROT   (1ULL << SVM_INTERCEPT_MSR_PROT)
#define INTERCEPT_VMRUN      (1ULL << SVM_INTERCEPT_VMRUN)
#define INTERCEPT_VMMCALL    (1ULL << SVM_INTERCEPT_VMMCALL)
#define INTERCEPT_HLT        (1ULL << SVM_INTERCEPT_HLT)
#define INTERCEPT_RDTSC      (1ULL << SVM_INTERCEPT_RDTSC)
#define INTERCEPT_RDTSCP     (1ULL << SVM_INTERCEPT_RDTSCP)

/* General intercept helper — absolute bit to correct u32 field mask.
   general1 (VMCB+0x0C u32): bits 0-31; general2 (VMCB+0x10 u32): bits 32+ */
#define INTR_GEN1(b)   (1U << (b))
#define INTR_GEN2(b)   (1U << ((b) - 32))

/* CR intercept mask helpers */
#define SVM_CR_INTERCEPT_READ_SHIFT(cr)  (cr)
#define SVM_CR_INTERCEPT_WRITE_SHIFT(cr) (16 + (cr))
#define SVM_CR_INTERCEPT_MASK(cr) \
    ((1ULL << SVM_CR_INTERCEPT_READ_SHIFT(cr)) | (1ULL << SVM_CR_INTERCEPT_WRITE_SHIFT(cr)))

/* SVM VMCB clean bits */
#define VMCB_CLEAN_INTERCEPTS  (1 << 0)
#define VMCB_CLEAN_IOPM        (1 << 1)
#define VMCB_CLEAN_ASID        (1 << 2)
#define VMCB_CLEAN_TPR         (1 << 3)
#define VMCB_CLEAN_NPT         (1 << 4)
#define VMCB_CLEAN_CRx         (1 << 5)
#define VMCB_CLEAN_DRx         (1 << 6)
#define VMCB_CLEAN_DT          (1 << 7)
#define VMCB_CLEAN_SEG         (1 << 8)
#define VMCB_CLEAN_CR2         (1 << 9)
#define VMCB_CLEAN_LBR         (1 << 10)
#define VMCB_CLEAN_AVIC        (1 << 11)

/* SVM Exit codes */
#define SVM_EXIT_CR0_READ         0x00
#define SVM_EXIT_CR1_READ         0x01
#define SVM_EXIT_CR2_READ         0x02
#define SVM_EXIT_CR3_READ         0x03
#define SVM_EXIT_CR4_READ         0x04
#define SVM_EXIT_CR5_READ         0x05
#define SVM_EXIT_CR6_READ         0x06
#define SVM_EXIT_CR7_READ         0x07
#define SVM_EXIT_CR8_READ         0x08
#define SVM_EXIT_CR9_READ         0x09
#define SVM_EXIT_CR10_READ        0x0A
#define SVM_EXIT_CR11_READ        0x0B
#define SVM_EXIT_CR12_READ        0x0C
#define SVM_EXIT_CR13_READ        0x0D
#define SVM_EXIT_CR14_READ        0x0E
#define SVM_EXIT_CR15_READ        0x0F
#define SVM_EXIT_CR0_WRITE        0x10
#define SVM_EXIT_CR1_WRITE        0x11
#define SVM_EXIT_CR2_WRITE        0x12
#define SVM_EXIT_CR3_WRITE        0x13
#define SVM_EXIT_CR4_WRITE        0x14
#define SVM_EXIT_CR5_WRITE        0x15
#define SVM_EXIT_CR6_WRITE        0x16
#define SVM_EXIT_CR7_WRITE        0x17
#define SVM_EXIT_CR8_WRITE        0x18
#define SVM_EXIT_CR9_WRITE        0x19
#define SVM_EXIT_CR10_WRITE       0x1A
#define SVM_EXIT_CR11_WRITE       0x1B
#define SVM_EXIT_CR12_WRITE       0x1C
#define SVM_EXIT_CR13_WRITE       0x1D
#define SVM_EXIT_CR14_WRITE       0x1E
#define SVM_EXIT_CR15_WRITE       0x1F
#define SVM_EXIT_EXCEPTION_BASE   0x40
#define SVM_EXIT_EXCEPTION_UD     0x46
#define SVM_EXIT_INTR             0x60
#define SVM_EXIT_NMI              0x61
#define SVM_EXIT_SMI              0x62
#define SVM_EXIT_INIT             0x63
#define SVM_EXIT_VINTR            0x64
#define SVM_EXIT_CR0_SEL_WRITE    0x65
#define SVM_EXIT_IDTR_READ        0x66
#define SVM_EXIT_GDTR_READ        0x67
#define SVM_EXIT_LDTR_READ        0x68
#define SVM_EXIT_TR_READ          0x69
#define SVM_EXIT_IDTR_WRITE       0x6A
#define SVM_EXIT_GDTR_WRITE       0x6B
#define SVM_EXIT_LDTR_WRITE       0x6C
#define SVM_EXIT_TR_WRITE         0x6D
#define SVM_EXIT_RDTSC            0x6E
#define SVM_EXIT_RDPMC            0x6F
#define SVM_EXIT_PUSHF            0x70
#define SVM_EXIT_POPF             0x71
#define SVM_EXIT_CPUID            0x72
#define SVM_EXIT_RSM              0x73
#define SVM_EXIT_IRET             0x74
#define SVM_EXIT_SWINT            0x75
#define SVM_EXIT_INVD             0x76
#define SVM_EXIT_PAUSE            0x77
#define SVM_EXIT_HLT              0x78
#define SVM_EXIT_INVLPG           0x79
#define SVM_EXIT_INVLPGA          0x7A
#define SVM_EXIT_IOIO             0x7B
#define SVM_EXIT_MSR              0x7C
#define SVM_EXIT_TASKSR           0x7D
#define SVM_EXIT_FERR_FREEZE      0x7E
#define SVM_EXIT_SHUTDOWN         0x7F
#define SVM_EXIT_VMRUN            0x80
#define SVM_EXIT_VMMCALL          0x81
#define SVM_EXIT_VMLOAD           0x82
#define SVM_EXIT_VMSAVE           0x83
#define SVM_EXIT_STGI             0x84
#define SVM_EXIT_CLGI             0x85
#define SVM_EXIT_SKINIT           0x86
#define SVM_EXIT_RDTSCP           0x87
#define SVM_EXIT_ICEBP            0x88
#define SVM_EXIT_WBINVD           0x89
#define SVM_EXIT_MONITOR          0x8A
#define SVM_EXIT_MWAIT            0x8B
#define SVM_EXIT_MWAIT_COND       0x8C
#define SVM_EXIT_XSETBV           0x8D
#define SVM_EXIT_NPF              0x400

/* Legacy VMEXIT_* aliases for compatibility with existing dispatch */
#define VMEXIT_CPUID              SVM_EXIT_CPUID
#define VMEXIT_MSR                SVM_EXIT_MSR
#define VMEXIT_VMMCALL            SVM_EXIT_VMMCALL
#define VMEXIT_INTR               SVM_EXIT_INTR
#define VMEXIT_NMI                SVM_EXIT_NMI
#define VMEXIT_NPF                SVM_EXIT_NPF
#define VMEXIT_EXCEPTION_UD       SVM_EXIT_EXCEPTION_UD
#define VMEXIT_CR0_READ           SVM_EXIT_CR0_READ
#define VMEXIT_CR3_READ           SVM_EXIT_CR3_READ
#define VMEXIT_CR4_READ           SVM_EXIT_CR4_READ
#define VMEXIT_CR0_WRITE          SVM_EXIT_CR0_WRITE
#define VMEXIT_CR3_WRITE          SVM_EXIT_CR3_WRITE
#define VMEXIT_CR4_WRITE          SVM_EXIT_CR4_WRITE
#define VMEXIT_CR8_WRITE          SVM_EXIT_CR8_WRITE

/* NPF exitinfo1 bits */
#define NPF_INFO1_PRESENT   (1ULL << 0)
#define NPF_INFO1_WRITE     (1ULL << 1)
#define NPF_INFO1_USER      (1ULL << 2)
#define NPF_INFO1_RSVD      (1ULL << 3)
#define NPF_INFO1_EXEC      (1ULL << 4)
#define NPF_INFO1_GPA(v)    ((v)->exitinfo2)

/* Page size constants */
#define HV_PAGE_SIZE       0x1000
#define HV_LARGE_PAGE_SIZE 0x200000

#define SVM_MAX_CORES      16

#endif
