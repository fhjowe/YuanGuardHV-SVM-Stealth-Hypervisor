# YuanGuardHV 技术审查报告

> 日期：2026-07-30
> 范围：`D:/yuanguard/YuanGuardHV`，重点为 `hv/` 内核 Hypervisor 代码、`build.bat`、项目文档；`mod/`、`tests/` 当前基本为空。
> 方法：只读静态审查；未修改代码；未运行构建；未做物理机/VM 动态验证。
> 依据：AMD APM Vol.2 SVM/VMCB/NPT 布局、Windows 内核驱动开发约束、项目内 `PLAN.md` / `HANDOFF.md`。

---

## 0. 执行结论

当前代码处于“Phase 2 早期骨架 + 部分危险路径已定型”的状态。**按现状编译出的 `yuanguard_hv.sys` 不应在任何真实机器上加载**：即使绕过 VMware 的 SVM 暴露问题，在 bare-metal 上也会被 VMCB 布局错误、trampoline 寄存器错误、VMEXIT 不推进 RIP、清理路径错误等问题阻塞，高概率 BSOD 或永久挂起 SCM 线程。

最关键结论：

1. **功能正确性**：核心 SVM 路径未打通。VMCB 布局、`svm_core_init`、trampoline、VMEXIT RIP 推进、NPT/ASID/TLB 均存在致命问题。
2. **安全**：当前设计一旦按 Phase 3 继续堆功能，会形成“假保护”：全物理内存 RWX identity-map + VMMCALL 无认证 + 权限 API 假成功。
3. **稳定性/性能**：多核路径、全局状态生命周期、cleanup 顺序没有安全模型；DbgPrint 在 VMEXIT hot path；DPC resident loop 设计不可启用。
4. **可维护性**：`build.bat` 漏编译/死代码/无测试；`npt_core.c` 单行压缩；大量 stub 返回假成功；`.bak`/日志/历史产物污染仓库。

建议：不要继续在此基础上直接实现进程保护/Java 层。先完成 P0 修复并用单核最小 VMMCALL 冒烟验证，再恢复 NPT 权限、多核和 stealth。

---

## 1. 严重级别定义

| 级别 | 定义 | 处理要求 |
|---|---|---|
| P0 致命 | 必然/高概率 BSOD、无法 VMRUN、保护可被绕过、安全架构失效 | 阻塞后续开发，必须先修 |
| P1 高 | 启用相关功能即出错、泄露 HV、卸载/多核/检测路径高风险 | 发布前必须修 |
| P2 中 | 泄漏、死代码、构建/工程问题、未来启用会爆 | 进入 Phase 3 前修 |
| P3 低 | 风格、重复、文档漂移、卫生问题 | 随重构清理 |

---

## 2. 问题索引

