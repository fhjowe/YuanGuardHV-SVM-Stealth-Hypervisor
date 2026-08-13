#ifndef YGHV_VMCB_H
#define YGHV_VMCB_H

#include <stddef.h>
#include "svm_defs.h"

/* VMCB control area — AMD APM Vol.2 Table B-1, 0x000-0x3FF */
typedef struct {
    /* +0x00: CR intercepts — u16 read (bits 0-15) + u16 write (bits 16-31) */
    uint16_t cr_read_intercepts;     /* +0x00 CR0-CR15 read */
    uint16_t cr_write_intercepts;    /* +0x02 CR0-CR15 write */
    /* +0x04: DR intercepts — u16 read + u16 write */
    uint16_t dr_read_intercepts;     /* +0x04 DR0-DR15 read */
    uint16_t dr_write_intercepts;    /* +0x06 DR0-DR15 write */
    uint32_t exception_intercepts;   /* +0x08: exception vectors 0-31 */
    uint32_t general1_intercepts;    /* +0x0C: INTR(0)..SHUTDOWN(31) */
    uint32_t general2_intercepts;    /* +0x10: VMRUN(0)..XSETBV(13),EFER(14) */
    uint8_t  _pad_014[0x2C];         /* +0x14..+0x3F */

    uint64_t iopm_base_pa;           /* +0x40 */
    uint64_t msrpm_base_pa;          /* +0x48 */
    uint64_t tsc_offset;             /* +0x50 */
    uint32_t guest_asid;             /* +0x58 */
    uint8_t  tlb_control;            /* +0x5C */
    uint8_t  _pad_05D[3];

    uint64_t vintr;                  /* +0x60 */
    uint64_t interrupt_shadow;       /* +0x68 */
    uint64_t exitcode;               /* +0x70 */
    uint64_t exitinfo1;              /* +0x78 */
    uint64_t exitinfo2;              /* +0x80 */
    uint64_t exitintinfo;            /* +0x88 */
    uint64_t np_enable;              /* +0x90: NP_ENABLE bit 0 */
    uint64_t avic_apic_bar;          /* +0x98 */
    uint64_t ghcb_gpa;               /* +0xA0 */

    uint64_t event_injection;        /* +0xA8 */
    uint64_t ncr3;                   /* +0xB0 */
    uint64_t lbr_virtualization;     /* +0xB8 */
    uint32_t vmcb_clean_bits;        /* +0xC0 */
    uint8_t  _pad_0C4[4];

    uint64_t next_rip;               /* +0xC8: nRIP */
    uint8_t  byte_fetched;           /* +0xD0: number of bytes fetched */
    uint8_t  instruction_bytes[15];  /* +0xD1: fetched instruction bytes */
    uint64_t avic_backing_page;      /* +0xE0 */
    uint64_t _reserved_0E8;          /* +0xE8 */
    uint64_t avic_logical_id;        /* +0xF0 */
    uint64_t avic_physical_id;       /* +0xF8 */
    uint8_t  _pad_100[0x300];        /* +0x100..+0x3FF */
} vmcb_control_t;

_Static_assert(sizeof(vmcb_control_t) == 0x400, "VMCB control area size");
_Static_assert(offsetof(vmcb_control_t, exitcode) == 0x70, "VMCB exitcode offset");
_Static_assert(offsetof(vmcb_control_t, exitinfo1) == 0x78, "VMCB exitinfo1 offset");
_Static_assert(offsetof(vmcb_control_t, exitinfo2) == 0x80, "VMCB exitinfo2 offset");
_Static_assert(offsetof(vmcb_control_t, avic_apic_bar) == 0x98, "VMCB AVIC APIC BAR offset");
_Static_assert(offsetof(vmcb_control_t, event_injection) == 0xA8, "VMCB event injection offset");
_Static_assert(offsetof(vmcb_control_t, np_enable) == 0x90, "VMCB np_enable offset");
_Static_assert(offsetof(vmcb_control_t, avic_backing_page) == 0xE0, "VMCB AVIC backing page offset");
_Static_assert(offsetof(vmcb_control_t, avic_logical_id) == 0xF0, "VMCB AVIC logical ID offset");
_Static_assert(offsetof(vmcb_control_t, avic_physical_id) == 0xF8, "VMCB AVIC physical ID offset");
_Static_assert(offsetof(vmcb_control_t, next_rip) == 0xC8, "VMCB nRIP offset");
_Static_assert(offsetof(vmcb_control_t, vmcb_clean_bits) == 0xC0, "VMCB clean bits offset");
_Static_assert(offsetof(vmcb_control_t, guest_asid) == 0x58, "VMCB ASID offset");
_Static_assert(offsetof(vmcb_control_t, tlb_control) == 0x5C, "VMCB TLB control offset");

/* VMCB state save area — AMD APM Vol.2 Table B-3
   Located at VMCB+0x400, size 0xC00 bytes */
