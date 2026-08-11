# Phase 3 进程保护 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在 YuanGuardHV 虚拟化层实现单目标进程的内存页写保护、进程终止保护和句柄保护，并在 VMware 调试 VM 内通过驱动内自动化测试验证。

**Architecture:** 复用已验证的 NPT/NPF 链路。内存页保护把受保护页设为 NPT 只读，写触发 NPF 后按访问者 CR3/CPL 决策；终止/句柄保护用补丁 stub 把 `PspTerminateProcess`/`ObpCreateHandle` 开头改成跳转到驱动 trampoline，trampoline 用带认证的 VMMCALL 让 Hypervisor 决定放行或返回 `STATUS_ACCESS_DENIED`，函数页用 NPT 写保护防篡改。

**Tech Stack:** C（内核模式 WDK）、clang-cl + MSVC link（见 `build.bat`）、AMD-V SVM/NPT、VMware 17.6.4 调试 VM + KD 串口管道。

## Global Constraints

- 仓库根：`D:\yuanguard`；代码在 `YuanGuardHV\hv`，构建脚本 `YuanGuardHV\build.bat`。
- 任何代码/资源改动前先向用户说明并取得确认；改动与决策必须记录到 `docs/YUANMOD_HANDOFF_CURRENT.md`。
- 禁止回滚用户/历史未提交改动；每阶段结束提交一次 git。
- 测试基线与验证方式：构建 vNN → `Copy-Item bin\yuanguard_hv.sys D:\aaaaaavm\yuanguard_hv_vNN.sys` → VM 中 `copy \\vmware-host\Shared Folders\aaaaaavm\yuanguard_hv_vNN.sys C:\yuanguard_hv.sys` → `sc.exe start yuanguard` → KD 日志核对。
- 保持 `YGHV_R1_SKIP_NPT_TEST=0`、`YGHV_R1_NPT_UNIT_TEST=1`（v26 已启用，回归必须继续 PASS）。
- 不启用 NPT 私有页剔除、NPT 自剔除（VMware 嵌套 SVM 会崩，留给裸机/KVM）。
- 目标平台镜像：Win10 Pro 19045.2965；`PspTerminateProcess`/`ObpCreateHandle` 按该镜像定位。

---

### Task 1: protect 模块骨架（目标状态 + 页表 + NPT arm/disarm）

**Files:**
- Create: `YuanGuardHV/hv/common/protect.h`
- Create: `YuanGuardHV/hv/protect.c`
- Modify: `YuanGuardHV/build.bat`（`for %%f in (...)` 加 `protect`，link 加 `protect.obj`）
- Modify: `YuanGuardHV/hv/common/svm_defs.h`（加 `SVM_EXIT_EXCEPTION_DB`）

**Interfaces:**
- Produces:
  - `yghv_protect_state_t g_protect;`
  - `yghv_protect_hook_t g_protect_hooks[YGHV_PROTECT_MAX_HOOKS];`
  - `NTSTATUS yghv_protect_init(void);`
  - `void yghv_protect_cleanup(void);`
  - `NTSTATUS yghv_protect_set_target(uint32_t pid);`
  - `BOOLEAN yghv_protect_is_target_cr3(uint64_t cr3);`
  - `NTSTATUS yghv_protect_add_page(uint64_t target_va);`
  - `NTSTATUS yghv_protect_remove_page(uint64_t target_va);`
  - `yghv_protect_page_t *yghv_protect_find_page(uint64_t gpa);`
  - `int yghv_protect_arm_page(yghv_protect_page_t *p);`
  - `int yghv_protect_disarm_page(yghv_protect_page_t *p);`
  - `uint64_t yghv_protect_guest_va_to_pa(uint64_t cr3, uint64_t va);`
  - `NTSTATUS yghv_protect_start(void);`
  - `NTSTATUS yghv_protect_stop(void);`

- [ ] **Step 1: 创建 `protect.h`**

```c
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
```

- [ ] **Step 2: 在 `svm_defs.h` 加 #DB 退出码**

```c
#define SVM_EXIT_EXCEPTION_DB  (SVM_EXIT_EXCEPTION_BASE + 1)  /* #DB, vector 1 */
```

- [ ] **Step 3: 实现 `protect.c`**