| ID | 级别 | 维度 | 标题 | 主要位置 |
|---|---|---|---|---|
| YGHV-001 | P0 | 功能正确性 | VMCB 布局与 AMD APM 不一致 | `hv/common/vmcb.h`, `hv/common/svm_vcpu.h`, `hv/svm_trampoline.S` |
| YGHV-002 | P0 | 功能正确性 | `svm_core_init()` 真实路径被 `#if 0` | `hv/svm_core.c:332-394` |
| YGHV-003 | P0 | 功能正确性 | trampoline 寄存器保存/恢复错误 | `hv/svm_trampoline.S:55-131` |
| YGHV-004 | P0 | 功能正确性 | VMEXIT 不推进 guest RIP，返回值写错位置 | `hv/vmexit.c`, `hv/vmmcall.c`, `hv/svm_core.c` |
| YGHV-005 | P0 | 功能正确性/稳定性 | cleanup 分配释放不匹配并无条件清 EFER.SVME | `hv/svm_core.c:191-200,336-343,397-416` |
| YGHV-006 | P0 | 安全 | NPT 全物理 RWX identity-map，HV 自身暴露给 guest | `hv/common/npt.h`, `hv/npt_core.c`, `hv/main.c` |
| YGHV-007 | P0 | 安全 | VMMCALL 控制面无认证，未知命令可停保护 | `hv/vmmcall.c`, `hv/common/control_plane.h` |
| YGHV-008 | P0 | 安全 | NPT 权限 API 假成功 | `hv/npt_core.c:20-22` |
| YGHV-009 | P0 | 功能正确性 | NPF 事件注入 VALID 位写错 | `hv/vmexit.c:9,55-56` |
| YGHV-010 | P0 | 功能正确性 | ASID/TLB/PAT 未初始化 | `hv/svm_core.c:234-329`, `hv/common/vmcb.h` |
| YGHV-011 | P1 | 功能正确性 | GDT 描述符解析错误 | `hv/svm_core.c:203-231` |
| YGHV-012 | P1 | 稳定性/性能 | 多核 DPC resident loop 不可安全启用 | `hv/multi_core.c`, `hv/svm_core.c`, `build.bat` |
| YGHV-013 | P1 | 稳定性 | 全局状态无同步/无 rundown | `hv/svm_core.c`, `hv/npt_core.c`, `hv/main.c` |
| YGHV-014 | P1 | 功能正确性 | SVM/NPT CPUID 检测被硬编码为信任 | `hv/svm_core.c:98-121` |
| YGHV-015 | P1 | 功能正确性/安全 | MSR 读写语义错误，MSRPM 注释/初始化反向 | `hv/vmexit.c:127-138`, `hv/svm_core.c:156-168` |
| YGHV-016 | P1 | 安全 | CPUID 隐身死代码且不完整 | `hv/vmexit.c:95-119`, `hv/svm_core.c:241` |
| YGHV-017 | P1 | 安全 | 调试日志/池标签泄露 HV 与物理地址 | `hv/common/debug.h`, 各 `.c` 顶部宏 |
| YGHV-018 | P1 | 可维护性/安全 | 驱动生命周期不完整，loader_stealth 死代码且高风险 | `hv/main.c`, `hv/loader_stealth.c`, `hv/min_drv.c` |
| YGHV-019 | P1 | 安全 | 测试证书/文档指导弱化驱动签名校验 | `yuanguard_test.cer`, `HANDOFF.md` |
| YGHV-020 | P2 | 稳定性 | NPT 缓存全局单例、错误路径泄漏、映射洞 | `hv/npt_core.c`, `hv/main.c` |
| YGHV-021 | P2 | 安全/稳定性 | guest RSP 使用 host stack | `hv/svm_core.c:313-314` |
| YGHV-022 | P2 | 可维护性 | build.bat 漏编译/死代码/无 PDB/无签名/无 clean | `build.bat` |
| YGHV-023 | P2 | 可维护性 | stub/假成功/死代码过多 | `hv/svm_core.c`, `hv/svm_trampoline.S`, `hv/npt_core.c`, `hv/pool` |
| YGHV-024 | P2 | 可维护性 | 仓库卫生与第三方二进制未审计 | `hv/common/*.bak`, `kd_*.log`, `bin/`, `SymbolicAccessKM.lib` |
| YGHV-025 | P2 | 可维护性 | 测试与 mod 层为空，PLAN 漂移 | `tests/`, `mod/`, `PLAN.md` |
| YGHV-026 | P3 | 可维护性 | 代码风格、返回值、日志级别不统一 | `hv/npt_core.c`, `hv/svm_core.c`, `hv/common/debug.h` |
| YGHV-027 | P3 | 可维护性 | Intel VMX 遗留文件混在 AMD SVM 项目 | `hv/common/msr.h`, `crx.h`, `cpuid.h`, `reference_*` |
| YGHV-028 | P3 | 文档/协议 | control plane 枚举、PLAN、代码三者漂移 | `hv/common/control_plane.h`, `PLAN.md` |

---

## 3. P0 致命问题详录

### YGHV-001 — VMCB 布局与 AMD APM 不一致

- **级别**：P0
- **维度**：功能正确性
- **位置**：
  - `hv/common/vmcb.h:8-55`（`vmcb_control_t`）
  - `hv/common/vmcb.h:57-141`（`vmcb_state_t`）
  - `hv/common/svm_vcpu.h:38-41`
  - `hv/svm_trampoline.S:96,130`
- **证据**：
  - `vmcb.h:51` 断言 `sizeof(vmcb_control_t)==0x200`；APM 应为 control area `0x000-0x3FF`。
  - `vmcb.h:138` 注释/结构把 state 放在 `+0x200`；APM state save area 从 `0x400` 开始。
  - `svm_vcpu.h:38-41` 使用 `state.rflags=0x370`、`state.rip=0x378`、`state.rsp=0x3D8`、`state.rax=0x3F8`；正确绝对偏移应为 `state.rax=0x5F8`、`state.rip=0x578`、`state.rsp=0x5D8`、`state.rflags=0x570`。
  - `svm_trampoline.S:96` 从 `vmcb+0x3F8` 读 guest RAX；`svm_trampoline.S:130` 从 `vmcb+0x88` 读 exitcode。
