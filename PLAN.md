# YuanGuardHV — 项目初始化与实施计划

## 0. 进度更新（2026-08-11）

- 已完成并合入 main：v32 IOCTL 配置通道、v33 常驻真实保护与 hook stub allow/deny、v34 NPT 共享状态加锁、v37 目标进程生命周期、v38 hook 加固、v39 控制面 CPL/CR3、v40/v41 隐形基础 CPUID、v42 仓库整理（详见 `docs/YUANMOD_HANDOFF_CURRENT.md`）。
- 定档：常驻模式为合成 resident guest 测试基架；真实系统 hook、整机级隐形、Java 层、R1 私有页/NPT 自剔除/默认 NX、MSR/IO 隐身仍未做（后四项需裸机/KVM）。
- 后续：实机验证（裸机/KVM）→ Java 层 → 整机级隐形立项 → tests/mod/vm 目录补齐。

## 项目概述

基于 AMD-V SVM/NPT 的隐形 Hypervisor，替代原 YuanGuard 内核驱动架构，将进程保护逻辑提升至 VMX Root 层。

- **目标平台**: Windows 10/11 x64, AMD Ryzen/EPYC (SVM+NPT), Forge 1.20.1, Minecraft 1.20.1
- **目标环境**: Ryzen 5 5500, VBS 关闭, Secure Boot 关闭
- **来源设计**: `D:\mcmodwork\openspec\changes\yuanguard-hypervisor/` (design.md, tasks.md, proposal.md, specs/)
- **可复用代码库**: `UnrealVTDbg` (Intel VT-x 调试器, 位于 `C:\Users\Administrator\Desktop\2\UnrealVTDbg..zip`)

---

## 一、目录结构规划

```
D:\yuanguard\
├── YuanGuardHV\                  # 主项目
│   ├── mod\                      # Java 层 (Forge 1.20.1 Mod)
│   │   ├── build.gradle
│   │   ├── settings.gradle
│   │   ├── gradle.properties
│   │   └── src\main\
│   │       ├── java\com\yuan\hv\
│   │       │   ├── HVMod.java            # @Mod("yuanguard_hv") 入口
│   │       │   ├── HVLoader.java         # SCM 驱动加载/卸载
│   │       │   ├── HVCommand.java        # /overlay 命令
│   │       │   └── HVConfig.java         # 配置管理
│   │       └── resources\
│   │           ├── yuanguard_hv.sys       # 嵌入驱动 (构建产物)
│   │           ├── yuanguard_hv.sha256
│   │           └── META-INF\mods.toml
│   │
│   ├── hv\                        # AMD-V Hypervisor (C 内核驱动)
│   │   ├── CMakeLists.txt         # WDK MSBuild 或 CMake 构建
│   │   ├── main.c                 # DriverEntry → SVM init → 自卸载
│   │   ├── svm_core.c             # VMRUN/VMCB/VMSAVE/VMLOAD 管理
│   │   ├── svm_trampoline.S       # asm: VMRUN 循环, VMSAVE/VMLOAD, 上下文切换
│   │   ├── npt_core.c             # NPT 页表: 分配/identity-map/拆分/权限
│   │   ├── vmexit.c               # VM Exit 分发 (30+ exit codes)
│   │   ├── vmmcall.c              # VMMCALL 命令处理
│   │   ├── loader_stealth.c       # 自卸载: 脱链表/删SCM/释镜像
│   │   ├── stealth.c              # 隐形: CPUID伪造/ETW抑制/随机PoolTag
│   │   ├── debug.c                # DbgPrint 日志
│   │   ├── common\
│   │   │   ├── svm_vcpu.h         # VCPU 结构体 (VMCB/HSave/状态)
│   │   │   ├── vmcb.h             # VMCB 位定义 + 偏移量
│   │   │   ├── npt.h              # NPT entry 结构 + 常量
│   │   │   ├── control_plane.h    # VMMCALL 协议命令ID
│   │   │   ├── msr.h              # MSR 地址常量 + rdmsr/wrmsr inline
│   │   │   ├── crx.h              # CRx 读写 + 拦截常量
│   │   │   ├── cpuid.h            # CPUID leaf 常量
│   │   │   ├── pool.h             # 物理内存池 (from UnrealVTDbg)
│   │   │   ├── spinlock.h         # 自旋锁 (from UnrealVTDbg)
│   │   │   ├── gdt_idt.h          # GDT/IDT 工具 (from UnrealVTDbg)
│   │   │   ├── segment.h          # 段选择子操作 (from UnrealVTDbg)
│   │   │   ├── pe_parser.h        # PE 解析 (from UnrealVTDbg Ring0)
│   │   │   ├── utils.h            # 通用工具函数
│   │   │   └── svm_defs.h         # AMD SVM MSR/指令常量
│   │   └── pool\
│   │       ├── pool_manager.c     # NonPagedPool 物理内存管理器
│   │       └── pool_manager.h
│   │
│   ├── tests\                     # 验证脚本
│   │   ├── resident_lifecycle_static.ps1   # 常驻生命周期静态检查
│   │   ├── task3_cleanup_static.ps1        # 清理路径静态检查
│   │   ├── task4_cross_path_static.ps1     # cross-path VMRUN 静态检查
│   │   └── run_static.ps1                  # 批量运行所有静态检查
│   │
│   ├── vm\                        # VM 测试辅助
│   │   └── diag_serial.ps1        # VMware 串口诊断脚本
│   │
│   └── build.bat                  # 一键构建脚本
│
├── reference\                     # 参考资料 (只读)
│   └── (UnrealVTDbg 解压后的参考代码, 不直接编译)
│
└── PLAN.md                        # 本文件
```

