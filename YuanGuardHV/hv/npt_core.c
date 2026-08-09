#include <ntddk.h>
#include "npt.h"
#include "debug.h"

#define YGHV_DEBUG_LOG

typedef struct { npt_entry_t *pdpt_va[512]; npt_entry_t *pd_va[512]; } npt_cached_t;
static npt_cached_t *g_cache = NULL;

static npt_entry_t *npt_alloc_table(void) {
    npt_entry_t*t=(npt_entry_t*)MmAllocateContiguousMemory(HV_PAGE_SIZE,(PHYSICAL_ADDRESS){.QuadPart=(ULONGLONG)-1});
    if(t)RtlZeroMemory(t,HV_PAGE_SIZE); return t;
}
static uint64_t npt_va_to_pa(void*va){return MmGetPhysicalAddress(va).QuadPart;}

int npt_init(npt_mgr_t*m,uint64_t mp){RtlZeroMemory(m,sizeof(*m));g_cache=(npt_cached_t*)ExAllocatePoolWithTag(NonPagedPool,sizeof(npt_cached_t),YGHV_TAG);if(!g_cache)return STATUS_INSUFFICIENT_RESOURCES;RtlZeroMemory(g_cache,sizeof(npt_cached_t));m->pml4_va=npt_alloc_table();if(!m->pml4_va){ExFreePool(g_cache);return STATUS_INSUFFICIENT_RESOURCES;}m->pml4_pa=npt_va_to_pa(m->pml4_va);return npt_identity_map_range(m,0,mp);}

int npt_identity_map_range(npt_mgr_t*m,uint64_t s,uint64_t e){uint64_t pa=s&~(HV_LARGE_PAGE_SIZE-1);uint64_t ae=(e+HV_LARGE_PAGE_SIZE-1)&~(HV_LARGE_PAGE_SIZE-1);for(;pa<ae;pa+=HV_LARGE_PAGE_SIZE){uint32_t p4=(uint32_t)NPT_PML4_INDEX(pa),p2=(uint32_t)NPT_PDPT_INDEX(pa),p1=(uint32_t)NPT_PD_INDEX(pa);if(!m->pml4_va[p4].present){npt_entry_t*d=npt_alloc_table();if(!d)return STATUS_INSUFFICIENT_RESOURCES;m->pml4_va[p4].all=npt_va_to_pa(d)|NPT_4K_PAGE_FLAGS;g_cache->pdpt_va[p4]=d;}npt_entry_t*d=g_cache->pdpt_va[p4];if(!d[p2].present){npt_entry_t*e2=npt_alloc_table();if(!e2)return STATUS_INSUFFICIENT_RESOURCES;d[p2].all=npt_va_to_pa(e2)|NPT_4K_PAGE_FLAGS;if(p4==0&&p2<512)g_cache->pd_va[p2]=e2;}npt_entry_t*e2=(p4==0&&p2<512)?g_cache->pd_va[p2]:NULL;if(!e2){e2=(npt_entry_t*)MmGetVirtualForPhysical((PHYSICAL_ADDRESS){.QuadPart=d[p2].pfn<<12});if(!e2)continue;}e2[p1].all=pa|NPT_LARGE_PAGE_FLAGS;m->total_mapped_2mb_pages++;}m->total_mapped_pages=m->total_mapped_2mb_pages*(HV_LARGE_PAGE_SIZE/HV_PAGE_SIZE);return STATUS_SUCCESS;}

int npt_set_page_perm(npt_mgr_t*m,uint64_t g,uint64_t f){(void)m;(void)g;(void)f;return STATUS_SUCCESS;}
int npt_set_page_perm_range(npt_mgr_t*m,uint64_t g,uint64_t s,uint64_t f){(void)m;(void)g;(void)s;(void)f;return STATUS_SUCCESS;}
uint64_t npt_translate(npt_mgr_t*m,uint64_t g){(void)m;(void)g;return 0;}
int npt_cleanup(npt_mgr_t*m){uint32_t i;if(g_cache){for(i=0;i<512;i++){if(g_cache->pd_va[i])MmFreeContiguousMemory(g_cache->pd_va[i]);if(g_cache->pdpt_va[i])MmFreeContiguousMemory(g_cache->pdpt_va[i]);}ExFreePool(g_cache);g_cache=NULL;}if(m->pml4_va)MmFreeContiguousMemory(m->pml4_va);RtlZeroMemory(m,sizeof(*m));return STATUS_SUCCESS;}