- **正确偏移对照（关键项）**：
  - control：`exitcode=0x70`、`exitinfo1=0x78`、`exitinfo2=0x80`、`exitintinfo=0x88`、`np_enable=0x90`、`event_injection=0xA8`、`ncr3=0xB0`、`vmcb_clean=0xC0`、`next_rip=0xC8`。
  - 当前代码：`exitcode=0x88`、`exitinfo1=0x90`、`exitinfo2=0x98`、`np_enable=0xA8`、`event_injection=0xD0`、`ncr3=0xD8`、`next_rip=0xF0`。
  - state：代码把 state base 错为 `0x200`，导致 `state.rax` 用 `0x3F8` 而非 `0x5F8`。
- **影响**：
  - 硬件把 exitcode 写到 `0x70`，代码从 `0x88` 读到 `exitintinfo`；整个 `svm_dispatch_exit()` 基于错误值。
  - `svm_core_set_npt()` 写 `control.np_enable/ncr3` 到错误位置，等价于在设置 event injection/指令字节区。
  - guest 段、CR、RIP、RSP 等 state 写入 control 保留区；真实 state area 大量为 0，VMRUN 一致性失败或进入垃圾状态。
  - 注：`INTERCEPT_VMMCALL` 在当前 `general1_intercepts` u64 写法下可能“歪打正着”落到真实 VMMCALL 拦截位，但不改变整体布局错误。
- **修复方案**：
  1. 按 AMD APM Vol.2 Appendix B 重写 `vmcb_control_t`：control area 扩到 `0x400`；补齐 CR/DR 拦截、general intercepts、IOPM/MSRPM、ASID、TLB、VINTR、exit 信息、event injection、NCR3、clean bits、nRIP。
  2. `vmcb_state_t` 放到 VMCB `+0x400`。
  3. 删除或修正所有自证型 `_Static_assert`，改为断言 APM 真实偏移。
  4. 同步修改 `svm_vcpu.h` 的 `SVM_VMCB_STATE_*_OFFSET` 与 trampoline 硬编码偏移。
- **验证**：
  - 编译期断言：`offsetof(vmcb_t,state)==0x400`、`offsetof(vmcb_t,control.exitcode)==0x70`、`state.rax==0x5F8`。
  - 运行期最小冒烟：单核只拦截 VMMCALL，guest 执行 `vmmcall` 后应读到 `exitcode==0x81`，且 `next_rip` 指向 `vmmcall` 下一条。

---

### YGHV-002 — `svm_core_init()` 真实路径被 `#if 0`

- **级别**：P0
- **维度**：功能正确性
- **位置**：`hv/svm_core.c:332-347`（stub），`hv/svm_core.c:348-394`（被屏蔽真实实现）
- **证据**：
  - stub 直接 `ExAllocatePoolWithTag(NonPagedPool,sizeof(svm_vcpu_t))`，`vmcb` 也用 `ExAllocatePoolWithTag`；没有 `MmGetPhysicalAddress` 设置 `vmcb_pa/hsave_pa`。
  - 不设置 `EFER.SVME`，不调用 `svm_prepare_vcpu()`。
- **影响**：
  - 裸机且 SVME 未开：`clgi/vmrun` #UD，BSOD。
  - SVME 已开：`VMRUN rax=vmcb_pa=0`，物理页 0 非 VMCB，VMRUN invalid/死循环。
  - HANDOFF 中“已修复 Bug #1/#3/#6”实际对应被 `#if 0` 屏蔽的路径，当前构建不适用。
- **修复方案**：
  1. 恢复真实 `svm_core_init()`：CPUID 检测、SVME 置位/验证、`svm_alloc_vcpu()`、`svm_prepare_vcpu()`、初始化 `state.rax`。
  2. debug stub 只保留为独立最小驱动 target，不参与正式 `yuanguard_hv.sys`。
- **验证**：加载后第一步必须成功进入 VMEXIT-VMMCALL，而不是在 `clgi` 前崩溃。

---

### YGHV-003 — trampoline 寄存器保存/恢复错误