```c
#include <ntddk.h>
#include "protect.h"
#include "debug.h"
#include "control_plane.h"

extern npt_mgr_t g_npt;

static const uint64_t YGHV_PT_ADDR_MASK = 0x000FFFFFFFFFF000ULL;

yghv_protect_state_t g_protect;
yghv_protect_hook_t g_protect_hooks[YGHV_PROTECT_MAX_HOOKS];

static uint64_t yghv_pt_read(uint64_t table_pa, uint64_t index) {
    uint64_t *va;
    if (!table_pa) return 0;
    va = MmGetVirtualForPhysical((PHYSICAL_ADDRESS){ .QuadPart = table_pa });
    if (!va) return 0;
    return va[index];
}

uint64_t yghv_protect_guest_va_to_pa(uint64_t cr3, uint64_t va) {
    uint64_t pml4e, pdpte, pde, pte;
    pml4e = yghv_pt_read(cr3 & YGHV_PT_ADDR_MASK, (va >> 39) & 0x1FF);
    if (!(pml4e & 1)) return 0;
    pdpte = yghv_pt_read(pml4e & YGHV_PT_ADDR_MASK, (va >> 30) & 0x1FF);
    if (!(pdpte & 1)) return 0;
    if (pdpte & (1ULL << 7))  /* 1GB page */
        return (pdpte & 0x000FFFFFC0000000ULL) | (va & 0x3FFFFFFFULL);
    pde = yghv_pt_read(pdpte & YGHV_PT_ADDR_MASK, (va >> 21) & 0x1FF);
    if (!(pde & 1)) return 0;
    if (pde & (1ULL << 7))  /* 2MB page */
        return (pde & 0x000FFFFFFFE00000ULL) | (va & 0x1FFFFFULL);
    pte = yghv_pt_read(pde & YGHV_PT_ADDR_MASK, (va >> 12) & 0x1FF);
    if (!(pte & 1)) return 0;
    return (pte & YGHV_PT_ADDR_MASK) | (va & 0xFFFULL);
}

NTSTATUS yghv_protect_init(void) {
    RtlZeroMemory(&g_protect, sizeof(g_protect));
    RtlZeroMemory(g_protect_hooks, sizeof(g_protect_hooks));
    return STATUS_SUCCESS;
}

void yghv_protect_cleanup(void) {
    yghv_protect_stop();
    if (g_protect.process) {
        ObDereferenceObject(g_protect.process);
        g_protect.process = NULL;
    }
    RtlZeroMemory(&g_protect, sizeof(g_protect));
    RtlZeroMemory(g_protect_hooks, sizeof(g_protect_hooks));
}

NTSTATUS yghv_protect_set_target(uint32_t pid) {
    PEPROCESS proc = NULL;
    NTSTATUS st = PsLookupProcessByProcessId((HANDLE)(ULONG_PTR)pid, &proc);
    if (!NT_SUCCESS(st)) {
        LOG_ERROR("protect set_target: lookup pid %u failed 0x%x", pid, st);
        return st;
    }
    g_protect.pid = pid;
    g_protect.process = proc;
    /* KPROCESS.DirectoryTableBase on 19045 is at offset 0x028. */
    g_protect.cr3 = *(volatile uint64_t *)((uint8_t *)proc + 0x028);
    if (!g_protect.cr3) {
        ObDereferenceObject(proc);
        LOG_ERROR("protect set_target: pid %u has no CR3", pid);
        return STATUS_INVALID_PARAMETER;
    }
    LOG_ERROR("protect target: pid=%u process=0x%llx cr3=0x%llx", pid,
        (uint64_t)proc, g_protect.cr3);
    return STATUS_SUCCESS;
}

BOOLEAN yghv_protect_is_target_cr3(uint64_t cr3) {
    return g_protect.cr3 != 0 && cr3 == g_protect.cr3;
}

NTSTATUS yghv_protect_add_page(uint64_t target_va) {
    uint64_t gpa;
    yghv_protect_page_t *p;
    if (g_protect.page_count >= YGHV_PROTECT_MAX_PAGES)
        return STATUS_INSUFFICIENT_RESOURCES;
    if (!g_protect.cr3)
        return STATUS_INVALID_PARAMETER;
    gpa = yghv_protect_guest_va_to_pa(g_protect.cr3, target_va);
    if (!gpa) {
        LOG_ERROR("protect add_page: va 0x%llx not mapped", target_va);
        return STATUS_INVALID_ADDRESS;
    }
    p = &g_protect.pages[g_protect.page_count++];
    p->gpa = gpa;
    p->target_va = target_va;
    p->flags = YGHV_PROTECT_MEM;
    p->armed = 0;
    LOG_ERROR("protect add_page: va=0x%llx gpa=0x%llx", target_va, gpa);
    return STATUS_SUCCESS;
}

NTSTATUS yghv_protect_remove_page(uint64_t target_va) {
    uint32_t i;
    for (i = 0; i < g_protect.page_count; i++) {
        if (g_protect.pages[i].target_va == target_va) {
            if (g_protect.pages[i].armed)
                yghv_protect_disarm_page(&g_protect.pages[i]);
            g_protect.pages[i] = g_protect.pages[g_protect.page_count - 1];
            g_protect.page_count--;
            return STATUS_SUCCESS;
        }
    }
    return STATUS_NOT_FOUND;
}

yghv_protect_page_t *yghv_protect_find_page(uint64_t gpa) {
    uint32_t i;
    uint64_t page = gpa & ~0xFFFULL;
    for (i = 0; i < g_protect.page_count; i++)
        if (g_protect.pages[i].gpa == page)
            return &g_protect.pages[i];
    return NULL;
}

int yghv_protect_arm_page(yghv_protect_page_t *p) {
    int st = npt_set_page_perm(&g_npt, p->gpa, NPT_PERM_PRESENT);
    if (!st) p->armed = 1;
    return st;
}

int yghv_protect_disarm_page(yghv_protect_page_t *p) {
    int st = npt_set_page_perm(&g_npt, p->gpa,
        NPT_PERM_PRESENT | NPT_PERM_WRITABLE);
    if (!st) p->armed = 0;
    return st;
}

NTSTATUS yghv_protect_start(void) {
    uint32_t i;
    if (!g_protect.cr3) return STATUS_INVALID_PARAMETER;
    for (i = 0; i < g_protect.page_count; i++) {
        int st = yghv_protect_arm_page(&g_protect.pages[i]);
        if (st) {
            LOG_ERROR("protect start: arm page %u failed 0x%x", i, st);
            yghv_protect_stop();
            return (NTSTATUS)st;
        }
    }
    g_protect.active = TRUE;
    LOG_ERROR("protect start: %u pages armed", g_protect.page_count);
    return STATUS_SUCCESS;
}

NTSTATUS yghv_protect_stop(void) {
    uint32_t i;
    for (i = 0; i < g_protect.page_count; i++)
        if (g_protect.pages[i].armed)
            yghv_protect_disarm_page(&g_protect.pages[i]);
    g_protect.active = FALSE;
    return STATUS_SUCCESS;
}
```

