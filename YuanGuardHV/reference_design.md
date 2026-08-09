## Context

YuanGuard 当前是标准 Windows 内核驱动，通过 `ObRegisterCallbacks`、设备对象 IOCTL、InfinityHook SSDT Hook 实现进程保护。隐形瓶颈在于 Guest 操作系统中留下的 PsLoadedModuleList 条目、设备对象、OB Callback 表条目等不可避免的痕迹。

AMD-V 提供 SVM（Secure Virtual Machine）和 NPT（Nested Page Table），使保护逻辑运行于 VMX Root，Guest 完全无感知。当前系统（Ryzen 5 5500、VBS 关闭、Secure Boot 关闭）是部署独立 Hypervisor 的理想环境。

约束：
- Windows 10/11 x64、Forge 1.20.1、Minecraft 1.20.1
- AMD Ryzen/EPYC 处理器（SVM+NPT 必需）
- 管理员权限 + VBS 关闭（VBS 开启时自动降级）
- 性能预算：总 CPU 开销 <5%（正常游戏不受影响）

## Goals / Non-Goals

**Goals:**
- 215 项保护功能全部可实现、全部可配置开关/阈值/策略
- 隐形评分 ★★★★（无 PsLoadedModuleList、无设备对象、无 OB Callback）
- 自卸载 Loader <10ms 加载窗口
- VMMCALL 通信协议替代 IOCTL
- VBS 开启时自动降级用户态纯监控
- 所有功能支持热加载配置（无需重启 MC）
- 预设模板一键切换

**Non-Goals:**
- 不实现 Intel VT-x 版本（本次仅 AMD-V）
- 不实现 UEFI 预启动 Loader（B-2 路线）
- 不实现 SMM 方案
- 不实现物理 DMA 攻击/防护
- 不修改现有 YuanGuard 项目

## Decisions

### D1: AMD-V SVM/NPT 作为唯一虚拟化后端

**选择**: AMD-V（SVM + NPT + VMMCALL），不实现 Intel VT-x 兼容。

**理由**: 目标系统为 AMD Ryzen 5 5500。AMD-V 指令集与 Intel VT-x 原理相同但指令不同：VMRUN 替代 VMLAUNCH/VMRESUME，VMCB 替代 VMCS，NPT 替代 EPT，VMMCALL 替代 VMCALL。实现单一后端降低复杂度，后续可追加 Intel 支持。

**替代方案**: 双后端（AMD-V + VT-x）→ 增加 40%+ 代码量，一期不值得。

### D2: 自卸载 Loader (B-1 路线)

**选择**: 临时 SCM 服务加载驱动 → 驱动初始化 Hypervisor → 自删除 SCM 键 → 脱 PsLoadedModuleList → 驱动返回释放镜像。运行期仅剩 NonPagedPool 中的代码和数据。

**理由**: 无需 UEFI 签名（B-2）、无需寄生 Hyper-V（B-3）、不依赖 BYOVD（D）。10ms 检测窗口可接受（ETW Event 6 仍会记录加载事件，但用通用驱动名模糊化）。

**替代方案**: B-2 UEFI 预启动 → 需要固件签名，个人开发者不可行。B-3 Hyper-V 寄生 → 结构未公开，工程不可行。

### D3: VMMCALL 通信协议

**选择**: VMMCALL 指令（opcode `0F 01 D9` on AMD）作为通信原语。RAX=命令ID，RCX=请求GPA，RDX=响应GPA，R8=大小。Host 通过 VMCB 读取 Guest 寄存器，NPT 翻译 GPA→HPA，处理后写回响应。

```
用户态 Agent (Guest):
  mov eax, COMMAND_ID
  mov rcx, guest_va_request
  mov rdx, guest_va_response
  mov r8, request_size
  vmcall
  // Host 处理后 response 已在内存中

Host (VM Exit Handler #VMEXIT(VMMCALL)):
  ExitCode == 0x7B  (SVM_EXIT_VMMCALL on AMD)
  CommandID = VMCB.RAX
  RequestHPA = NPT_Translate(VMCB.RCX)
  // 执行命令
  // 写入 Response
  VMCB.RAX = STATUS
  VMRUN  (resume Guest)
```

**命令 ID 空间**:
- 0x00-0x0F: 心跳/状态/版本
- 0x10-0x2F: 句柄保护操作
- 0x30-0x4F: 进程/内存扫描
- 0x50-0x6F: 配置读写
- 0x70-0x8F: 事件/告警查询
- 0x90-0xAF: Hook 管理
- 0xF0-0xFF: 系统命令（关闭/自检/降级）

**替代方案**: CPUID 陷出 → 性能低于 VMMCALL（CPUID 是序列化指令）。IOCTL → 需要设备对象，违背隐形目标。

### D4: NPT 句柄保护替代 OB Callback

**选择**: NPT Execute Trap 在 `nt!ObpCreateHandle` 或 `nt!ObpPreInterceptHandleCreate` 函数入口页。VM Exit 时检查目标进程 PID 和请求的访问掩码，拒绝危险权限组合。

```
1. 定位 Guest 中 ObpCreateHandle 物理地址
2. NPT 页权限设为非可执行
3. 执行流到达 → #VMEXIT(NPF)
4. Host 检查:
   - 目标 == Minecraft PID?
   - 调用者 在信任列表中?
   - 请求掩码 含 TERMINATE/VM_OPERATION/VM_WRITE?
5. 拒绝 → 修改 VMCB.RIP 跳到返回拒绝路径
   放行 → 临时恢复 NPT 权限 → single-step → 恢复陷阱
```