- **级别**：P0
- **维度**：功能正确性
- **位置**：`hv/svm_trampoline.S:42-131`
- **证据/问题分解**：
  1. `svm_trampoline.S:53` 把 `rbx` 用作 vcpu 指针；`:56-70` 加载 guest GPR 时刻意不加载 guest RBX。guest RBX 从未进入 CPU，也从未保存。
  2. `:73-79` 用 `wrmsr` 写 `MSR_VM_HSAVE`，使用 `ecx/edx/eax`；只 push/pop 了 RDX，RCX 被覆盖为 `0xC0010117`。
  3. `:127-131` 返回时注释认为 `rcx` 仍是 vcpu 指针，实际 RCX 在 `:69` 被加载为 guest RCX，VMEXIT 后仍是 guest 值；`mov rax,[rcx]` 可能解引用 0 或 guest 控制地址。
- **影响**：单次 VMRUN/VMEXIT 即可破坏 guest GPR 或触发内核 #PF。
- **修复方案**：
  1. 不使用 guest 可见 GPR 保存 host vcpu 指针；改用 host stack 或 host-only save area。
  2. 在进入 guest 前完整加载所有 guest GPR；VMEXIT 后完整保存所有 guest GPR。
  3. 读取 exitcode 时，在恢复 host GPR 前使用安全保存的 vcpu 指针：`vmcb = vcpu->vmcb; exitcode = vmcb->control.exitcode`。
  4. 将 VM_HSAVE 初始化移到加载 guest GPR 之前，或 wrmsr 后显式恢复 RCX/RAX。
- **验证**：构造 guest 修改全部 GPR 后执行 `vmmcall`；host 侧校验 `vcpu->regs` 与 guest 预期一致，且 trampoline 返回 `0x81`。

---

### YGHV-004 — VMEXIT 不推进 guest RIP，返回值写错位置

- **级别**：P0
- **维度**：功能正确性
- **位置**：`hv/vmexit.c:19-93`，`hv/vmmcall.c:11-44`，`hv/svm_core.c:485-490`
- **证据**：
  - `vmcb.h:45` 定义 `next_rip`，但全仓库无使用。
  - `vmmcall_dispatch()` / `svm_emulate_cpuid()` 写 `vcpu->regs.rax`，但 VMRUN guest RAX 来源是 `vmcb->state.rax`；trampoline 只有 `state.rax -> regs.rax` 的单向保存。
- **影响**：
  - VMMCALL/CPUID/MSR 退出后 guest RIP 仍指向触发指令，重新 VMRUN 会再次退出，形成无限 VMEXIT 风暴。
  - guest 拿不到 VMMCALL/CPUID 返回值。
- **修复方案**：
  1. 在 `svm_dispatch_exit()` 对所有可恢复 exit 统一执行：`vcpu->vmcb->state.rip = vcpu->vmcb->control.next_rip`。
  2. 明确返回值通道：CPUID/MSR 直接写 VMCB state 的 rax/rbx/rcx/rdx（或确保 trampoline 在下轮 VMRUN 前从 regs 同步到硬件可见状态）。
  3. 对不可恢复 exit 才返回 stop。
- **验证**：guest 执行 `vmmcall; ud2`，应只发生一次 VMMCALL exit，随后 RIP 到 `ud2` 并触发 #UD exit/stop。

---

### YGHV-005 — cleanup 分配释放不匹配并无条件清 EFER.SVME

- **级别**：P0
- **维度**：功能正确性/稳定性
- **位置**：`hv/svm_core.c:336-343`（stub 分配），`hv/svm_core.c:191-200`（free），`hv/svm_core.c:397-416`（cleanup），`hv/main.c:37,51-52`
- **证据**：
  - stub 中 `g_vcpus[0]->vmcb = ExAllocatePoolWithTag(...)`；`svm_free_vcpu()` 却对 `vcpu->vmcb` 调 `MmFreeContiguousMemory()`。
  - `svm_core_cleanup()` 无条件 `yg_write_msr(MSR_EFER, efer & ~EFER_SVME)`；但 `svm_core.c:363-365` 注释自己承认嵌套下 EFER wrmsr 可能 #GP。
  - `old_efer/old_hsave` 字段存在于 `svm_vcpu.h:101-103`，但从未保存/恢复。
- **影响**：正常退出和错误路径均可能 `BAD_POOL_CALLER`、#GP、或释放后硬件仍引用。
- **修复方案**：
  1. VMCB/HSAVE/host stack/MSRPM/IOPM 统一使用 `MmAllocateContiguousMemory` 和 `MmFreeContiguousMemory`。
  2. init 保存 `old_efer`、`old_hsave`；cleanup 只恢复本驱动修改过的 MSR。
  3. 固定顺序：stop residents → wait stopped → per-core restore MSR → free NPT → free VCPU。