- [ ] **Step 4: 修改 `build.bat` 编译与链接**

把 `for %%f in (main svm_core npt_core vmexit vmmcall multi_core)` 改为 `for %%f in (main svm_core npt_core vmexit vmmcall multi_core protect)`；link 行加入 `"%BIN_DIR%\protect.obj" ^`。

- [ ] **Step 5: 构建验证**

Run: `cmd /c build.bat`
Expected: `Build SUCCESS`，`protect.c` 编译无错误（WDK intrinsic 警告可忽略）。

- [ ] **Step 6: 提交**

```powershell
git add YuanGuardHV/hv/common/protect.h YuanGuardHV/hv/protect.c YuanGuardHV/hv/common/svm_defs.h YuanGuardHV/build.bat
git commit -m "feat: protect 模块骨架（目标状态/页表遍历/NPT arm-disarm）"
```

---

### Task 2: 控制面扩展（VMMCALL 命令）

**Files:**
- Modify: `YuanGuardHV/hv/common/control_plane.h`
- Modify: `YuanGuardHV/hv/vmmcall.c`

**Interfaces:**
- Consumes: Task 1 的 `yghv_protect_*` 全部接口。
- Produces: `YGHV_CMD_SET_TARGET 0x20`、`YGHV_CMD_ADD_PAGE 0x21`、`YGHV_CMD_REMOVE_PAGE 0x22`、`YGHV_CMD_START_PROTECT 0x23`、`YGHV_CMD_STOP_PROTECT 0x24`、`YGHV_CMD_GET_STATE 0x25`、`YGHV_CMD_HOOK_QUERY 0x60`、`YGHV_STATUS_DENIED 2`、`YGHV_STATUS_INVALID 3`。

- [ ] **Step 1: 扩展 `control_plane.h`**

```c
#define YGHV_CMD_SET_TARGET    0x20ULL
#define YGHV_CMD_ADD_PAGE      0x21ULL
#define YGHV_CMD_REMOVE_PAGE   0x22ULL
#define YGHV_CMD_START_PROTECT 0x23ULL
#define YGHV_CMD_STOP_PROTECT  0x24ULL
#define YGHV_CMD_GET_STATE     0x25ULL
#define YGHV_CMD_HOOK_QUERY    0x60ULL
#define YGHV_STATUS_DENIED     2ULL
#define YGHV_STATUS_INVALID    3ULL
```

- [ ] **Step 2: `vmmcall.c` 加保护命令分发**

在 `#include "control_plane.h"` 后加 `#include "protect.h"`，并在 `vmmcall_dispatch` 的 `default` 前插入：

```c
    case YGHV_CMD_SET_TARGET:
        if (vcpu->regs.rcx != g_vmmcall_auth_cookie) {
            vcpu->regs.rax = YGHV_STATUS_DENIED;
            return 0;
        }
        vcpu->regs.rax = (uint64_t)yghv_protect_set_target((uint32_t)vcpu->regs.rdx);
        return 0;

    case YGHV_CMD_ADD_PAGE:
        if (vcpu->regs.rcx != g_vmmcall_auth_cookie) {
            vcpu->regs.rax = YGHV_STATUS_DENIED;
            return 0;
        }
        vcpu->regs.rax = (uint64_t)yghv_protect_add_page(vcpu->regs.rdx);
        return 0;

    case YGHV_CMD_REMOVE_PAGE:
        if (vcpu->regs.rcx != g_vmmcall_auth_cookie) {
            vcpu->regs.rax = YGHV_STATUS_DENIED;
            return 0;
        }
        vcpu->regs.rax = (uint64_t)yghv_protect_remove_page(vcpu->regs.rdx);
        return 0;

    case YGHV_CMD_START_PROTECT:
        if (vcpu->regs.rcx != g_vmmcall_auth_cookie) {
            vcpu->regs.rax = YGHV_STATUS_DENIED;
            return 0;
        }
        vcpu->regs.rax = (uint64_t)yghv_protect_start();
        return 0;

    case YGHV_CMD_STOP_PROTECT:
        if (vcpu->regs.rcx != g_vmmcall_auth_cookie) {
            vcpu->regs.rax = YGHV_STATUS_DENIED;
            return 0;
        }
        yghv_protect_stop();
        vcpu->regs.rax = YGHV_STATUS_OK;
        return 0;

    case YGHV_CMD_GET_STATE:
        vcpu->regs.rax = g_protect.active ? 1 : 0;
        vcpu->regs.rbx = g_protect.page_count;
        vcpu->regs.rcx = g_protect.pid;
        return 0;
```

