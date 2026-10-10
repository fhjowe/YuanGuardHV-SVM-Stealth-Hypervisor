#include <ntddk.h>
#include "npt.h"
#include "debug.h"
/* YGHV_DEBUG_LOG is provided by build.bat; do not redefine locally. */

typedef struct { npt_entry_t *pdpt_va[512]; npt_entry_t *pd_va[512]; } npt_cached_t;
static npt_cached_t *g_cache = NULL;

static npt_entry_t *npt_alloc_table(void) {
    npt_entry_t*t=(npt_entry_t*)MmAllocateContiguousMemory(HV_PAGE_SIZE,(PHYSICAL_ADDRESS){.QuadPart=0xFFFFFFFF});
    if(t)RtlZeroMemory(t,HV_PAGE_SIZE); return t;
}
static uint64_t npt_va_to_pa(void*va){return MmGetPhysicalAddress(va).QuadPart;}

static npt_entry_t *npt_get_pdpt(npt_mgr_t *m, uint32_t p4) {
    if (!m || !m->pml4_va || !m->pml4_va[p4].present) return NULL;
    return (npt_entry_t*)MmGetVirtualForPhysical(
        (PHYSICAL_ADDRESS){ .QuadPart = (uint64_t)m->pml4_va[p4].pfn << 12 });
}

static npt_entry_t *npt_get_pd(npt_mgr_t *m, uint32_t p4, uint32_t p2) {
    npt_entry_t *pdpt = npt_get_pdpt(m, p4);
    if (!pdpt || !pdpt[p2].present) return NULL;
    return (npt_entry_t*)MmGetVirtualForPhysical(
        (PHYSICAL_ADDRESS){ .QuadPart = (uint64_t)pdpt[p2].pfn << 12 });
}

static npt_entry_t *npt_get_pt(npt_mgr_t *m, uint32_t p4, uint32_t p2, uint32_t p1) {
    npt_entry_t *pd = npt_get_pd(m, p4, p2);
    if (!pd || !pd[p1].present || pd[p1].large_page) return NULL;
    return (npt_entry_t*)MmGetVirtualForPhysical(
        (PHYSICAL_ADDRESS){ .QuadPart = (uint64_t)pd[p1].pfn << 12 });
}

int npt_init(npt_mgr_t*m,uint64_t mp){
    RtlZeroMemory(m,sizeof(*m));
    g_cache=(npt_cached_t*)ExAllocatePoolWithTag(NonPagedPool,sizeof(npt_cached_t),YGHV_TAG);
    if(!g_cache)return STATUS_INSUFFICIENT_RESOURCES;
    RtlZeroMemory(g_cache,sizeof(npt_cached_t));
    m->pml4_va=npt_alloc_table();
    if(!m->pml4_va){ExFreePool(g_cache);g_cache=NULL;return STATUS_INSUFFICIENT_RESOURCES;}
    m->pml4_pa=npt_va_to_pa(m->pml4_va);
    if(mp)return npt_identity_map_range(m,0,mp);
    return STATUS_SUCCESS;
}

int npt_identity_map_range(npt_mgr_t*m,uint64_t s,uint64_t e){
    uint64_t pa=s&~(HV_LARGE_PAGE_SIZE-1);
    uint64_t ae=(e+HV_LARGE_PAGE_SIZE-1)&~(HV_LARGE_PAGE_SIZE-1);
    for(;pa<ae;pa+=HV_LARGE_PAGE_SIZE){
        uint32_t p4=(uint32_t)NPT_PML4_INDEX(pa),p2=(uint32_t)NPT_PDPT_INDEX(pa),p1=(uint32_t)NPT_PD_INDEX(pa);
        if(!m->pml4_va[p4].present){
            npt_entry_t*d=npt_alloc_table();
            if(!d)return STATUS_INSUFFICIENT_RESOURCES;
            m->pml4_va[p4].all=npt_va_to_pa(d)|NPT_4K_PAGE_FLAGS;
            g_cache->pdpt_va[p4]=d;
        }
        npt_entry_t*d=g_cache->pdpt_va[p4];
        if(!d[p2].present){
            npt_entry_t*e2=npt_alloc_table();
            if(!e2)return STATUS_INSUFFICIENT_RESOURCES;
            d[p2].all=npt_va_to_pa(e2)|NPT_4K_PAGE_FLAGS;
            if(p4==0&&p2<512)g_cache->pd_va[p2]=e2;
        }
        npt_entry_t*e2=(p4==0&&p2<512)?g_cache->pd_va[p2]:NULL;
        if(!e2){
            e2=(npt_entry_t*)MmGetVirtualForPhysical((PHYSICAL_ADDRESS){.QuadPart=d[p2].pfn<<12});
            if(!e2)continue;
        }
        e2[p1].all=pa|NPT_LARGE_PAGE_FLAGS;
        m->total_mapped_2mb_pages++;
    }
    m->total_mapped_pages=m->total_mapped_2mb_pages*(HV_LARGE_PAGE_SIZE/HV_PAGE_SIZE);
    return STATUS_SUCCESS;
}