- **验证**：强制 `npt_init` 失败、强制 `svm_core_set_npt` 失败、正常 stop 三条路径均不触发 bugcheck，池/ contiguous 内存无泄漏。

---

### YGHV-006 — NPT 全物理 RWX identity-map，HV 自身暴露给 guest

- **级别**：P0
- **维度**：安全
- **位置**：`hv/common/npt.h:12-13`，`hv/npt_core.c:16-18`，`hv/main.c:33-40`
- **证据**：
  - `NPT_LARGE_PAGE_FLAGS = present|writable|user|accessed|large_page`；NX=0。
  - `npt_identity_map_range(m,0,mp)` 从 0 到 `max_phys` 连续映射，不按真实 RAM ranges 排除 MMIO 洞。
  - `main.c:34` 将 `max_phys` 硬截断到 `0x1000000000`（64GB）。
- **影响**：
  - guest ring0 可直接读写包含 VMCB/HSAVE/host stack/NPT 页表/驱动镜像的物理页；扫描池 tag `'vhGY'` 可定位 HV。
  - MMIO 区域被当 RAM；>64GB 物理访问触发 NPF。
- **修复方案**：
  1. 按 `MmGetPhysicalMemoryRanges()` 逐段映射 RAM，不映射 MMIO 洞；MMIO NPF 单独仿真或告警。
  2. 从 guest NPT 剔除所有 HV 私有页：驱动镜像、每核 VMCB/host_vmcb/hsave/host_stack/msrpm/iopm、NPT 页表自身。
  3. 数据页 NX，代码页去 writable；对保护目标实现最小权限。
- **验证**：guest 内核扫描 `'vhGY'` 不应命中任何可读写 GPA；访问 HV 私有 GPA 应触发受控 NPF 告警。

---

### YGHV-007 — VMMCALL 控制面无认证，未知命令可停保护

- **级别**：P0
- **维度**：安全
- **位置**：`hv/vmmcall.c:11-44`，`hv/common/control_plane.h:4-15`
- **证据**：
  - `vmmcall_dispatch()` 直接信任 `vcpu->regs.rax`。
  - `default:` 分支 `return 1`，导致 resident loop 退出。
  - `YGHV_CMD_STOP_INTERNAL` 无鉴权；`control_plane.h` 预留 `PROTECT_HANDLE/UNPROTECT/SCAN_PROCESS/READ_MEMORY/GET_CONFIG/SET_CONFIG/SHUTDOWN`。
- **影响**：guest ring0 任意代码可用未知命令关闭 resident；未来高危命令落地后会变成任意读内存/解除保护接口。
- **修复方案**：
  1. 未知命令只返回 `YGHV_STATUS_ERROR`，继续 resident。
  2. 初始化时通过安全通道下发一次性随机 cookie；每个命令校验 cookie。
  3. 危险命令额外校验调用者上下文：仅允许受保护进程/agent 的 CR3/RIP 白名单。
  4. 默认 fail-closed，危险命令编译期默认关闭。
- **验证**：fuzz `rax=0..0x1000/随机值` 执行 `vmmcall`，resident 不退出；无 cookie 的 STOP/READ_MEMORY 一律拒绝。

---

### YGHV-008 — NPT 权限 API 假成功

- **级别**：P0
- **维度**：安全
- **位置**：`hv/npt_core.c:20-22`
- **证据**：`npt_set_page_perm()` / `npt_set_page_perm_range()` 直接 `return STATUS_SUCCESS`；`npt_translate()` 返回 0。
- **影响**：句柄保护、执行 trap、进程保护会“认为已保护”，实际没有保护。
- **修复方案**：
  1. 实现 `npt_split_2mb_to_4kb()`、真实权限修改、TLB flush。
  2. 未实现前返回 `STATUS_NOT_IMPLEMENTED`。
- **验证**：对测试页去掉 write 后 guest 写触发 NPF；恢复后写成功；`npt_translate` 返回与页表一致。

---

### YGHV-009 — NPF 事件注入 VALID 位写错

- **级别**：P0
- **维度**：功能正确性
- **位置**：`hv/vmexit.c:9,55-56`
- **证据**：`#define SVM_EVENTINJ_VALID (1ULL << 11)`；EVENTINJ valid 位应为 bit31，bit11 是 error-code valid。
- **影响**：NPF 注入的 #PF 实际无效，guest 重复触发同一访问，形成 NPF 风暴。
- **修复方案**：
  - `#define SVM_EVENTINJ_VALID (1ULL << 31)`
  - `#define SVM_EVENTINJ_ERROR_VALID (1ULL << 11)`
  - 注入 #PF 时同时使用两个位。