- [ ] **Step 3: 构建**

Run: `cmd /c build.bat`；Expected: `Build SUCCESS`。

- [ ] **Step 4: 提交**

```powershell
git add YuanGuardHV/hv/common/control_plane.h YuanGuardHV/hv/vmmcall.c
git commit -m "feat: VMMCALL 保护控制命令"
```

---

### Task 3: NPF 写决策 + #DB 重新加锁 + 常驻心跳

**Files:**
- Modify: `YuanGuardHV/hv/common/svm_vcpu.h`（`svm_vcpu_t` 尾部加 `rearm_gpa`）
- Modify: `YuanGuardHV/hv/vmexit.c`
- Modify: `YuanGuardHV/hv/vmmcall.c`（心跳常驻逻辑）

**Interfaces:**
- Consumes: Task 1 的 `yghv_protect_find_page/is_target_cr3/arm/disarm`。
- Produces: `vcpu->rearm_gpa`；`SVM_EXIT_EXCEPTION_DB` 处理；`g_protect.active` 时心跳不停止。

- [ ] **Step 1: `svm_vcpu_t` 加字段**

在 `resident_interrupt_exits;` 后加：

```c
    uint64_t rearm_gpa;
```

- [ ] **Step 2: `vmexit.c` 扩展 NPF 分支**

在 `case SVM_EXIT_NPF:` 内、`g_npt_test_active` 判断之前插入：

```c
        yghv_protect_page_t *pp = yghv_protect_find_page(vcpu->vmcb->control.exitinfo2);
        if (pp && (info1 & NPF_INFO1_WRITE)) {
            static uint64_t ring0_logged = 0;
            if (yghv_protect_is_target_cr3(vcpu->vmcb->state.cr3) ||
                vcpu->vmcb->state.cpl == 0) {
                /* target process or ring0: allow one write, re-arm after #DB */
                yghv_protect_disarm_page(pp);
                vcpu->rearm_gpa = pp->gpa;
                vcpu->vmcb->state.rflags |= 0x100ULL;  /* TF */
                if (vcpu->vmcb->state.cpl == 0 && ring0_logged++ < 32)
                    LOG_ERROR("protect: ring0 write allowed gpa=0x%llx", pp->gpa);
                return 0;
            }
            /* foreign user-mode write: inject #PF with write error code */
            vcpu->vmcb->control.event_injection =
                SVM_EVENTINJ_VALID | SVM_EVENTINJ_TYPE_EXC |
                SVM_EVENTINJ_ERROR_VALID | 0x0E | (2ULL << 32);
            LOG_ERROR("protect: foreign write denied gpa=0x%llx cr3=0x%llx",
                pp->gpa, vcpu->vmcb->state.cr3);
            return 0;
        }
```

在 `case SVM_EXIT_NPF:` 之前插入：

```c
    case SVM_EXIT_EXCEPTION_DB:
        if (vcpu->rearm_gpa) {
            yghv_protect_page_t *pp = yghv_protect_find_page(vcpu->rearm_gpa);
            if (pp)
                yghv_protect_arm_page(pp);
            vcpu->rearm_gpa = 0;
            vcpu->vmcb->state.rflags &= ~0x100ULL;
            return 0;
        }
        return 0;
```

`vmexit.c` 顶部加 `#include "protect.h"`。

- [ ] **Step 3: `vmmcall.c` 心跳常驻逻辑**

把 `YGHV_HEARTBEAT_TEST_LIMIT` 判断改为：

```c
        if (g_protect.active) {
            if ((vcpu->resident_exits % 10000ULL) == 0)
                LOG_ERROR("heartbeat protect exits=%llu core=%u",
                    vcpu->resident_exits, vcpu->resident_index);
            vcpu->regs.rax = YGHV_STATUS_OK;
            return 0;
        }
        if (vcpu->resident_exits >= YGHV_HEARTBEAT_TEST_LIMIT) {
            LOG_ERROR("heartbeat limit reached, stopping resident loop");
            svm_core_stop_all_residents();
            vcpu->regs.rax = YGHV_STATUS_OK;
            return 1;
        }
```

`vmmcall.c` 顶部加 `#include "protect.h"`。

- [ ] **Step 4: 构建**

Run: `cmd /c build.bat`；Expected: `Build SUCCESS`。

- [ ] **Step 5: 提交**

```powershell
git add YuanGuardHV/hv/common/svm_vcpu.h YuanGuardHV/hv/vmexit.c YuanGuardHV/hv/vmmcall.c
git commit -m "feat: NPF 写决策 + #DB 重新加锁 + 常驻心跳"
```

---

### Task 4: 驱动内保护测试（策略矩阵 + 真实写陷阱）→ v27

**Files:**
- Modify: `YuanGuardHV/hv/svm_trampoline.S`（新增 `svm_trampoline_test_prot_write`）
- Modify: `YuanGuardHV/hv/main.c`（`extern` 声明 + `yghv_protect_test()` + 调用）

**Interfaces:**
- Consumes: Task 1-3 全部接口。
- Produces: KD 日志 `protect test: policy PASS/FAIL`、`protect test: real write PASS/FAIL`。