---

## 二、UnrealVTDbg → YuanGuardHV 复用映射

### 可直接复制的文件 (保持 MIT/原作者许可)

| UnrealVTDbg 源文件 | 目标位置 | 修改 |
|---|---|---|
| `VT_Driver/poolmanager.cpp/h` | `hv/pool/pool_manager.c/h` | WDK 适配, NonPagedPool → 物理页池 |
| `VT_Driver/spinlock.cpp/h` | `hv/common/spinlock.h` | 改为单头文件 inline |
| `Common/Ring0/AllocateMem.h` | `hv/common/pool.h` 合并 | 物理内存分配宏 |
| `Common/Ring0/PE/PE_struct.h` | `hv/common/pe_parser.h` | PE 头结构定义 |
| `VT_Driver/msr.h` | `hv/common/msr.h` | 保持 |
| `VT_Driver/cpuid.h` | `hv/common/cpuid.h` | 保持 |
| `VT_Driver/crx.h` | `hv/common/crx.h` | 保持 |
| `VT_Driver/gdt.cpp/h + idt.cpp/h + segment.h` | `hv/common/gdt_idt.h` + `hv/common/segment.h` | 合并简化 |
| `Common/Ring0/List/MyList.cpp/h` | `hv/common/utils.h` 内联 | 简化 |
| `Common/Ring0/String/*` | `hv/common/utils.h` 内联 | 简化 |
| `Common/Logger/Logger.cpp/h` | `hv/debug.c` 适配 | DbgPrint 封装 |
| `Common/Ring0/SymbolicAccessKM.lib` | `hv/` 链接 | 符号解析 |
| `ia32-doc/out/ia32.h` | `hv/common/svm_defs.h` | 提取 AMD SVM 部分 |
| `mod/build.gradle` (来自 YuanGuard) | `mod/build.gradle` | 适配 HV 包名 |

### 需要 VT-x → SVM 重写的核心文件

| UnrealVTDbg 原文件 | 目标文件 | 重写原因 |
|---|---|---|
| `vmm.cpp/h` (VMXON/VMPTRLD/VMLAUNCH) | `svm_core.c` | VMRUN/VMSAVE/VMLOAD 序列完全不同 |
| `vmcs.cpp/h` (VMREAD/VMWRITE) | `svm_core.c` 内联 | VMCB 内存直写, 不需 VMREAD/VMWRITE 指令 |
| `hypervisor_routines.cpp` (__vmx_*) | `svm_trampoline.S` | 全部 `__vmx_*` → `__svm_*` 或直接 asm |
| `hypervisor_gateway.cpp` (VMLAUNCH 入口) | `svm_trampoline.S` 合并 | SVM 是 VMRUN, 入口不同 |
| `vmexit_handler.cpp` (VT-x exit reason) | `vmexit.c` | Exit code 映射完全不同 |
| `vmcall_handler.cpp` (VMCALL) | `vmmcall.c` | VMMCALL 指令 + 参数寄存器映射 |
| `EPT.cpp/h` (EPT entry bit) | `npt_core.c` | NPT entry 格式不同 |
| `Driver.cpp` (VMM init) | `main.c` | SVM init 流程不同 |
| `vm_context.h` (__vcpu) | `svm_vcpu.h` | VMCB/HSAVE/EFER 等 SVM 特有字段 |
| `vmx.h` (VMX capability MSRs) | `svm_defs.h` | SVM capability CPUID leaves |

### 可参考逻辑但需改写