typedef struct {
    uint16_t es_selector;     /* +0x000 */
    uint16_t es_attrib;
    uint32_t es_limit;
    uint64_t es_base;
    uint16_t cs_selector;     /* +0x010 */
    uint16_t cs_attrib;
    uint32_t cs_limit;
    uint64_t cs_base;
    uint16_t ss_selector;     /* +0x020 */
    uint16_t ss_attrib;
    uint32_t ss_limit;
    uint64_t ss_base;
    uint16_t ds_selector;     /* +0x030 */
    uint16_t ds_attrib;
    uint32_t ds_limit;
    uint64_t ds_base;
    uint16_t fs_selector;     /* +0x040 */
    uint16_t fs_attrib;
    uint32_t fs_limit;
    uint64_t fs_base;
    uint16_t gs_selector;     /* +0x050 */
    uint16_t gs_attrib;
    uint32_t gs_limit;
    uint64_t gs_base;
    uint16_t gdtr_selector;   /* +0x060 */
    uint16_t gdtr_attrib;
    uint32_t gdtr_limit;
    uint64_t gdtr_base;
    uint16_t ldtr_selector;   /* +0x070 */
    uint16_t ldtr_attrib;
    uint32_t ldtr_limit;
    uint64_t ldtr_base;
    uint16_t idtr_selector;   /* +0x080 */
    uint16_t idtr_attrib;
    uint32_t idtr_limit;
    uint64_t idtr_base;
    uint16_t tr_selector;     /* +0x090 */
    uint16_t tr_attrib;
    uint32_t tr_limit;
    uint64_t tr_base;
    uint8_t  _pad_a0[0x2B];
    uint8_t  cpl;             /* +0x0CB */
    uint8_t  _pad_cc[4];
    uint64_t efer;            /* +0x0D0 */
    uint8_t  _pad_d8[0x70];
    uint64_t cr4;             /* +0x148 */
    uint64_t cr3;             /* +0x150 */
    uint64_t cr0;             /* +0x158 */
    uint64_t dr7;             /* +0x160 */
    uint64_t dr6;             /* +0x168 */
    uint64_t rflags;          /* +0x170 */
    uint64_t rip;             /* +0x178 */
    uint8_t  _pad_180[0x58];
    uint64_t rsp;             /* +0x1D8 */
    uint8_t  _pad_1e0[0x18];
    uint64_t rax;             /* +0x1F8 */
    uint64_t star;            /* +0x200 */
    uint64_t lstar;           /* +0x208 */
    uint64_t cstar;           /* +0x210 */
    uint64_t sfmask;          /* +0x218 */
    uint64_t kernel_gs_base;  /* +0x220 */
    uint64_t sysenter_cs;     /* +0x228 */
    uint64_t sysenter_esp;    /* +0x230 */
    uint64_t sysenter_eip;    /* +0x238 */
    uint64_t cr2;             /* +0x240 */
    uint8_t  _pad_248[0x20];
    uint64_t g_pat;           /* +0x268 */
    uint8_t  _pad_270[0x190];
    uint8_t  _pad_400;        /* +0x400: reserved area, not a GIF field */
    uint8_t  _pad_401[0x7FF]; /* pad to 0xC00 total */
} vmcb_state_t;

_Static_assert(sizeof(vmcb_state_t) == 0xC00, "VMCB state save area size");
_Static_assert(offsetof(vmcb_state_t, rflags) == 0x170, "VMCB RFLAGS offset");
_Static_assert(offsetof(vmcb_state_t, rip) == 0x178, "VMCB RIP offset");
_Static_assert(offsetof(vmcb_state_t, rsp) == 0x1D8, "VMCB RSP offset");
_Static_assert(offsetof(vmcb_state_t, rax) == 0x1F8, "VMCB RAX offset");
_Static_assert(offsetof(vmcb_state_t, g_pat) == 0x268, "VMCB G_PAT offset");

/* Full VMCB (4KB page): control at +0x000, state at +0x400 */
typedef struct {
    vmcb_control_t control;   /* +0x000 */
    vmcb_state_t   state;     /* +0x400 */
} ATTR_ALIGNED(0x1000) vmcb_t;

_Static_assert(sizeof(vmcb_t) == 0x1000, "VMCB page layout");

/* VMCB field offsets from VMCB base (used in assembly trampoline) */
#define SVM_VMCB_RFLAGS_OFFSET  (0x400 + offsetof(vmcb_state_t, rflags))
#define SVM_VMCB_RIP_OFFSET     (0x400 + offsetof(vmcb_state_t, rip))
#define SVM_VMCB_RSP_OFFSET     (0x400 + offsetof(vmcb_state_t, rsp))
#define SVM_VMCB_RAX_OFFSET     (0x400 + offsetof(vmcb_state_t, rax))

/* NPT enable bit */
#define SVM_NP_ENABLE  (1ULL << 0)

#include "control_plane.h"

#define VMMCALL_CMD_HEARTBEAT  YGHV_CMD_HEARTBEAT
#define VMMCALL_CMD_STOP       YGHV_CMD_STOP_INTERNAL
#define VMMCALL_CMD_VERSION    YGHV_CMD_VERSION
#define VMMCALL_CMD_STATS      YGHV_CMD_STATS
#define VMMCALL_STATUS_OK      YGHV_STATUS_OK

#endif
