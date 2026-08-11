# Phase 3 进程保护设计（YuanGuardHV）

> 状态：已确认（修订版：终止/句柄保护改为补丁 stub + VMMCALL 决策）
> 日期：2026-08-11
> 基线：v26（commit 36a719a）
> 关联文档：`docs/YUANMOD_HANDOFF_CURRENT.md`、`docs/TASKS.md`、`docs/SESSION_20260811.md`

## 1. 背景与目标

YuanGuardHV 是 AMD-V SVM/NPT 隐形 Hypervisor，目标是替代原 YuanGuard 内核驱动，把 Minecraft/Forge 进程保护逻辑放到虚拟化层。当前已具备稳定基线和 NPF 注入能力：

- v22：多核 VMRUN + 16GB NPT identity map + VMMCALL 认证心跳稳定。
- v25：NPT `translate` / `perm-range` / `split` API 单测 PASS，修复大页 PFN 位域 bug。
- v26：NPT 权限剔除 → NPF 触发 → 恢复映射 → guest 重执行，双核心跳通过。

本设计定义 Phase 3 第一版进程保护：内存页写保护、进程终止保护、句柄保护，全部在虚拟化层实现，并先以驱动内自动化测试验证。

## 2. 范围

### 2.1 本期包含

- 单目标进程保护（一次一个 PID，后续可扩展多目标）。
- 内存页写保护：只保护调用方指定的关键页/区域，只拦截写。
- 进程终止保护：拦截非目标进程发起的终止目标进程请求。
- 句柄保护：拦截非目标进程打开目标进程句柄的请求。
- VMMCALL 测试协议扩展：`SET_TARGET`、`ADD_PAGE`、`REMOVE_PAGE`、`START_PROTECT`、`STOP_PROTECT`、`GET_STATE`。
- 常驻保护模式：测试通过后 residents 持续运行，保护一直生效，直到卸载。
- 驱动内自动化测试：加载时四段测试，KD 日志输出 PASS/FAIL。

### 2.2 本期不含

- 用户态 IOCTL/设备对象通道（下一阶段再做正式对外接口）。
- 多目标进程列表。
- 完整隐形（CPUID/ETW 等全量处理，后续阶段）。
- Java 层（Forge mod、`/overlay` 命令）。
- ring0 内存写硬拦截/影子页静默拒绝（后续阶段）。

## 3. 威胁模型与策略

威胁模型：外部进程（用户态）和 ring0 工具都可能对目标进程发起内存篡改、终止或句柄操作。目标进程自身的行为不受影响。

| 场景 | 访问者 | 决策 |
|---|---|---|
| 受保护页写 | 目标进程（CR3=目标） | 放行，单步后重新加锁 |
| 受保护页写 | 外来用户态（CPL3） | 注入 #PF，调用失败 |
| 受保护页写 | ring0（CPL0） | 放行 + 记录（第一版不硬拦） |
| 终止目标进程 | 目标进程调用 | 放行 |
| 终止目标进程 | 非目标（含管理员/SYSTEM） | 模拟返回 `STATUS_ACCESS_DENIED` + 记录 |
| 打开目标句柄 | 目标进程调用 | 放行 |
| 打开目标句柄 | 非目标（含管理员/SYSTEM） | 模拟返回 `STATUS_ACCESS_DENIED` + 记录 |

读和执行受保护页不产生 NPT 陷阱，不拦截。

## 4. 架构

### 4.1 新模块 `protect.h` / `protect.c`

保护状态机与策略核心，职责单一、不依赖具体 VCPU 实现：

- `yghv_protect_target_t`：目标 `PID`、`EPROCESS*`、目标 `CR3`、启用标志（内存/终止/句柄）。
- 受保护页表：`{GPA, 目标进程 VA, 标志}`。
- `ADD_PAGE` 时按目标 CR3 遍历 guest 页表，把目标进程 VA 翻译为 GPA；目标 VA 当前无映射则命令失败。
- 决策接口：`is_target_cr3()`、`on_npf_write()`、`on_hook_query()`、`install_hook()/remove_hook()`、`arm()/disarm()`。
- 保存被 hook 函数开头 16 字节原值，trampoline 用原字节执行被 hook 函数本体。

### 4.2 `control_plane.h` / `vmmcall.c`

扩展 VMMCALL 命令，全部要求 `g_vmmcall_auth_cookie` 认证：

| 命令 | 作用 |
|---|---|
| `SET_TARGET` | 设置目标 PID，解析 EPROCESS/CR3 |
| `ADD_PAGE` | 添加受保护页（目标 VA → 按目标 CR3 遍历 guest 页表翻译为 GPA） |
| `REMOVE_PAGE` | 移除受保护页并恢复权限 |
| `START_PROTECT` | 进入常驻保护 |
| `STOP_PROTECT` | 停止保护，恢复全部页 |
| `GET_STATE` | 查询目标/页数/保护状态 |
| `HOOK_QUERY` | trampoline 调用：上报函数 ID，由 Hypervisor 决定放行/拒绝 |

### 4.3 `vmexit.c` NPF 分支扩展

- 受保护页写 NPF → 调 `on_npf_write()`。
- 被 hook 函数页写 NPF → 按内存写保护策略处理（防篡改补丁）。
- `HOOK_QUERY` VMMCALL → 调 `on_hook_query()` 决定放行/拒绝。
- 保留现有 `g_npt_test_active` 测试路径和早期 NPF 路径。
- 新增 `SVM_EXIT_EXCEPTION_DB` 分支，用于单步重放后重新加锁。