| 功能 | 参考源 | 改写策略 |
|---|---|---|
| VM Exit 分发模式 | `vmexit_handler.cpp:dispatch_vm_exit()` | 保留 switch-case 结构, 替换 exit codes |
| VMCALL 协议模式 | `vmcall_handler.cpp` + `vmcall_reason.h` | 保留命令分发模式, 替换 VMCALL→VMMCALL |
| NPT identity-map | `EPT.cpp:initialize()` | 保留 4 级页表逻辑, 替换 entry bit layout |
| MSR/CPUID 拦截 | `vmexit_handler.cpp` 中的 handler | 保留拦截模式, 匹配 SVM exit codes |
| CR 访问拦截 | `vmexit_cr_handler` | 保留 MOV CRx 解码逻辑, 适配 SVM exit |
| 每核 VCPU 上下文 | `__readfsbase_u64()` 取 VCPU 指针 | SVM 用 VMCB 直接访问或 GS base |
| 自卸载流程 | `Driver.cpp:Unload()` + `D-encryption` 模式 | 保留脱链/删键逻辑, 适配 SVM devirtualization |

---

## 三、Phase 1 任务分解 (MVP: Hypervisor 核心 + 基础保护)

依据 `openspec/changes/yuanguard-hypervisor/tasks.md` 1.1–1.14 重新编排:

