#ifndef YGHV_NPT_H
#define YGHV_NPT_H
#include "svm_defs.h"
typedef union { uint64_t all; struct { uint64_t present:1; uint64_t writable:1; uint64_t user:1; uint64_t pwt:1; uint64_t pcd:1; uint64_t accessed:1; uint64_t dirty:1; uint64_t large_page:1; uint64_t global:1; uint64_t _avail_9_11:3; uint64_t pfn:40; uint64_t _rsvd_52_58:7; uint64_t _avail_59_61:3; uint64_t _ignored:1; uint64_t nx:1; }; } npt_entry_t;
_Static_assert(sizeof(npt_entry_t) == 8, "NPT size");
#define NPT_ENTRIES_PER_TABLE 512
#define NPT_PML4_INDEX(a) (((a)>>39)&0x1FF)
#define NPT_PDPT_INDEX(a) (((a)>>30)&0x1FF)
#define NPT_PD_INDEX(a) (((a)>>21)&0x1FF)
#define NPT_PT_INDEX(a) (((a)>>12)&0x1FF)
#define NPT_PAGE_OFFSET(a) ((a)&0xFFF)
#define NPT_LARGE_PAGE_FLAGS ((1ULL<<0)|(1ULL<<1)|(1ULL<<2)|(1ULL<<5)|(1ULL<<7))
#define NPT_4K_PAGE_FLAGS ((1ULL<<0)|(1ULL<<1)|(1ULL<<2)|(1ULL<<5))
typedef struct { uint64_t pml4_pa; npt_entry_t *pml4_va; uint64_t total_mapped_pages; uint64_t total_mapped_2mb_pages; } npt_mgr_t;
int npt_init(npt_mgr_t*m,uint64_t x); int npt_identity_map_range(npt_mgr_t*m,uint64_t s,uint64_t e);
int npt_set_page_perm(npt_mgr_t*m,uint64_t g,uint64_t f); int npt_set_page_perm_range(npt_mgr_t*m,uint64_t g,uint64_t s,uint64_t f);
uint64_t npt_translate(npt_mgr_t*m,uint64_t g); int npt_cleanup(npt_mgr_t*m);
#endif