### 4.4 常驻模式

四段测试全部 PASS 后，`vmmcall_dispatch` 不再因心跳计数停止 residents；NPF 持续生效。`DriverUnload` 先 `STOP_PROTECT` 恢复所有页，再停 residents、清理 NPT。

## 5. 数据流

### 5.1 内存页写保护

1. 受保护页在 NPT 中保持 `present=1, writable=0`；只有写触发 NPF，读/执行零开销。
2. NPF 到达：读取 `exitinfo2`（GPA）、`info1`（WRITE 位）、`vcpu->vmcb->state.cr3`（访问者 CR3）、`CPL`。
3. 决策：
   - 目标 CR3：恢复 `writable=1`，设置 `RFLAGS.TF`，指令执行后触发 #DB；在 #DB 分支把页设回只读并清 TF。
   - 外来用户态写：注入 #PF（写错误码），调用者指令失败。
   - ring0 写：放行（单步重放后重新加锁）+ 节流记录。
4. 每次放行都重新加锁，保护持续。

### 5.2 补丁 stub（终止/句柄）

AMD SVM 没有近返回 `RET` 拦截，因此终止/句柄保护采用补丁 stub + VMMCALL 决策：

1. 定位 `PspTerminateProcess` / `ObpCreateHandle` 后，保存函数开头 16 字节，把开头改为 `E9 rel32` 跳转到驱动内 trampoline。
2. trampoline 以 `HOOK_QUERY` VMMCALL（带认证 cookie）向 Hypervisor 上报函数 ID；决策：
   - 目标进程调用 → `RAX=0`，trampoline 执行保存的原字节，再跳回 原函数+patch 长度，函数正常执行。
   - 非目标调用 → `RAX=STATUS_ACCESS_DENIED`，trampoline 直接返回，调用方收到拒绝。
3. 函数页 NPT 设 `writable=0` 防篡改：任何进程改写函数开头都触发 NPF，按内存写保护策略处理。
4. 卸载时恢复原字节并恢复页可写。

### 5.3 控制命令流

加载 → 测试代码 VMMCALL 下发 `SET_TARGET/ADD_PAGE/START_PROTECT` → 常驻 → 卸载时 `STOP_PROTECT` + 恢复权限 + 停 residents。

## 6. 错误处理与稳定性

- 配置失败回滚：`SET_TARGET/ADD_PAGE/START_PROTECT` 任一步失败，恢复已改 NPT 页并返回错误，不留半状态。
- 单步重放：#DB 重新加锁失败时强制恢复只读并记录，带最大重试次数，防止无限 NPF/#DB 循环。
- 补丁 stub：patch 前校验函数页可写、patch 长度足够，保存原字节；patch 失败则禁用对应功能。
- `HOOK_QUERY` 认证失败按拒绝处理，trampoline 不执行原字节。
- 卸载恢复原字节前校验哈希，被外部篡改也恢复保存的原字节并记录。
- 函数定位：`PspTerminateProcess` / `ObpCreateHandle` 是未导出函数，按当前 VM 镜像（19045.2965）模式定位；找不到则只禁用该功能并记录。
- 目标退出/PID 复用：按 `EPROCESS` 指针校验；目标退出自动解除保护并恢复页权限。
- 日志节流：ring0 放行记录和拒绝事件限速。
- 卸载恢复：`DriverUnload` 恢复全部受保护页权限、关闭执行陷阱与 RET 拦截，再停 residents。

## 7. 测试与验收

驱动加载时自动执行四段测试：

1. 回归：v26 的 NPT API 单测 + NPF 触发/恢复 PASS。
2. 策略矩阵：合成访问者（目标 CR3 / 外来用户态 / ring0）验证决策输出。
3. 真实写陷阱：目标=System，保护测试页，当前上下文写触发真实 NPF，验证放行→单步→重新加锁，读回页表项确认恢复只读。
4. 执行陷阱：确认函数入口页 NX 已设；合成 EXEC NPF 验证外来调用返回 `ACCESS_DENIED`、目标调用走放行路径，不真执行系统函数。

四段全 PASS 后进入常驻空转：心跳继续但不按 10000 停止；VM 空转数分钟确认不崩、可卸载、可重载。

验收标准：

- KD 日志四段全 PASS。
- 常驻期间 VM 不蓝屏、不卡死。
- `unload_driver.ps1` 干净卸载，恢复所有 NPT 权限。
- 再次加载正常。

## 8. 实施顺序

每个阶段独立构建 vNN，VM 验证通过后再进下一阶段，并更新交接记录：

1. 阶段 1：内存页写保护（NPF 决策 + 单步重放 + 策略矩阵 + 真实写陷阱测试）。
2. 阶段 2：进程终止保护（执行陷阱 + RET 重新加锁 + 拒绝模拟）。
3. 阶段 3：句柄保护（复用执行陷阱框架，接入 `ObpCreateHandle`）。

## 9. 风险与开放项

- 补丁 stub 会修改内核代码字节，存在被完整性检测发现的风险；NPT 写保护与后续隐形可降低。
- stub 方案无法阻止攻击者直接调用函数原始实现（inline hook 通病），后续可用影子 NPT 加强。
- `PspTerminateProcess` / `ObpCreateHandle` 地址定位依赖镜像版本，后续需要更稳的定位策略。
- 目标自身写保护的单步重放会产生额外 VMEXIT，性能影响需实测。
- ring0 内存写只能记录放行；真正的静默拒绝需要影子页/COW，属后续阶段。
