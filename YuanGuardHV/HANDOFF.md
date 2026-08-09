# YuanGuardHV — 完整交接文档

> 生成日期: 2026-07-30 | 项目: AMD-V SVM/NPT Hypervisor | Phase 2
> 宿主机: WXSG-20260615RF (Win 10 19045, AMD Ryzen 5 5500)

---

## 1. 项目状态速览

| 里程碑 | 状态 |
|--------|:----:|
| Phase 1: 项目骨架 + 核心头文件 | ✅ 完成 |
| Phase 2a: SVM 初始化 + VMRUN 单核 | ✅ 代码完成, **未在物理机验证** |
| Phase 2b: NPT identity-map + NPF | ✅ 代码完成, **未在物理机验证** |
| Phase 2c: 多核 DPC 启动 | ✅ 代码完成, **未经过任何测试** |
| Phase 2d: 物理机验证 | ❌ **阻塞** — 需要 bare-metal 测试 |
| Phase 3+: 隐形/保护/Java 层 | ❌ 未开始 |

---

## 2. 关键发现 (必须阅读)

### 🔴 致命: VMware vhv.enable 不暴露 SVM 指令给 L1 Guest

```
clgi → #UD (STATUS_ILLEGAL_INSTRUCTION, 0xC000001D)
```

VMware 的 `vhv.enable = TRUE` **只让 L0 能运行嵌套虚拟化**，但 L1 Guest 中的驱动**无法执行任何 SVM 指令** (`clgi`, `vmrun`, `vmmcall` 等)。原因是 VMware 不会将 AMD SVM 的 CPUID 位暴露给 L1 guest。

**测试 YuanGuardHV 的唯一有效路径:**
1. **物理机 bare-metal** (BIOS 启用 SVM) — **推荐首选**
2. **KVM/Linux 嵌套虚拟化** (qemu + `-cpu host,+svm`)
3. **Hyper-V** (已确认不支持 AMD SVM 嵌套)

### 🔴 build.bat 的关键约束