- [ ] **Step 1: `svm_trampoline.S` 加写测试 guest**

在 `svm_trampoline_test_npt_guest_end:` 之后追加：

```asm
# --- Protect write test guest ---
    .globl svm_trampoline_test_prot_write
    .globl svm_trampoline_test_prot_write_resume
    .globl svm_trampoline_test_prot_write_end
svm_trampoline_test_prot_write:
    mov     qword ptr [rdi], 1      # write protected page -> NPF
    mov     rax, 2                  # YGHV_CMD_STOP_INTERNAL
svm_trampoline_test_prot_write_resume:
    jmp svm_trampoline_test_prot_write
svm_trampoline_test_prot_write_end:
```

- [ ] **Step 2: `main.c` 加 `yghv_protect_test()`**

```c
extern const uint8_t svm_trampoline_test_prot_write[];
extern const uint8_t svm_trampoline_test_prot_write_resume[];
extern const uint8_t svm_trampoline_test_prot_write_end[];

static NTSTATUS yghv_protect_test(void) {
    void *buf;
    uint64_t buf_pa, buf_va;
    uint64_t entry_after;
    svm_vcpu_t *v;
    int ok = 1;

    /* Phase B: policy matrix (synthetic) */
    g_protect.cr3 = 0x1000ULL;   /* fake target CR3 */
    if (!yghv_protect_is_target_cr3(0x1000ULL)) ok = 0;
    if (yghv_protect_is_target_cr3(0x2000ULL)) ok = 0;
    LOG_ERROR("protect test: policy %s", ok ? "PASS" : "FAIL");
    if (!ok) return STATUS_UNSUCCESSFUL;

    /* Phase C: real write trap, target = System (current process) */
    yghv_protect_set_target((uint32_t)(ULONG_PTR)PsGetCurrentProcessId());
    buf = MmAllocateContiguousMemory(HV_PAGE_SIZE,
        (PHYSICAL_ADDRESS){ .QuadPart = 0xFFFFFFFF });
    if (!buf) return STATUS_INSUFFICIENT_RESOURCES;
    RtlZeroMemory(buf, HV_PAGE_SIZE);
    buf_va = (uint64_t)buf;
    buf_pa = MmGetPhysicalAddress(buf).QuadPart;
    if (yghv_protect_add_page(buf_va) ||
        yghv_protect_start()) {
        LOG_ERROR("protect test: setup FAILED");
        MmFreeContiguousMemory(buf);
        return STATUS_UNSUCCESSFUL;
    }
    entry_after = npt_read_entry(&g_npt, buf_pa);
    if (entry_after & NPT_PERM_WRITABLE) {
        LOG_ERROR("protect test: page still writable after arm");
        ok = 0;
    }

    v = svm_core_get_vcpu(0);
    v->regs.rdi = buf_va;
    v->regs.rcx = g_vmmcall_auth_cookie;
    v->vmcb->state.rip = (uint64_t)svm_trampoline_test_prot_write;
    v->vmcb->state.rax = 0;
    svm_core_enter_resident_current(0);
    entry_after = npt_read_entry(&g_npt, buf_pa);
    if (entry_after & NPT_PERM_WRITABLE) {
        LOG_ERROR("protect test: page not re-armed after write");
        ok = 0;
    }
    LOG_ERROR("protect test: real write %s", ok ? "PASS" : "FAIL");
    yghv_protect_stop();
    yghv_protect_remove_page(buf_va);
    MmFreeContiguousMemory(buf);
    return ok ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
}
```

在 DriverEntry 的 `#if !YGHV_R1_SKIP_NPT_TEST` 块结束后、`svm_core_prepare_vcpu_other(0)` 之前调用：

```c
    if (!NT_SUCCESS(yghv_protect_test())) {
        LOG_ERROR("protect test failed");
        npt_cleanup(&g_npt);
        svm_core_cleanup();
        if (g_guest_code_page) MmFreeContiguousMemory(g_guest_code_page);
        g_guest_code_page = NULL;
        KeRevertToUserAffinityThread();
        return STATUS_UNSUCCESSFUL;
    }
```

注意：`yghv_protect_test()` 内 `yghv_protect_set_target()` 使用 KPROCESS+0x028 读 CR3，仅对 19045 有效；测试中 System 进程的 CR3 必须非 0。

- [ ] **Step 3: 构建并复制 v27**

```powershell
cmd /c build.bat
Copy-Item bin\yuanguard_hv.sys D:\aaaaaavm\yuanguard_hv_v27.sys -Force
Get-FileHash D:\aaaaaavm\yuanguard_hv_v27.sys -Algorithm SHA256
```

- [ ] **Step 4: VM 验证**

VM 中执行：
```powershell
powershell -ExecutionPolicy Bypass -File "\\vmware-host\Shared Folders\aaaaaavm\unload_driver.ps1"
copy "\\vmware-host\Shared Folders\aaaaaavm\yuanguard_hv_v27.sys" C:\yuanguard_hv.sys
sc.exe start yuanguard
```

Expected KD：`r1 unit: PASS`、`NPT test NPF`、`protect test: policy PASS`、`protect test: real write PASS`、`heartbeat limit reached`，无崩溃。

- [ ] **Step 5: 记录 + 提交**

更新 `docs/YUANMOD_HANDOFF_CURRENT.md`（v27 验证结果）并提交：