- **验证**：制造一次受控 NPF，guest 应收到一次 #PF，而不是无限 NPF。

---

### YGHV-010 — ASID/TLB/PAT 未初始化

- **级别**：P0
- **维度**：功能正确性
- **位置**：`hv/svm_core.c:234-329`，`hv/common/vmcb.h:27-28`
- **证据**：`guest_asid`、`tlb_control` 定义后从未赋值；NPT 启用后 guest PAT 未初始化。
- **影响**：ASID 0 与 host 保留冲突；权限变更无 TLB flush；guest 内存类型异常。
- **修复方案**：
  1. `guest_asid = core_id + 1`。
  2. 首次 VMRUN 和 NPT 权限变更时设置 `tlb_control` flush。
  3. 镜像 host PAT 到 guest PAT，并明确 MMIO 页的缓存策略。
- **验证**：修改 NPT 权限后 guest 旧翻译立即失效；ASID 非 0；多核 ASID 不重复。

---

## 4. P1 高问题详录

### YGHV-011 — GDT 描述符解析错误

- **级别**：P1
- **位置**：`hv/svm_core.c:203-231`
- **问题**：`desc[1]` 是下一条 GDT 项，不是当前项高半；attrib/limit/base 全错；TR/LDTR 64 位系统描述符高 32 位 base 未读；边界检查 off-by-one。
- **影响**：guest segment state 错误，VMRUN 一致性失败或 guest 立即异常。
- **修复**：按单个 qword 描述符解析；系统段读取第二 qword 的高 32 位 base；边界改为 `selector + 7 > gdt_limit`。
- **验证**：解析结果与 WinDbg `dg selector` 输出一致。

### YGHV-012 — 多核 DPC resident loop 不可安全启用

- **级别**：P1
- **位置**：`hv/multi_core.c:6-25`，`hv/svm_core.c:544-559`，`build.bat:67-86`
- **问题**：`multi_core.c` 不在 build；DPC 在 DISPATCH_LEVEL 调 resident loop；KDPC 分配不释放；IPI 回调未接线；`svm_core_ipi_set_npt` 可能修改正在运行的 VMCB。
- **影响**：启用后 DPC_WATCHDOG/调度器损坏/UAF。
- **修复**：resident loop 移到 per-core 系统线程；KDPC 生命周期由系统或静态 per-core 管理；VMCB 修改只在停止或 VMEXIT 上下文。

### YGHV-013 — 全局状态无同步/无 rundown

- **级别**：P1
- **位置**：`hv/svm_core.c:9-10,397-416,552-569`，`hv/npt_core.c:7-23`，`hv/main.c:7`
- **问题**：`g_vcpus/g_npt/g_cache` 跨 PASSIVE/DPC/VMEXIT 访问，无锁无引用计数；cleanup 直接 free；无 stop/wait 顺序。
- **修复**：建立生命周期状态机；所有 free 前必须 stop_all + wait_all；cleanup 使用 IPI 恢复每核 MSR。

### YGHV-014 — SVM/NPT CPUID 检测硬编码信任

- **级别**：P1
- **位置**：`hv/svm_core.c:98-121`
- **问题**：`cpu_has_svm()` / `cpu_has_npt()` 直接 `return 1`；真实检测被 `#if 0`。
- **影响**：无 SVM、BIOS 关闭 SVM、`VM_CR.SVMDIS` 锁定或 Intel CPU 上加载即崩。
- **修复**：恢复真实 CPUID 检测；检查 `VM_CR.SVMDIS`；失败时 DriverEntry 安全返回。嵌套信任模式做成编译开关。

### YGHV-015 — MSR 读写语义错误，MSRPM 注释/初始化反向

- **级别**：P1
- **位置**：`hv/vmexit.c:127-138`，`hv/svm_core.c:156-168`
- **问题**：MSRPM/IOPM bit=1 表示拦截，注释写反；handler 不按 `exitinfo1` 区分读/写；无安全 MSR 过滤。
- **修复**：修正注释；按 `exitinfo1 & 1` 分流；`EFER/VM_CR/VM_HSAVE/SMBASE` 等读虚拟化、写丢弃/告警。

### YGHV-016 — CPUID 隐身死代码且不完整