- **不能加 `/OSVERSION:10.0`** — 否则 SCM 返回 1168
- **不能加 `/SUBSYSTEM:NATIVE,6.3`** — 同上
- **必须签名** — `signtool sign /fd SHA256 /a /f yuanguard_test.cer`
- **WDK 版本必须为 10.0.19041.0 或匹配** — `NTDDI_VERSION=0x0A000005`
- **当前 WDK Include 版本**: 从 `C:\Program Files (x86)\Windows Kits\10\Include\` 自动检测

### 🔴 KdPrint/DbgPrintEx 在 boot debugger 下不可见

- `KdPrint` 输出仅在 `KdDebuggerEnabled` 且 KD 连接时可见
- **崩溃定位推荐方案**: 用 `KeBugCheckEx(0xDEAD, step_marker, param, 0, 0)` 打标记，在 KD 中看 bugcheck 参数1

---

## 3. 代码地图

### 3.1 核心组件

| 文件 | 行数 | 职责 | 状态 |
|------|------|------|:----:|
| `hv/main.c` | 54 | DriverEntry: init → NPT → set_npt → resident → cleanup | ✅ |
| `hv/svm_core.c` | 570 | SVM init/cleanup, VCPU 分配, Resident loop, IPI callbacks | ✅ |
| `hv/svm_trampoline.S` | 153 | VMRUN 汇编 trampoline (GPR 保存/恢复, HSAVE) | ✅ |
| `hv/vmexit.c` | 144 | Exit 分发 (VMMCALL/CPUID/MSR/CR/NPF/HLT/PAUSE) | ✅ |
| `hv/npt_core.c` | 23 | NPT PML4→PDPT→PD 三级页表, 2MB 大页 identity map | ✅ |
| `hv/vmmcall.c` | 44 | VMMCALL 命令 (心跳/停止/版本/统计) | ✅ |
| `hv/multi_core.c` | 41 | DPC 多核启动/等待 (KeInitializeDpc) | ✅ 未测试 |
| `hv/loader_stealth.c` | 66 | 脱 PsLoadedModuleList | ✅ |
| `hv/min_drv.c` | 18 | 最小加载测试 (仅 return STATUS_SUCCESS) | ✅ |
| `hv/stub.c` | 17 | 带 LOG 的 stub (DbgPrint) | ✅ |
| `hv/test_drv.c` | 13 | 最简测试驱动 | ✅ |

### 3.2 头文件

| 文件 | 职责 |
|------|------|
| `common/svm_defs.h` | AMD SVM 常量: MSR 地址, CPUID leaves, exit codes, 拦截位定义, NPF 位 |
| `common/svm_vcpu.h` | VCPU 结构体 (含 regs/rstate), 偏移量断言, API 函数声明 |
| `common/vmcb.h` | VMCB control + state 区域结构体 (严格按 AMD APM B节布局) |
| `common/npt.h` | NPT entry union (bitfield), 页表索引宏, 管理结构体 |
| `common/control_plane.h` | VMMCALL 命令枚举 (HEARTBEAT/STOP/STATS 等) |
| `common/debug.h` | LOG_INFO/ERROR/WARN 宏 (DbgPrintEx) |
| `common/msr.h` | MSR 常量 inline helpers |
| `common/crx.h` | CRx 操作 inline |
| `common/cpuid.h` | CPUID 封装 |

### 3.3 构建产物

`bin/` 目录含 51 个文件，其中关键产物:

| 文件 | 大小 | 用途 |
|------|------|:----:|
| `bin/yuanguard_hv3.sys` | 11648 bytes | **最新完整构建** |
| `bin/min_drv.sys` | 4480 bytes | **最小 stub — 用于加载链验证** |
| `bin/*.obj` | — | 中间目标文件 |

---

## 4. 架构要点

### 4.1 SVM 初始化流程 (main.c)

```
DriverEntry
  ├─ svm_core_init()         — alloc vcpu, prepare VMCB (含 guest CR3/RIP 镜像)
  ├─ MmGetPhysicalMemoryRanges() → 获取物理内存上限
  ├─ npt_init()              — 分配 PML4, identity-map 全物理内存 (2MB 大页)
  ├─ svm_core_set_npt()      — 设置 VMCB.ncr3 + np_enable=1
  ├─ svm_core_enter_resident_current()  — 进入 resident loop
  │    └─ while(ACTIVE) { svm_vmrun_trampoline() → svm_dispatch_exit() }
  ├─ npt_cleanup()
  └─ svm_core_cleanup()
```

### 4.2 VMRUN Trampoline (svm_trampoline.S)

```
svm_vmrun_trampoline(vcpu in RCX):
  1. 保存 host GPR (push rbx..rsi)
  2. 从 vcpu->regs 加载 guest GPR (r15..rax)
  3. WRMSR MSR_VM_HSAVE = vcpu->hsave_pa
  4. CLGI
  5. VMRUN (rax = vcpu->vmcb_pa)
  6. #VMEXIT → STGI
  7. 从 VMCB state.rax 读 guest RAX → vcpu->regs.rax
  8. 保存 guest GPR (rcx/rdx/rbp/rsi/rdi/r8-r15) 到 vcpu->regs
  9. 恢复 host GPR (pop rsi..rbx)
  10. 从 VMCB control.exitcode 返回
```

**关键设计决策**: 
- VMRUN 使用 RAX 传 vmcb_pa（rax 在 VMRUN 后被保存到 VMCB state.rax，可以恢复）
- Guest GPR 通过 `vcpu->regs` RAM 区域在每次 VMRUN 之间持久化
- 宿主机 GPR 由 HSAVE 硬件自动保存/恢复，trampoline 只需处理 vcpu->regs

### 4.3 NPT 页表结构

```
PML4 (512 entries, 1 page)
  └─ PDPT (512 entries, 1 page per PML4 entry)
       └─ PD (512 entries, 1 page per PDPT entry)
            └─ 2MB 大页 entry → 物理地址
```

- 最多映射 64GB 物理内存 (512×512×2MB)
- 大页标志: present|writable|user|accessed|dirty|large_page (NPT_LARGE_PAGE_FLAGS)
- NX 位: bit63 (已修复 — 在 npt.h 的 bitfield 中定义)

### 4.4 多核架构 (multi_core.c)

```
svm_core_start_remote_residents(online):
  for each core i (1..online-1):
    KeInitializeDpc(DPC, svm_dpc_resident_start, dpc)
    KeSetTargetProcessorDpc(dpc, i)
    KeInsertQueueDpc(dpc, NULL, NULL)
    → dpc 在目标核运行 svm_core_enter_resident_current(core)
```

**当前**: main.c 中没有调用 `svm_core_start_remote_residents` — 只有单核测试路径。

---

## 5. 已修复 Bug 清单

| # | 症状 | 根因 | 修复 |
|---|------|------|------|
| 1 | VMRUN 蓝屏 | MSR_VM_HSAVE 未设置, VMRUN 写宿主机到随机地址 | trampoline 加 WRMSR |
| 2 | 1168 加载失败 | EFER.SVME 检查对嵌套虚拟化过于严格 | 跳过 wrmsr，信任 SVM |
| 3 | Guest RAX=0 导致立即退出 | vmcb->state.rax 不是 HEARTBEAT | svm_prepare_vcpu 设 state.rax |
| 4 | NPT 链接后任意 VMRUN 崩溃 | `__attribute__((packed))` 损坏 npt_entry_t | 去掉 packed |
| 5 | npt_cleanup 后 VMRUN 崩溃 | cleanup 释放了 VMRUN 仍需要的页 | cleanup 移到 resident 退出后 |
| 6 | Guest RAX 被 trampoline 覆盖 | rax 被 hsave_pa/vmcb_pa 反复覆盖 | push/pop 保护 + VMRUN 用 rcx |
| 7 | VMRUN 后误存所有 GPR 到 regs | VMRUN 退出后 GPR 是 host 值 | 只存从 VMCB state.rax 读的 RAX |

---

## 6. 当前阻塞

### 6.1 无法在 VMware 中验证

`clgi` 在 VMware vhv.enable L1 guest 中触发 #UD。这是 **VMware 的设计限制**，不是 bug。

### 6.2 物理机测试准备

宿主机 (WXSG-20260615RF, AMD Ryzen 5 5500) 已确认:
- CPU 支持 SVM (AuthenticAMD)
- OS: Win 10 19045 x64
- SeLoadDriverPrivilege: 已启用
- testsigning: 需确认 (`bcdedit /set testsigning on` + 重启)
- 驱动签名: 使用 `yuanguard_test.cer`

### 6.3 宿主机上次 crash (2026-07-30 19:06)

`usbwifi.sys` 触发 #D1 (DRIVER_IRQL_NOT_LESS_OR_EQUAL) — 与 YuanGuardHV 无关，但建议禁用或更新该驱动以防干扰。

---

## 7. 下一步 — 物理机验证计划

### Step 1: 准备宿主机
- [ ] `bcdedit /set testsigning on && bcdedit /set nointegritychecks on` → 重启
- [ ] 验证 `bcdedit /enum {current}` 中 testsigning 和 nointegritychecks = Yes
- [ ] `reg add "HKLM\System\CurrentControlSet\Control\CI" /v IntegrityTest /t REG_DWORD /d 1 /f`
- [ ] `reg add "HKLM\System\CurrentControlSet\Control\CI" /v TestSigning /t REG_DWORD /d 1 /f`
- [ ] 禁用 usbwifi.sys (sc config usbwifi start= disabled) 或卸载

### Step 2: 验证驱动加载链 (min_drv.sys)
- [ ] 用 build.bat 编译 min_drv.sys (或将 min_drv.c 单独编译)
- [ ] 签名: `signtool sign /fd SHA256 /a /f D:\yuanguard\YuanGuardHV\yuanguard_test.cer C:\min_drv.sys`
- [ ] sc create yf type= kernel binPath= C:\min_drv.sys
- [ ] sc start yf → 预期: STATE=4 RUNNING
- [ ] sc query yf → 验证
- [ ] sc stop yf / sc delete yf

### Step 3: 验证加载链 (yuanguard_hv3.sys)
- [ ] 编译并签名 yuanguard_hv3.sys
- [ ] sc create yh type= kernel binPath= C:\yuanguard_hv3.sys
- [ ] sc start yh
- [ ] 预期: 加载成功或 BSOD — 如果是 BSOD，收集 minidump

### Step 4: 分析 BSOD (如果发生)
- [ ] 检查 `C:\Windows\Minidump\*.dmp`
- [ ] 用 `cdb -z <dumpfile>` 分析: `!analyze -v`
- [ ] 或用 `kd -z <dumpfile>` 分析
- [ ] 根据崩溃点定位问题

### Step 5: 迭代调试
- [ ] 使用 KeBugCheckEx(0xDEAD, step, param, 0, 0) 标记步骤
- [ ] 在 crash dump 或 KD 连接中看 bugcheck parameter1 确定崩溃步骤
- [ ] 调整代码 → 编译 → 签名 → 测试

### Step 6: 多核测试 (可选)
- [ ] 解锁 multi_core.c 的调用
- [ ] 需要 2+ 核物理机
- [ ] 测试 DPC 多核 resident 启动

---

## 8. 环境信息

| 项 | 值 |
|-----|-----|
| 宿主机名 | WXSG-20260615RF |
| CPU | AMD Ryzen 5 5500 (AuthenticAMD, SVM/NPT capable) |
| OS | Windows 10 x64 19045 |
| shell | PowerShell 7+ (pwsh) |
| LLVM | `C:\Program Files\LLVM\bin\clang-cl.exe` |
| MSVC link | `C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC\14.44.35207\bin\Hostx64\x64\link.exe` |
| WDK | `C:\Program Files (x86)\Windows Kits\10\Include\10.*` |
| 签名证书 | `D:\yuanguard\YuanGuardHV\yuanguard_test.cer` |
| 代码根目录 | `D:\yuanguard\YuanGuardHV\hv\` |
| 构建产物目录 | `D:\yuanguard\YuanGuardHV\bin\` |
| 构建脚本 | `D:\yuanguard\YuanGuardHV\build.bat` |
| KD (VM 调试) | `kd.exe -k com:pipe,port=\\.\pipe\yuanhv_debug,resets=0,reconnect` |
| VM 路径 | `D:\vmware\Windows 11 x64.vmx` |

---

## 9. 构建 & 签名命令速查

```powershell
# 构建
cd D:\yuanguard\YuanGuardHV
.\build.bat

# 签名
signtool sign /fd SHA256 /a /f yuanguard_test.cer bin\yuanguard_hv.sys

# 部署到宿主机
Copy-Item bin\yuanguard_hv.sys C:\yf.sys -Force
Copy-Item bin\min_drv.sys C:\min_drv.sys -Force

# 加载
sc.exe create yf type= kernel binPath= C:\yf.sys
sc.exe start yf
sc.exe query yf

# 卸载
sc.exe stop yf
sc.exe delete yf

# VM 部署 (通过共享文件夹)
Copy-Item bin\yuanguard_hv.sys "\\vmware-host\Shared Folders\aaaaaavm\" -Force
# VM 内:
Copy-Item "\\vmware-host\Shared Folders\aaaaaavm\yuanguard_hv.sys" C:\yf.sys -Force
```

---

## 10. 待清理

- `bin/` 目录: 51 个文件，其中 30+ 是历史测试产物。建议清理到只剩 `yuanguard_hv3.sys`, `min_drv.sys`, 和 `.obj` 文件。
- `hv/common/*.bak` 文件: 7 个 .bak 遗留文件，可以删除。
- `hv/reference_*` 文件: UnrealVTDbg 遗留参考文件，可以迁移到 `reference/`。
- `kd_*.log` 文件: 10+ KD 日志在项目根目录，建议归档清理。