```powershell
git add YuanGuardHV/hv/svm_trampoline.S YuanGuardHV/hv/main.c docs/YUANMOD_HANDOFF_CURRENT.md
git commit -m "feat: 阶段1 内存页写保护 + 驱动内测试 v27"
```

---

### Task 5: 终止保护（函数定位 + 补丁 stub + HOOK_QUERY）→ v28

**Files:**
- Modify: `YuanGuardHV/hv/common/protect.h`（hook 接口声明）
- Modify: `YuanGuardHV/hv/protect.c`（定位、stub 生成、安装/卸载、`on_hook_query`）
- Modify: `YuanGuardHV/hv/vmmcall.c`（`HOOK_QUERY` 分发）
- Modify: `YuanGuardHV/hv/vmexit.c`（函数页写保护走 NPF 写策略，无需新分支）

**Interfaces:**
- Consumes: `g_protect.cr3`、`npt_set_page_perm`、`yghv_protect_is_target_cr3`。
- Produces: `yghv_protect_install_hook(uint8_t hook_id, uint64_t func_va)`、`yghv_protect_remove_hook(uint8_t hook_id)`、`yghv_protect_on_hook_query(uint8_t hook_id)`、`YGHV_CMD_HOOK_QUERY` 分发。

- [ ] **Step 1: `protect.c` 实现 stub 生成与安装**

```c
static uint8_t *g_hook_stub_pages[YGHV_PROTECT_MAX_HOOKS];

static void yghv_emit_u8(uint8_t *p, uint8_t v) { *p = v; }
static void yghv_emit_u64(uint8_t *p, uint64_t v) {
    for (int i = 0; i < 8; i++) p[i] = (uint8_t)(v >> (i * 8));
}
static void yghv_emit_rel32(uint8_t *p, uint64_t from, uint64_t to) {
    int64_t d = (int64_t)(to - (from + 5));
    p[0] = 0xE9;
    for (int i = 0; i < 4; i++) p[1 + i] = (uint8_t)((uint64_t)d >> (i * 8));
}

NTSTATUS yghv_protect_install_hook(uint8_t hook_id, uint64_t func_va) {
    uint8_t *stub;
    uint64_t func_pa, page_va, page_pa;
    uint8_t *orig;
    yghv_protect_hook_t *h;

    if (hook_id >= YGHV_PROTECT_MAX_HOOKS) return STATUS_INVALID_PARAMETER;
    h = &g_protect_hooks[hook_id];
    if (h->installed) return STATUS_ALREADY_COMMITTED;

    func_pa = MmGetPhysicalAddress((PVOID)func_va).QuadPart;
    page_va = func_va & ~(HV_PAGE_SIZE - 1);
    page_pa = MmGetPhysicalAddress((PVOID)page_va).QuadPart;
    RtlCopyMemory(h->original, (void *)func_va, YGHV_PROTECT_PATCH_LEN);
    h->func_va = func_va;
    h->func_pa = func_pa;
    h->hook_id = hook_id;

    stub = (uint8_t *)MmAllocateContiguousMemory(HV_PAGE_SIZE,
        (PHYSICAL_ADDRESS){ .QuadPart = 0xFFFFFFFF });
    if (!stub) return STATUS_INSUFFICIENT_RESOURCES;
    RtlZeroMemory(stub, HV_PAGE_SIZE);
    g_hook_stub_pages[hook_id] = stub;

    /* entry: push rax/rcx/rbx, mov rbx,hook_id, movabs rcx,cookie,
       mov rax,HOOK_QUERY, vmmcall, test rax,rax, jnz deny */
    uint8_t *p = stub;
    p[0]=0x50; p[1]=0x51; p[2]=0x53;               /* push rax,rcx,rbx */
    p[3]=0x48; p[4]=0xC7; p[5]=0xC3;               /* mov rbx, imm32 */
    p[6]=hook_id; p[7]=0; p[8]=0; p[9]=0;
    p[10]=0x48; p[11]=0xB9;                        /* movabs rcx, imm64 */
    yghv_emit_u64(p+12, g_vmmcall_auth_cookie);
    p[20]=0x48; p[21]=0xB8;                        /* movabs rax, HOOK_QUERY */
    yghv_emit_u64(p+22, YGHV_CMD_HOOK_QUERY);
    p[30]=0x0F; p[31]=0x01; p[32]=0xD9;            /* vmmcall */
    p[33]=0x48; p[34]=0x85; p[35]=0xC0;            /* test rax,rax */
    p[36]=0x75; p[37]=0x08;                        /* jnz +8 -> deny at 0x2E */
    p[38]=0x5B; p[39]=0x59; p[40]=0x58;            /* pop rbx,rcx,rax */
    p[41]=0xE9;                                    /* jmp rel32 -> original slot */
    /* rel32 patched below */
    p[46]=0x5B; p[47]=0x59; p[48]=0x58;            /* deny: pop rbx,rcx,rax */
    p[49]=0x48; p[50]=0xB8;                        /* movabs rax, STATUS_ACCESS_DENIED */
    yghv_emit_u64(p+51, 0xC0000022ULL);
    p[59]=0xC3;                                    /* ret */

    /* original slot at offset 0x40 */
    orig = stub + 0x40;
    RtlCopyMemory(orig, h->original, YGHV_PROTECT_PATCH_LEN);
    /* jump back to func_va+16 after original bytes */
    orig[0x10] = 0x49; orig[0x11] = 0xBB;          /* movabs r11, imm64 */
    yghv_emit_u64(orig + 0x12, func_va + YGHV_PROTECT_PATCH_LEN);
    orig[0x1A] = 0x41; orig[0x1B] = 0xFF; orig[0x1C] = 0xE3;  /* jmp r11 */

    yghv_emit_rel32(p + 41, (uint64_t)(p + 41), (uint64_t)orig);

    /* patch function entry: E9 rel32 -> stub */
    uint64_t old_cr0 = __readcr0();
    __writecr0(old_cr0 & ~(1ULL << 16));           /* clear CR0.WP */
    yghv_emit_rel32((uint8_t *)func_va, func_va, (uint64_t)stub);
    __writecr0(old_cr0);

    /* write-protect the function's page in NPT */
    npt_set_page_perm(&g_npt, page_pa, NPT_PERM_PRESENT);
    h->installed = 1;
    LOG_ERROR("protect hook %u installed: va=0x%llx pa=0x%llx", hook_id, func_va, func_pa);
    return STATUS_SUCCESS;
}

NTSTATUS yghv_protect_remove_hook(uint8_t hook_id) {
    yghv_protect_hook_t *h;
    uint64_t page_pa;
    if (hook_id >= YGHV_PROTECT_MAX_HOOKS) return STATUS_INVALID_PARAMETER;
    h = &g_protect_hooks[hook_id];
    if (!h->installed) return STATUS_NOT_FOUND;
    uint64_t old_cr0 = __readcr0();
    __writecr0(old_cr0 & ~(1ULL << 16));
    RtlCopyMemory((void *)h->func_va, h->original, YGHV_PROTECT_PATCH_LEN);
    __writecr0(old_cr0);
    page_pa = MmGetPhysicalAddress((PVOID)(h->func_va & ~(HV_PAGE_SIZE - 1))).QuadPart;
    npt_set_page_perm(&g_npt, page_pa, NPT_PERM_PRESENT | NPT_PERM_WRITABLE);
    if (g_hook_stub_pages[hook_id]) {
        MmFreeContiguousMemory(g_hook_stub_pages[hook_id]);
        g_hook_stub_pages[hook_id] = NULL;
    }
    h->installed = 0;
    LOG_ERROR("protect hook %u removed", hook_id);
    return STATUS_SUCCESS;
}

uint64_t yghv_protect_on_hook_query(uint8_t hook_id, uint64_t accessor_cr3) {
    if (hook_id >= YGHV_PROTECT_MAX_HOOKS || !g_protect_hooks[hook_id].installed)
        return YGHV_STATUS_INVALID;
    if (yghv_protect_is_target_cr3(accessor_cr3))
        return YGHV_STATUS_OK;
    LOG_ERROR("protect hook %u denied cr3=0x%llx", hook_id, accessor_cr3);
    return YGHV_STATUS_DENIED;
}
```