- **级别**：P1
- **位置**：`hv/vmexit.c:95-119`，`hv/svm_core.c:241`
- **问题**：未设置 `INTERCEPT_CPUID`；未清 `CPUID.1:ECX.bit31`；hypervisor leaf 行为与真实 CPU 不一致。
- **修复**：启用 CPUID 拦截；清 hypervisor bit；leaf 行为与“无 hypervisor”模型一致；评估 TSC offset/RDTSC 拦截。

### YGHV-017 — 调试日志/池标签泄露 HV 与物理地址

- **级别**：P1
- **位置**：`hv/common/debug.h:6,9-11`，各 `.c` 顶部 `#define YGHV_DEBUG_LOG`，`hv/svm_core.c:327,438,557`
- **问题**：Release 也打印 `[YuanGuardHV]` 与物理地址；池 tag `'vhGY'` 独特。
- **修复**：Release 关闭日志；不打印物理地址；使用常见 tag 或无 tag contiguous；hot path 不 DbgPrint。

### YGHV-018 — 驱动生命周期不完整，loader_stealth 死代码且高风险

- **级别**：P1
- **位置**：`hv/main.c:11-53`，`hv/loader_stealth.c:62-66`，对照 `hv/min_drv.c:8-17`
- **问题**：正式驱动无 `DriverUnload`；`yghv_loader_stealth()` 无调用者；若启用又无锁遍历 `PsLoadedModuleList`、手写 LDR 布局。
- **修复**：明确常驻/可卸载语义；可卸载则补 `DriverUnload` 与控制面；常驻则移除 loader_stealth；保留 stealth 需加锁和版本校验。

### YGHV-019 — 测试证书/文档指导弱化驱动签名校验

- **级别**：P1
- **位置**：`yuanguard_test.cer`，`HANDOFF.md:198-201,206,264`
- **问题**：建议 `testsigning` + `nointegritychecks` + CI 注册表；超出测试签名必要范围。
- **修复**：只保留 testsigning；删除 `nointegritychecks`/CI 弱化步骤；私钥离线；发布走正规签名。

---

## 5. P2/P3 问题详录

### YGHV-020 — NPT 缓存全局单例、错误路径泄漏、映射洞（P2）

- **位置**：`hv/npt_core.c:7-23`，`hv/main.c:36-37`
- **问题**：`g_cache` 全局单例与 `npt_mgr_t*` API 语义冲突；`pd_va[]` 只缓存 `p4==0`；失败 `continue` 仍计数；`npt_init` 中途失败不自清理；`main.c` 失败路径漏 `npt_cleanup`。
- **修复**：缓存并入 `npt_mgr_t`；cleanup 遍历真实页表；失败返回错误；`main.c` 失败路径补 `npt_cleanup`。

### YGHV-021 — guest RSP 使用 host stack（P2）

- **位置**：`hv/svm_core.c:313-314`
- **问题**：guest 与 hypervisor 共用 4 页栈。
- **修复**：测试 guest 使用独立栈；host exit stack 与 guest stack 分离；加 guard page。

### YGHV-022 — build.bat 工程问题（P2）

- **位置**：`build.bat:55-90`
- **问题**：漏编译 `multi_core.c`；`min_drv/stub/test_drv` 都定义 `DriverEntry`；无 clean/PDB/签名；`/GS-`；`/OPT:NOREF` 链入死代码；WDK 版本自动取最后一个。
- **修复**：分 target 构建；加 clean/PDB/签名；移除 `/GS-`；固定/校验 WDK 版本；死代码不进入正式镜像。

### YGHV-023 — stub/假成功/死代码过多（P2）

- **位置**：`hv/svm_core.c:418-427,473-476`，`hv/svm_trampoline.S:133-153`，`hv/npt_core.c:20-22`，`hv/pool/*`
- **修复**：未实现 API 返回 `STATUS_NOT_IMPLEMENTED`；`ud2` stub 不链接；无用 pool/reference 文件移出正式构建。

### YGHV-024 — 仓库卫生与第三方二进制（P2）

- **位置**：`hv/common/*.bak`，`kd_*.log`，`bin/`，`hv/SymbolicAccessKM.lib`
- **修复**：清理日志/历史产物/`.bak`；加 `.gitignore`；删除或审计 `SymbolicAccessKM.lib`。

### YGHV-025 — 测试与 mod 层为空，PLAN 漂移（P2）

- **位置**：`tests/`，`mod/src/main/java`，`mod/src/main/resources`，`PLAN.md`
- **问题**：PLAN 中 `tests/run_static.ps1` 等不存在；Java 层无源码；当前无任何可重复 smoke。
- **修复**：先补最小验证：加载→心跳→停止→卸载脚本；再补静态检查脚本。