int npt_split_2mb_to_4kb(npt_mgr_t*m,uint64_t g){
    npt_entry_t *pd, *pt;
    uint32_t p4=(uint32_t)NPT_PML4_INDEX(g),p2=(uint32_t)NPT_PDPT_INDEX(g),p1=(uint32_t)NPT_PD_INDEX(g);
    uint64_t base_pa, common;
    uint32_t i;

    if(!m||!m->pml4_va)return STATUS_INVALID_PARAMETER;
    pd=npt_get_pd(m,p4,p2);
    if(!pd)return STATUS_NOT_FOUND;
    if(!pd[p1].present)return STATUS_NOT_FOUND;
    if(!pd[p1].large_page)return STATUS_SUCCESS;

    pt=npt_alloc_table();
    if(!pt)return STATUS_INSUFFICIENT_RESOURCES;

    base_pa=NPT_PFN_2MB(pd[p1].all)<<21;
    common=pd[p1].all & ((1ULL<<0)|(1ULL<<1)|(1ULL<<2)|(1ULL<<3)|
                          (1ULL<<4)|(1ULL<<5)|(1ULL<<6)|(1ULL<<8)|(1ULL<<63));
    for(i=0;i<512;i++){
        pt[i].all=(base_pa+((uint64_t)i<<12))|common;
    }
    pd[p1].all=npt_va_to_pa(pt)|NPT_4K_PAGE_FLAGS;
    return STATUS_SUCCESS;
}

int npt_map_page(npt_mgr_t*m,uint64_t g,uint64_t spa,uint64_t flags){
    npt_entry_t *pd, *pt;
    uint32_t p4=(uint32_t)NPT_PML4_INDEX(g),p2=(uint32_t)NPT_PDPT_INDEX(g),p1=(uint32_t)NPT_PD_INDEX(g),p0=(uint32_t)NPT_PT_INDEX(g);
    int st;

    if(!m||!m->pml4_va)return STATUS_INVALID_PARAMETER;
    st=npt_split_2mb_to_4kb(m,g);
    if(!NT_SUCCESS(st))return st;
    pd=npt_get_pd(m,p4,p2);
    if(!pd||!pd[p1].present)return STATUS_NOT_FOUND;
    pt=npt_get_pt(m,p4,p2,p1);
    if(!pt)return STATUS_NOT_FOUND;
    pt[p0].all=(spa&~0xFFFULL)|(flags&0xFFFULL)|(flags&(1ULL<<63));
    return STATUS_SUCCESS;
}

int npt_set_page_perm(npt_mgr_t*m,uint64_t g,uint64_t f){
    npt_entry_t *pd, *pt;
    uint32_t p4=(uint32_t)NPT_PML4_INDEX(g),p2=(uint32_t)NPT_PDPT_INDEX(g),p1=(uint32_t)NPT_PD_INDEX(g),p0=(uint32_t)NPT_PT_INDEX(g);
    npt_entry_t *e;

    if(!m||!m->pml4_va)return STATUS_INVALID_PARAMETER;
    pd=npt_get_pd(m,p4,p2);
    if(!pd)return STATUS_NOT_FOUND;
    if(pd[p1].large_page){
        e=&pd[p1];
    }else if(pd[p1].present){
        pt=npt_get_pt(m,p4,p2,p1);
        if(!pt)return STATUS_NOT_FOUND;
        e=&pt[p0];
    }else{
        return STATUS_NOT_FOUND;
    }

    if(f&NPT_PERM_PRESENT)e->all|=1;else e->all&=~1ULL;
    if(f&NPT_PERM_WRITABLE)e->all|=(1ULL<<1);else e->all&=~(1ULL<<1);
    if(f&NPT_PERM_NX)e->all|=(1ULL<<63);else e->all&=~(1ULL<<63);
    return STATUS_SUCCESS;
}

int npt_set_page_perm_range(npt_mgr_t*m,uint64_t g,uint64_t s,uint64_t f){
    uint64_t start=g&~(HV_PAGE_SIZE-1);
    uint64_t end=g+s;
    uint64_t pa;
    /* Iterate per 4K page so a split (4K) region is fully covered and a large
       page is not over-applied beyond the requested range. */
    for(pa=start;pa<end;pa+=HV_PAGE_SIZE){
        int st=npt_split_2mb_to_4kb(m,pa);
        if(st)return st;
        st=npt_set_page_perm(m,pa,f);
        if(st)return st;
    }
    return STATUS_SUCCESS;
}