- [ ] **Step 2: `vmmcall.c` 加 `HOOK_QUERY` 分发**

```c
    case YGHV_CMD_HOOK_QUERY:
        if (vcpu->regs.rcx != g_vmmcall_auth_cookie) {
            vcpu->regs.rax = YGHV_STATUS_DENIED;
            return 0;
        }
        vcpu->regs.rax = yghv_protect_on_hook_query(
            (uint8_t)vcpu->regs.rbx, vcpu->vmcb->state.cr3);
        return 0;
```

- [ ] **Step 3: 定位 `PspTerminateProcess` 的测试入口**

本阶段先用 `MmGetSystemRoutineAddress` 定位导出函数 `NtTerminateProcess` 作为 stub 目标（未导出 `PspTerminateProcess` 的模式扫描作为后续增强；当前镜像先用导出入口跑通链路）。`protect.c` 加：

```c
uint64_t yghv_protect_find_func_pattern(PCWSTR name_hint, uint8_t *pat, SIZE_T pat_len) {
    UNICODE_STRING name;
    (void)pat; (void)pat_len;
    RtlInitUnicodeString(&name, name_hint);
    return (uint64_t)MmGetSystemRoutineAddress(&name);
}
```

调用点（Task 6 测试中）：`uint64_t term = yghv_protect_find_func_pattern(L"NtTerminateProcess", NULL, 0);`

- [ ] **Step 4: 构建并复制 v28**

```powershell
cmd /c build.bat
Copy-Item bin\yuanguard_hv.sys D:\aaaaaavm\yuanguard_hv_v28.sys -Force
```

- [ ] **Step 5: VM 验证**

先验证 v28 在不安装 hook 的既有四段测试仍全 PASS（回归）；安装/决策测试在 Task 6 加。

- [ ] **Step 6: 提交**

```powershell
git add YuanGuardHV/hv/common/protect.h YuanGuardHV/hv/protect.c YuanGuardHV/hv/vmmcall.c
git commit -m "feat: 终止保护补丁 stub + HOOK_QUERY 决策"
```

---

### Task 6: 终止保护测试 → v28 完整验证

**Files:**
- Modify: `YuanGuardHV/hv/main.c`（`yghv_hook_test()`）