### YGHV-026 — 风格/返回值/日志级别不统一（P3）

- **位置**：`hv/npt_core.c`，`hv/svm_core.c`，`hv/vmexit.c`，`hv/common/debug.h:9-11`
- **修复**：展开单行实现；统一 NTSTATUS；MSR helper 去重；日志分级。

### YGHV-027 — Intel VMX 遗留文件混入（P3）

- **位置**：`hv/common/msr.h`, `crx.h`, `cpuid.h`, `segment.h`, `hv/reference_*`
- **修复**：AMD SVM 项目只保留 SVM 所需头；VMX 参考移到 `reference/`。

### YGHV-028 — control plane 枚举、PLAN、代码漂移（P3）

- **位置**：`hv/common/control_plane.h:4-15`，`PLAN.md`
- **问题**：头文件写“No user-mode control transport”，但设计又规划 VMMCALL 替代 IOCTL；`0xF0` 在不同文档中含义漂移。
- **修复**：以一份协议文档为准；命令 ID、参数寄存器、返回值、安全模型统一。

---

## 6. 推荐修复阶段

### Phase R0：先能活着跑一次 VMRUN

1. 修 YGHV-001（VMCB 布局）。
2. 修 YGHV-003（trampoline）。
3. 修 YGHV-004（RIP 推进/返回值）。
4. 修 YGHV-002（恢复真实 init）。
5. 修 YGHV-010（ASID/TLB）。
6. 修 YGHV-005（cleanup）。
7. 修 YGHV-009（NPF EVENTINJ）。

验收：单核 guest 执行 `vmmcall` 一次，host 返回 OK，随后 guest `ud2` 触发受控停止；加载/失败/卸载路径不 BSOD。

### Phase R1：安全地基

1. 修 YGHV-006（NPT 最小权限 + HV 私有页剔除）。
2. 修 YGHV-008（权限 API 真实实现）。
3. 修 YGHV-007（VMMCALL 认证/fail-closed）。
4. 修 YGHV-015/YGHV-016（MSR/CPUID 安全语义）。
5. 修 YGHV-017（日志/tag 泄露）。

验收：guest 内核不能定位/读写 HV 私有页；未知/未认证 VMMCALL 不能停保护；权限修改即刻生效。

### Phase R2：多核与生命周期

1. 修 YGHV-011/YGHV-014（状态准备正确性）。
2. 修 YGHV-012/YGHV-013（多核与 rundown）。
3. 修 YGHV-018（DriverUnload/常驻语义）。
4. 修 YGHV-020/YGHV-021（NPT 生命周期、栈隔离）。

验收：多核启动/停止/卸载可重复；无 KDPC/池/contiguous 泄漏；无 DPC_WATCHDOG。

### Phase R3：工程化

1. 修 YGHV-022~YGHV-028。
2. 建立 smoke/static 测试。
3. 清理仓库与签名流程。

---

## 7. 最小验证清单

- [ ] 编译期 VMCB 偏移断言全部对齐 APM。
- [ ] `sc start` 后服务不卡在 START_PENDING。
- [ ] guest `vmmcall` 只触发一次 `SVM_EXIT_VMMCALL`。
- [ ] handler 返回后 guest RIP == 原 `next_rip`。
- [ ] `STOP` 命令必须带有效 cookie；未知命令不停止 resident。
- [ ] 关闭日志后无 `[YuanGuardHV]` 横幅，无物理地址输出。
- [ ] guest 扫描 `'vhGY'` 无可读写命中。
- [ ] NPT 权限修改后旧 TLB 翻译失效。
- [ ] 正常卸载、init 失败、NPT 失败三条路径无 bugcheck、无泄漏。

---

## 8. 附：当前构建清单核对

`build.bat:67-86` 实际编译/链接：

- `main.c`
- `svm_core.c`
- `npt_core.c`
- `vmexit.c`
- `vmmcall.c`
- `loader_stealth.c`
- `svm_trampoline.S`

未编译但存在于 `hv/`：`multi_core.c`、`min_drv.c`、`stub.c`、`test_drv.c`、`reference_*`、`pool/*.cpp`。这意味着：

- `multi_core.c` 当前是死代码，但头文件仍声明多核 API，容易误导。
- `loader_stealth.c` 被链接却无调用者，且 `/OPT:NOREF` 会保留。
- `min_drv.c/stub.c/test_drv.c` 若误入同一 target 会造成 `DriverEntry` 重复定义。