uint64_t npt_translate(npt_mgr_t*m,uint64_t g){
    npt_entry_t *pd, *pt;
    uint32_t p4=(uint32_t)NPT_PML4_INDEX(g),p2=(uint32_t)NPT_PDPT_INDEX(g),p1=(uint32_t)NPT_PD_INDEX(g),p0=(uint32_t)NPT_PT_INDEX(g);
    if(!m||!m->pml4_va)return 0;
    pd=npt_get_pd(m,p4,p2);
    if(!pd)return 0;
    /* Present must be checked before the large-page branch: a non-present
       large page must not return a translation. */
    if(!pd[p1].present)return 0;
    if(pd[p1].large_page){
        return (NPT_PFN_2MB(pd[p1].all)<<21)|(g&(HV_LARGE_PAGE_SIZE-1));
    }
    pt=npt_get_pt(m,p4,p2,p1);
    if(!pt||!pt[p0].present)return 0;
    return ((uint64_t)pt[p0].pfn<<12)|(g&0xFFF);
}

uint64_t npt_read_entry(npt_mgr_t*m,uint64_t g){
    npt_entry_t *pd, *pt;
    uint32_t p4=(uint32_t)NPT_PML4_INDEX(g),p2=(uint32_t)NPT_PDPT_INDEX(g),p1=(uint32_t)NPT_PD_INDEX(g),p0=(uint32_t)NPT_PT_INDEX(g);
    if(!m||!m->pml4_va)return 0;
    pd=npt_get_pd(m,p4,p2);
    if(!pd)return 0;
    if(pd[p1].large_page)return pd[p1].all;
    if(!pd[p1].present)return 0;
    pt=npt_get_pt(m,p4,p2,p1);
    if(!pt)return 0;
    return pt[p0].all;
}

int npt_exclude_pa(npt_mgr_t*m,uint64_t g){
    int st=npt_split_2mb_to_4kb(m,g);
    if(st)return st;
    return npt_set_page_perm(m,g,0);
}

int npt_exclude_range(npt_mgr_t*m,uint64_t g,uint64_t s){
    uint64_t end=g+s;
    uint64_t pa=g&~(HV_PAGE_SIZE-1);
    for(;pa<end;pa+=HV_PAGE_SIZE){
        int st=npt_exclude_pa(m,pa);
        if(st&&st!=STATUS_SUCCESS)return st;
    }
    return STATUS_SUCCESS;
}

int npt_exclude_self(npt_mgr_t*m){
    uint32_t p4,p2,p1;
    npt_entry_t *pdpt,*pd,*pt;
    if(!m||!m->pml4_va)return STATUS_INVALID_PARAMETER;
    npt_exclude_pa(m,m->pml4_pa);
    for(p4=0;p4<512;p4++){
        if(!m->pml4_va[p4].present)continue;
        pdpt=npt_get_pdpt(m,p4);
        if(!pdpt)continue;
        npt_exclude_pa(m,(uint64_t)m->pml4_va[p4].pfn<<12);
        for(p2=0;p2<512;p2++){
            if(!pdpt[p2].present)continue;
            pd=npt_get_pd(m,p4,p2);
            if(!pd)continue;
            npt_exclude_pa(m,(uint64_t)pdpt[p2].pfn<<12);
            for(p1=0;p1<512;p1++){
                if(!pd[p1].present)continue;
                if(!pd[p1].large_page){
                    pt=npt_get_pt(m,p4,p2,p1);
                    if(pt)npt_exclude_pa(m,(uint64_t)pd[p1].pfn<<12);
                }
            }
        }
    }
    return STATUS_SUCCESS;
}

int npt_cleanup(npt_mgr_t*m){
    uint32_t i,j,k;
    if(g_cache){
        for(i=0;i<512;i++){
            npt_entry_t *pdpt=g_cache->pdpt_va[i];
            if(!pdpt)continue;
            for(j=0;j<512;j++){
                if(pdpt[j].present){
                    npt_entry_t *pd=(npt_entry_t*)MmGetVirtualForPhysical(
                        (PHYSICAL_ADDRESS){ .QuadPart = (uint64_t)pdpt[j].pfn << 12 });
                    if(!pd)continue;
                    for(k=0;k<512;k++){
                        if(pd[k].present&&!pd[k].large_page){
                            npt_entry_t *pt=(npt_entry_t*)MmGetVirtualForPhysical(
                                (PHYSICAL_ADDRESS){ .QuadPart = (uint64_t)pd[k].pfn << 12 });
                            if(pt)MmFreeContiguousMemory(pt);
                        }
                    }
                    MmFreeContiguousMemory(pd);
                }
            }
            MmFreeContiguousMemory(pdpt);
        }
        ExFreePool(g_cache);
        g_cache=NULL;
    }
    if(m->pml4_va)MmFreeContiguousMemory(m->pml4_va);
    RtlZeroMemory(m,sizeof(*m));
    return STATUS_SUCCESS;
}
