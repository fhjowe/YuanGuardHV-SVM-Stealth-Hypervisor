#ifndef YGHV_PROTECT_H
#define YGHV_PROTECT_H
#include <ntddk.h>
#include "npt.h"

#define YGHV_PROTECT_MAX_PAGES 64
#define YGHV_PROTECT_MAX_HOOKS 4
#define YGHV_PROTECT_PATCH_LEN 16

typedef enum {
    YGHV_PROTECT_MEM     = 0x1,
    YGHV_PROTECT_TERM    = 0x2,
    YGHV_PROTECT_HANDLE  = 0x4,
} yghv_protect_flags_t;

typedef struct {
    uint64_t gpa;
    uint64_t target_va;
    uint8_t  flags;
    uint8_t  armed;
} yghv_protect_page_t;

typedef struct {
    uint32_t pid;
    uint64_t cr3;
    PEPROCESS process;
    uint64_t flags;
    uint32_t page_count;
    yghv_protect_page_t pages[YGHV_PROTECT_MAX_PAGES];
    volatile BOOLEAN active;
} yghv_protect_state_t;

typedef struct {
    uint64_t func_va;
    uint64_t func_pa;
    uint8_t  original[YGHV_PROTECT_PATCH_LEN];
    uint8_t  installed;
    uint8_t  hook_id;
} yghv_protect_hook_t;

NTSTATUS yghv_protect_init(void);
void yghv_protect_cleanup(void);
NTSTATUS yghv_protect_set_target(uint32_t pid);
BOOLEAN yghv_protect_is_target_cr3(uint64_t cr3);
NTSTATUS yghv_protect_add_page(uint64_t target_va);
NTSTATUS yghv_protect_remove_page(uint64_t target_va);
yghv_protect_page_t *yghv_protect_find_page(uint64_t gpa);
int yghv_protect_arm_page(yghv_protect_page_t *p);
int yghv_protect_disarm_page(yghv_protect_page_t *p);
uint64_t yghv_protect_guest_va_to_pa(uint64_t cr3, uint64_t va);
NTSTATUS yghv_protect_start(void);
NTSTATUS yghv_protect_stop(void);
NTSTATUS yghv_protect_install_hook(uint8_t hook_id, uint64_t func_va);
NTSTATUS yghv_protect_remove_hook(uint8_t hook_id);
uint64_t yghv_protect_on_hook_query(uint8_t hook_id, uint64_t accessor_cr3);
uint64_t yghv_protect_find_func_pattern(PCWSTR name_hint, uint8_t *pat, SIZE_T pat_len);

extern yghv_protect_state_t g_protect;
extern yghv_protect_hook_t g_protect_hooks[YGHV_PROTECT_MAX_HOOKS];

#endif