**Interfaces:**
- Consumes: Task 5 的 `yghv_protect_install_hook/remove_hook/on_hook_query`、`yghv_protect_find_func_pattern`。
- Produces: KD 日志 `protect hook test: PASS/FAIL`。

- [ ] **Step 1: `main.c` 加 `yghv_hook_test()`**

```c
static NTSTATUS yghv_hook_test(void) {
    uint64_t term;
    uint64_t gpa;
    uint64_t entry_before, entry_after;
    int ok = 1;

    term = yghv_protect_find_func_pattern(L"NtTerminateProcess", NULL, 0);
    if (!term) {
        LOG_ERROR("protect hook test: locate FAILED");
        return STATUS_NOT_FOUND;
    }
    if (yghv_protect_install_hook(0, term)) {
        LOG_ERROR("protect hook test: install FAILED");
        return STATUS_UNSUCCESSFUL;
    }
    gpa = MmGetPhysicalAddress((PVOID)(term & ~(HV_PAGE_SIZE - 1))).QuadPart;
    entry_before = npt_read_entry(&g_npt, gpa);
    if (entry_before & NPT_PERM_WRITABLE) ok = 0;

    if (yghv_protect_on_hook_query(0, g_protect.cr3) != YGHV_STATUS_OK) ok = 0;
    if (yghv_protect_on_hook_query(0, g_protect.cr3 + 0x1000) != YGHV_STATUS_DENIED) ok = 0;

    yghv_protect_remove_hook(0);
    entry_after = npt_read_entry(&g_npt, gpa);
    if (!(entry_after & NPT_PERM_WRITABLE)) ok = 0;
    if (memcmp((void *)term, &g_protect_hooks[0].original, YGHV_PROTECT_PATCH_LEN) != 0) ok = 0;

    LOG_ERROR("protect hook test: %s", ok ? "PASS" : "FAIL");
    return ok ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
}
```

在 `yghv_protect_test()` 之后调用 `yghv_hook_test()`，失败同样回滚并返回。

- [ ] **Step 2: 构建复制 v28 并 VM 验证**

```powershell
cmd /c build.bat
Copy-Item bin\yuanguard_hv.sys D:\aaaaaavm\yuanguard_hv_v28.sys -Force
```

VM 中执行加载命令；Expected KD：原有四段 PASS + `protect hook test: PASS`；`NtTerminateProcess` 前 16 字节恢复后与保存原值一致。

- [ ] **Step 3: 记录 + 提交**

更新 `docs/YUANMOD_HANDOFF_CURRENT.md`，提交：

```powershell
git add YuanGuardHV/hv/main.c docs/YUANMOD_HANDOFF_CURRENT.md
git commit -m "feat: 阶段2 终止保护测试 v28"
```

---

### Task 7: 句柄保护 → v29

**Files:**
- Modify: `YuanGuardHV/hv/protect.c`（句柄函数定位 + hook slot 1）
- Modify: `YuanGuardHV/hv/main.c`（`yghv_hook_test()` 覆盖两个 hook）

**Interfaces:**
- Consumes: Task 5 全部。
- Produces: hook_id 1 = `ObpCreateHandle`（当前镜像先用导出 `NtOpenProcess` 跑通，模式扫描增强后续）。

- [ ] **Step 1: `main.c` 测试扩展**

`yghv_hook_test()` 中增加：

```c
    uint64_t open = yghv_protect_find_func_pattern(L"NtOpenProcess", NULL, 0);
    if (!open || yghv_protect_install_hook(1, open)) ok = 0;
    if (yghv_protect_on_hook_query(1, g_protect.cr3) != YGHV_STATUS_OK) ok = 0;
    if (yghv_protect_on_hook_query(1, g_protect.cr3 + 0x1000) != YGHV_STATUS_DENIED) ok = 0;
    yghv_protect_remove_hook(1);
```

- [ ] **Step 2: 构建复制 v29 并 VM 验证**

Expected KD：`protect hook test: PASS`（覆盖 hook 0/1），四段回归 PASS。

- [ ] **Step 3: 记录 + 提交**

---

### Task 8: 常驻空转 + 收尾

**Files:**
- Modify: `YuanGuardHV/hv/main.c`（测试后 `START_PROTECT` 保持常驻，除非显式 STOP）
- Modify: `docs/YUANMOD_HANDOFF_CURRENT.md`、`docs/TASKS.md`

- [ ] **Step 1: 常驻逻辑**

DriverEntry 测试全部 PASS 后，若 `g_protect.cr3` 有效则调用 `yghv_protect_start()` 使保护常驻；`DriverUnload` 已通过 `yghv_protect_cleanup()` 恢复。

- [ ] **Step 2: VM 空转验证**

加载后 KD 观察 `heartbeat protect exits=...`，VM 保持响应 ≥5 分钟；`unload_driver.ps1` 干净卸载；再次加载正常。

- [ ] **Step 3: 更新文档并提交**

更新交接记录、TASKS（Phase 3 阶段 1-3 完成状态），提交：

```powershell
git add YuanGuardHV/hv/main.c docs/YUANMOD_HANDOFF_CURRENT.md docs/TASKS.md
git commit -m "feat: 阶段3 句柄保护 + 常驻保护模式验证"
```