### 1.1 项目骨架初始化 [P0]
- [ ] 创建 `D:\yuanguard\YuanGuardHV\` 目录树
- [ ] 复制 UnrealVTDbg 可复用文件到 `hv/common/` 和 `hv/pool/`
- [ ] 创建 `hv/common/svm_defs.h` (AMD SVM MSR/CPUID/VMCB 偏移常量)
- [ ] 创建 `hv/common/svm_vcpu.h` (svm_vcpu_t 结构体)
- [ ] 创建 `hv/common/vmcb.h` (VMCB 布局 + exit codes)
- [ ] 创建 `hv/common/control_plane.h` (VMMCALL 命令 ID)
- [ ] 创建 `hv/common/npt.h` (NPT entry 结构)
- [ ] 配置构建系统: `build.bat` (WDK clang-cl + MS link)
- [ ] 创建 `mod/` 目录 + ForgeGradle 构建
- [ ] `D:\yuanguard\YuanGuardHV\` 已存在 ✓

### 1.2 AMD-V SVM 初始化 [P0]
- [ ] `svm_core.c`: 实现 `cpu_has_svm()` CPUID 检测
- [ ] `svm_core.c`: 实现 `svm_core_init()` — EFER.SVME=1, 每核 VMCB 分配
- [ ] `svm_core.c`: 实现 `svm_prepare_vcpu()` — 配置 VMCB (CRx shadow, intercept 位图, IOPM/MSRPM)
- [ ] `svm_core.c`: 实现 `svm_alloc_vcpu()` — 4KB 对齐 VMCB/HSAVE/HostVMCB/HostStack
- [ ] `svm_trampoline.S`: 实现 `svm_vmrun_trampoline()` — VMSAVE → VMLOAD → VMRUN → VMSAVE → VMLOAD
- [ ] `svm_core.c`: 实现 `svm_core_run_trampoline_once()` — 单次 VMRUN 测试 (VMMCALL+UD2 guest)
- [ ] 目标: `svm_core_init()` PASS → `svm_core_run_trampoline_once(0)` 单核测试通过

### 1.3 Identity NPT 页表管理 [P0]
- [ ] `npt_core.c`: 实现 NPT PML4/PDPT/PD/PT 四级页表结构
- [ ] `npt_core.c`: `npt_init()` — 分配 PML4, identity-map 全部物理内存 (GPA=HPA, 1:1)
- [ ] `npt_core.c`: `npt_identity_map_range()` — 范围映射, 大页(2MB) 优先
- [ ] `npt_core.c`: `npt_set_page_perm()` — 细粒度权限 (R/W/X/NX)
- [ ] `npt_core.c`: `npt_split_2mb_to_4kb()` — on-demand 拆分
- [ ] `npt_core.c`: `npt_translate()` — GPA→HPA 遍历

### 1.4 VM Exit 基础分发器 [P0]
- [ ] `vmexit.c`: `svm_dispatch_exit()` 主分发 switch-case
- [ ] `vmexit.c`: NPF handler (`VMEXIT_NPF`) — NPT violation 处理
- [ ] `vmexit.c`: VMMCALL handler (委托给 `vmmcall.c`)
- [ ] `vmexit.c`: CPUID handler (`VMEXIT_CPUID`) — 基础透传
- [ ] `vmexit.c`: MSR handler (`VMEXIT_MSR`) — EFER/LSTAR/STAR 选择性拦截
- [ ] `vmexit.c`: CR handler (`VMEXIT_CR0/3/4/8_READ/WRITE`) — MOV CRx 解码
- [ ] `vmexit.c`: INTR handler (`VMEXIT_INTR`) — 中断透传
- [ ] `vmexit.c`: NMI handler (`VMEXIT_NMI`) — NMI 透传
- [ ] `vmexit.c`: fail-closed default — 未知 exit → 停止该核 VMRUN

### 1.5 自卸载 Loader [P0]
- [ ] `main.c`: `DriverEntry` — SCM 临时服务→SVM init→自删→返回
- [ ] `loader_stealth.c`: 脱 PsLoadedModuleList (清零 LIST_ENTRY Flink/Blink)
- [ ] `loader_stealth.c`: ZwDeleteKey 删 SCM 注册表键
- [ ] `loader_stealth.c`: 释放驱动映像内存 (MmFreeDriverInitialization 或等效)
- [ ] `loader_stealth.c`: 清理无设备对象/DOS 符号链接残留
- [ ] 目标: <10ms 加载窗口, 运行期无模块列表条目

### 1.6 VMMCALL 通信协议 [P0]
- [ ] `vmmcall.c`: `vmmcall_dispatch()` 根据 RAX 命令 ID 分发
- [ ] `vmmcall.c`: 心跳 (0x01) — Guest→Host 握手
- [ ] `vmmcall.c`: 状态查询 (0x02) — 返回版本/运行状态/NPT 统计
- [ ] `vmmcall.c`: 内部停止 (0xF0) — 触发 resident 退出循环
- [ ] `control_plane.h`: 命令 ID 枚举定义

### 1.7–1.10 基础保护功能 [P1]
- [x] NPT 句柄保护 (PP-001) — 一版完成：补丁 stub + VMMCALL 决策（AMD 无 RET 拦截，故非 Execute Trap）
- [x] 进程终止保护 (PP-002) — 一版完成：`ZwTerminateProcess` stub + VMMCALL 决策
- [ ] JVM DLL 完整性扫描 — NPT 直读 + SHA-256 对比
- [ ] 句柄枚举 — PspCidTable NPT 遍历

### 1.11 退出清理 [P1]
- [ ] 每核 VMRUN disable → EFER.SVME=0
- [ ] NonPagedPool 覆写清零
- [ ] 所有分配的内存页归还系统

### 1.12 Java 层适配 [P1]
- [ ] `HVMod.java` — `@Mod("yuanguard_hv")` + 事件订阅
- [ ] `HVLoader.java` — JNA SCM load/unload + 嵌入资源提取
- [ ] `HVCommand.java` — `/overlay status/config/events/perf`
- [ ] `HVConfig.java` — 配置热加载

### 1.13 隐形基础包 [P1]
- [ ] `stealth.c`: CPUID 0x40000000 → 返回 "无 Hypervisor"
- [ ] `stealth.c`: ETW 事件抑制
- [ ] `stealth.c`: NonPagedPool 随机 PoolTag
- [ ] 无设备对象 / 无 OB Callback / 无 PsLoadedModuleList

### 1.14 VBS 检测 + 降级 [P1]
- [ ] 检测 `HKLM\...\DeviceGuard\EnableVirtualizationBasedSecurity`
- [ ] VBS 开启 → 不加载驱动 + 用户态监控模式
- [ ] `/overlay status` 显示降级原因

---

## 四、复用组件提取清单

从 UnrealVTDbg 提取到 `D:\yuanguard\YuanGuardHV\hv\` 的具体文件:

### Ring0 公共库 (直接复制, C→C 适配)

```
UnrealVTDbg/Common/Ring0/                 → YuanGuardHV/hv/common/
  ├── AllocateMem.h                       → pool.h (合并)
  ├── PE/PE_struct.h                      → pe_parser.h
  ├── List/MyList.cpp/h                   → utils.h (内联)
  ├── Spinlock/*                          → spinlock.h
  ├── Comms/SerialPort.cpp/h              → debug.c (适配)
  └── SymbolicAccessKM.lib                → hv/SymbolicAccessKM.lib (直接链接)

UnrealVTDbg/VT_Driver/
  ├── poolmanager.cpp/h                   → pool/pool_manager.c/h
  ├── msr.h / cpuid.h / crx.h             → common/ (保持)
  ├── gdt.cpp/h + idt.cpp/h + segment.h    → common/gdt_idt.h + common/segment.h
  ├── spinlock.cpp/h                      → common/spinlock.h (合并)
  └── mtrr.h                              → common/ (参考, 用于物理内存类型)

UnrealVTDbg/Common/Ring0/Hvm/ASM/
  └── AsmCallset.asm                      → 参考 svm_trampoline.S 编写
```

### Ring3 公共库 (Forge Mod Java 侧可参考)

```
UnrealVTDbg/Common/
  ├── Logger/                             → 参考 HVMod 日志
  ├── IPC/                                → 不适用 (我们用 VMMCALL)
  ├── Encrypt/Blowfish/                   → 配置加密可选
  └── FileSystem/                         → 参考
```

### 设计文档参考

```
openspec/changes/yuanguard-hypervisor/
  ├── design.md          → 架构决策 D1-D8
  ├── proposal.md        → 能力矩阵
  ├── tasks.md           → Phase 1-4 任务分解
  └── specs/
      ├── hypervisor-core/spec.md     → HC-001 ~ HC-008
      ├── process-protection/spec.md  → PP-001 ~ PP-011
      ├── traceless-memory/spec.md    → TM-001 ~ TM-005
      ├── anti-tamper/spec.md         → AT-001 ~ AT-010
      ├── kernel-monitor/spec.md      → KM-001 ~ KM-008
      ├── minecraft-defense/spec.md   → MD-001 ~ MD-008
      └── operations-config/spec.md   → OC-001 ~ OC-007
```

---

## 五、关键架构决策 (继承自 design.md)

| 决策 | 内容 | 状态 |
|:---|------|:--:|
| D1 | AMD-V SVM/NPT 唯一后端, 不实现 Intel VT-x | 确认 |
| D2 | 自卸载 Loader (B-1), <10ms 窗口 | 确认 |
| D3 | VMMCALL 通信协议 (RAX=cmd, RCX/DX/R8/R9=params) | 确认 |
| D4 | NPT 句柄保护替代 OB Callback (Execute Trap) | 确认 |
| D5 | 模块组织 (见上目录结构) | 确认 |
| D6 | Identity NPT + 2MB 大页 + on-demand 拆分 | 确认 |
| D7 | VBS 开启自动降级用户态 | 确认 |
| D8 | 三层配置: 默认/加密用户配置/运行时覆盖 | 确认 |

---

## 六、构建与验证流程

```
1. 开发修改源代码 (D:\yuanguard\YuanGuardHV\hv\*.c, *.S, *.h)
2. 运行 build.bat 生成 yuanguard_hv.sys
3. 运行 .\tests\run_static.ps1 静态检查
4. 运行残余扫描 (mailbox/device/NamedSection 关键字)
5. Copy yuanguard_hv.sys → mod/src/main/resources/
6. 生成 yuanguard_hv.sha256
7. 验证 Host + staged + resource hash 三方一致
8. 冷启动 VMware → sc start/stop 验证 → KD 抓取状态
9. 记录最终 artifact SHA-256 和 KD log
```

---

## 七、当前状态

- [x] 项目骨架创建（`hv/` 已建；`mod/`、`tests/`、`vm/` 未建）
- [x] 核心头文件编写（SVM/NPT/VMCB/control plane）
- [x] SVM 初始化 + VMRUN trampoline（单核，10000 轮 VMMCALL 心跳）
- [x] NPT identity-map（16GB + NPF 权限注入）
- [x] VM Exit 基础分发（VMMCALL/NPF 可达；CPUID/MSR/CR 未开拦截）
- [x] 多核 DPC（每核系统线程，双核 10000 轮心跳验证通过）
- [ ] 安全地基（NPT 最小权限/VMMCALL 认证）
- [ ] 隐形（loader_stealth 未编译、CPUID 隐身死代码）
- [x] 保护功能一版（内存写/终止/句柄）完成并合并 main
- [ ] 真实目标进程接入（IOCTL/Java 层）、隐形、JVM 完整性扫描（下一阶段）

---

## 开始执行顺序

**Step 1**: 创建目录结构 + 提取 UnrealVTDbg 复用文件
**Step 2**: 编写 `svm_defs.h` / `svm_vcpu.h` / `vmcb.h` / `control_plane.h` / `npt.h`
**Step 3**: 编写 `svm_core.c` + `svm_trampoline.S` (SVM init + VMRUN 最小闭环)
**Step 4**: 编写 `npt_core.c` (identity NPT)
**Step 5**: 编写 `vmexit.c` + `vmmcall.c` (exit dispatch)
**Step 6**: 编写 `main.c` + `loader_stealth.c` (DriverEntry + 自卸载)
**Step 7**: 编写 `stealth.c` + `debug.c`
**Step 8**: 编写 Java 层 (mod/)
**Step 9**: 构建脚本 + 测试脚本
**Step 10**: 构建 → 静态检查 → VM/KD 验证