**理由**: 无 OB Callback 表条目，无引用计数，无注册痕迹。EPT/NPT violation 是 CPU 硬件中断，不经 Guest 任何软件路径。

### D5: 模块组织

```
YuanGuardHV/
├── mod/                          # Java 层 (Forge Mod)
│   └── src/main/java/com/yuan/hv/
│       ├── HVMod.java            # @Mod("yuanguard_hv") 入口
│       ├── HvComm.java           # VMMCALL 通信封装
│       ├── HVLoader.java         # 自卸载 Loader 驱动加载
│       ├── HVConfig.java         # 配置管理 (热加载/加密)
│       ├── HVEvents.java         # 事件日志/告警
│       ├── HVCommand.java        # /overlay 命令体系
│       └── HVClient.java         # 联动 API (供 Yuan Mod 调用)
│
├── hv/                           # AMD-V Hypervisor (C)
│   ├── main.c                    # DriverEntry → SVM init → 自卸载
│   ├── svm.c                     # VMRUN/VMCB 管理
│   ├── npt.c                     # NPT 页表 (分配/映射/权限/拆分)
│   ├── vmexit.c                  # VM Exit 分发 (30+ exit codes)
│   ├── vmmcall.c                 # VMMCALL 命令分发
│   ├── protect/                  # 进程保护 (句柄/注入/调试器)
│   ├── hook/                     # 无痕 Hook 引擎
│   ├── scan/                     # 无痕内存扫描
│   ├── filter/                   # 无痕内存过滤
│   ├── mc/                       # Minecraft 专属
│   ├── kernel/                   # 内核监控 (SSDT/IDT/MSR)
│   ├── stealth/                  # 隐形 (CPUID伪造/ETW抑制/代码加密)
│   ├── selfdef/                  # 自身防御
│   ├── config/                   # 配置引擎
│   └── common/                   # AMD-V helpers
```

### D6: NPT 页表管理策略

**选择**: Identity-map 所有 Guest 物理内存（1:1 GPA→HPA），仅对受保护页设置细粒度 NPT 权限。动态分配 NPT 子页表（4KB 粒度 → 2MB 大页 → 拆分 on-demand）。

**内存开销**: 4级 NPT 页表 ~8KB/core 起始 + 每受保护页 ~8 bytes。保护 1000 页 ≈ 8KB 额外开销。

### D7: VBS 兼容降级

**选择**: 启动时检测 VBS 状态（读取 `HKLM\SYSTEM\CurrentControlSet\Control\DeviceGuard\EnableVirtualizationBasedSecurity`）。VBS 开启时：
- 不加载 Hypervisor 驱动
- 退化为纯用户态监控模式
- `/overlay status` 显示 "VBS 开启 - Hypervisor 模式不可用"
- 保留功能: 用户态句柄轮询、ETW 消费、进程 ACL、安全描述符
- 失去功能: 所有 NPT 级防护（句柄拦截、无痕内存、Hook 等）

### D8: 配置系统架构

**选择**: 三层配置存储。
1. 默认值 → 编译时嵌入 Hypervisor（CFG 段）
2. 用户配置 → Guest 加密页（NPT Read Shadow，Guest 读不到明文）
3. 运行时覆盖 → VMMCALL 热写入（仅当前会话有效）

配置分层视图：
```
┌─────────────────────────────────┐
│ 运行时覆盖 (VMMCALL set)        │ ← 优先级最高（/overlay config set）
├─────────────────────────────────┤
│ 用户配置 (加密 Guest 页)        │ ← 持久化（yuanguard.hv.conf）
├─────────────────────────────────┤
│ 预设模板 (silent/.../custom)    │ ← 一键切换基础
├─────────────────────────────────┤
│ 编译时默认值                     │ ← 安全底线
└─────────────────────────────────┘
```

## Risks / Trade-offs

| 风险 | 影响 | 缓解 |
|------|------|------|
| **VBS 开启无法 Hypervisor** | 70%+ 用户无法使用核心功能 | 自动降级用户态模式 + `/overlay status` 明确提示 |
| **AMD-V 仅限 AMD CPU** | Intel 用户无法使用 | 启动检测 → CPU 不兼容 → 用户态降级 + 提示 |
| **NPT 复杂性导致 BSOD** | 开发/测试周期长 | Phase 1 最小可行 Hypervisor → 逐步加功能 |
| **性能开销（VM Exit 频率）** | NPT 陷阱过多 → 游戏掉帧 | 每功能性能预算 + 瞬态 Hook + `/overlay perf` 监控 |
| **#VMEXIT 死循环** | 单次 VM Exit 未正确恢复 → 系统挂起 | 看门狗定时器 + 退出计数上限 → 强制恢复 |
| **Loader 10ms 窗口被检测** | ETW Event 6 记录驱动加载 | 通用驱动名模糊化 + 自删 SCM 键 + 退路: 接受此残余 |
| **与反作弊软件冲突** | Vanguard/FaceIt/EAC 也可能虚拟化 | 检测已存在 Hypervisor → 优雅降级 |
| **MC 更新兼容性** | 内核符号地址变化 | 签名扫描定位关键函数 (pattern matching) |

## Open Questions

- 是否需要支持 Windows 11 24H2（VBS 默认强制开启的版本）？→ 影响降级策略优先级
- 是否开源？→ 影响代码混淆/加密方案强度选择
- 215 项中哪些是 MVP（Phase 1 必须），哪些可以后续追加？→ 由 tasks.md 定义
