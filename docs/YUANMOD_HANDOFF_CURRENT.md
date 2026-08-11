# YuanMod / YuanGuardHV 当前交接与决策记录

> 接管人: Codex (/root) | 日期: 2026-08-09 | 仓库根: D:\yuanguard
> 本文件按协作铁律第 3 条维护，是项目所有修改与决策的记录，优先级高于普通文档。

## 1. 项目一句话

AMD-V SVM/NPT 隐形 Hypervisor（YuanGuardHV），替代原 YuanGuard 内核驱动，把 Minecraft/Forge 进程保护逻辑放到虚拟化层。

## 2. 当前进度快照（2026-08-11，本次会话完整记录见 `docs/SESSION_20260811.md`）

| 里程碑 | 状态 |
|---|---|
| Phase 1 骨架 + 核心头文件 | 完成 |
| Phase 2a SVM init + VMRUN 单核 | 完成：10000 轮 VMMCALL 心跳稳定 |
| Phase 2b NPT identity-map + NPF | 完成：16GB identity map + NPF 权限注入验证通过 |
| Phase 2c 多核 DPC | 完成：每核系统线程，双核 10000 轮心跳稳定 |
| Phase 2d 物理机验证 | 未完成 |
| Phase 3+ 隐形/保护/Java 层 | 未开始 |
| git 版本控制 | 本次初始化完成 |
| 构建基线 | 成功，见第 5 节 |
| VM+KD 调试通道 | 可用：官方镜像 VM + 串口命名管道 |
| 不重启反复测试 | 完成：`DriverUnload` + `unload_driver.ps1` 两轮验证 |
| v22 基线复验（压缩会话后） | 完成：加载→双核心跳 10000→卸载→不重启重载→双核心跳 10000，全部通过 |
| R1 NPT API 单测（v23） | 进行中：代码已实现并构建，待 VM 加载验证 |
| R1 NPT API 单测（v25） | 完成：translate/perm-range/split 全部 PASS，双核心跳通过 |
| R1 NPF 注入测试（v26） | 完成：权限剔除→NPF→恢复映射→重执行→双核心跳通过 |

注意：`hv/` 代码在 2026-07-31 之后已更新（`svm_core.c` 中原来的 `#if 0` stub 已不存在，`main.c` 改为直接 `svm_core_init()` + inline NPT，新增 `test_step1..6.c` 等），7/30 的 `TECHNICAL_REVIEW.md` 结论需要逐项复核后再采用。

## 3. 环境清单

### 3.1 构建工具链

| 项 | 路径 |
|---|---|
| clang-cl | `C:\Program Files\LLVM\bin\clang-cl.exe` |
| MSVC link | `C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC\14.44.35207\bin\Hostx64\x64\link.exe` |
| WDK Include | `C:\Program Files (x86)\Windows Kits\10\Include\10.0.19041.0`（另有 10.0.26100.0） |
| signtool | `C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64\signtool.exe` |
| 测试证书 | `D:\yuanguard\YuanGuardHV\yuanguard_test.cer` |

### 3.2 VMware / KD

| 项 | 值 |
|---|---|
| VMX | `C:\Users\Administrator\Documents\Virtual Machines\Windows 10 x64\Windows 10 x64.vmx` |
| VM 配置 | 官方镜像 Win10 Pro 19045.2965 / `vhv.enable=TRUE` |
| 调试管道 | `\\.\pipe\yuanhv_debug`（serial0, server 端） |
| KD | `C:\Program Files (x86)\Windows Kits\10\Debuggers\x64\kd.exe` |
| 连接脚本 | `YuanGuardHV\run_kd.bat` / `start_kd.ps1` |
| VM 状态 | 运行中，KD 已连接 |

### 3.3 已知环境限制

- VMware `vhv.enable=TRUE` 的项目记录是：L1 Guest 中执行 `clgi` 触发 `#UD`，即 VMware 不把 AMD SVM 指令暴露给 Guest。需在 VM 内用 CPUID/CLGI 实测再确认；若属实，VMRUN 冒烟只能走裸机或 KVM 嵌套。
- 宿主 `bcdedit` 在当前非管理员 shell 不可用；testsigning / nointegritychecks 状态需在裸机或 VM 内确认。
- 宿主 `C:\Windows\Minidump` 有 7/30 的 4 个 dump，其中 `usbwifi.sys` 蓝屏与项目无关。
- 宿主系统为“不忘初心”修改版 Windows 10 Pro for Workstations 22H2（19045.7291），非官方镜像；历史上 7/26-7/30 已有多次意外关机与 VMware 崩溃记录，需纳入稳定性根因考虑。
- VMware 17.5.2 官方下载需 Broadcom 账号登录；旧 `download3.vmware.com` 直链已下线，Broadcom token 链接 403。本地仅有 17.6.4 官方安装包（`C:\Users\Administrator\Downloads\VMware-workstation-full-17.6.4-24832109.exe`）。

## 4. 初始化动作

- [x] 2026-08-09 只读勘查仓库与环境（工具链 / VM / KD / 代码状态）
- [x] 2026-08-09 用户确认初始化方案（docs 记录 + git + 构建基线 + 任务清单）
- [x] `git init` + `.gitignore` + 首次提交
- [x] 修复 build.bat（用户确认后完成）
- [x] `build.bat` 构建基线：成功，见第 5 节
- [x] VM 加载链验证（min_drv）
- [x] VM 内 CPUID/CLGI SVM 暴露实测
- [ ] 重新核查 TECHNICAL_REVIEW P0 是否仍适用（表已更新，剩余项见 TASKS.md）
- [ ] 裸机冒烟准备

## 5. 构建基线

| 日期 | 结果 | sys SHA256 | 备注 |
|---|---|---|---|
| 2026-08-09 | SUCCESS | `9dfade1556d88feee9b08eb5950005100155639d933cf6b3e76a700cbb99fcca` | 修复 build.bat：for 块改为 `call :compile` 子程序，INCLUDES/LIBPATH 路径加引号。编译仅 WDK 头文件 intrinsic 警告（无害）。 |

## 6. 决策记录

| 日期 | 决策 | 理由 | 影响 |
|---|---|---|---|
| 2026-08-09 | 正式接管，先固化基线再动业务代码 | 铁律 1/2/3 | 后续改动先出方案、经确认，并记入本文件 |
| 2026-08-09 | 仓库初始化 git + .gitignore + 首次提交 | 后续可回滚、可审计 | 基线快照为 `docs` 建立后首次提交 |
| 2026-08-09 | build.bat 失败只记录不擅自修改 | 铁律 1 要求代码改动先确认 | 修复方案待用户确认后执行 |
| 2026-08-09 | 用户确认后修复 build.bat 并重跑基线 | 脚本路径含括号/空格导致无法编译 | 构建成功，sys 已签名，哈希见第 5 节 |
| 2026-08-11 | Task 5 review 修复：stub 换可执行 NonPagedPool、入口改 16B 绝对跳转、函数页先 split 再置只读并纳入 NPF 写策略 | 评审发现 4 项问题，按反馈最小修复 | 仅改 `protect.c`；构建成功，v28 SHA256 `AC05BC52...3447EF` |

## 7. 变更日志

| 日期 | 文件 | 变更 | 验证 |
|---|---|---|---|
| 2026-08-09 | `.gitignore` | 忽略 bin/obj/log/dump 等 | git 状态检查 |
| 2026-08-09 | `docs/YUANMOD_HANDOFF_CURRENT.md` | 创建本记录 | - |
| 2026-08-09 | `docs/TASKS.md` | 创建任务清单 | - |
| 2026-08-09 | 仓库 | git init + 首次提交 | git log |
| 2026-08-09 | `YuanGuardHV/build.bat` | for 块改 `call :compile` 子程序；INCLUDES/LIBPATH 路径加引号 | 构建成功并签名 |
| 2026-08-09 | `YuanGuardHV/svm_trampoline.asm` | 移除 clang 生成的中间汇编并加入 .gitignore | git 状态干净 |
| 2026-08-09 | `D:\vmware\Windows 11 x64*`（38 文件） | 用户确认删除 Win11 VM；因环境策略拦截 `Remove-Item`，改用 `Move-Item` 移入 `D:\vmware\_win11_trash` | 原路径 0 个匹配文件 |
| 2026-08-09 | `%APPDATA%\VMware\inventory.vmls` | 备份为 `.bak-20260809` 后移除 Win11 条目，仅保留 Windows 10 x64 | 清单读取核对通过 |
| 2026-08-09 | 系统 WiFi 适配器 `WLAN` | 按用户要求禁用（`Disable-NetAdapter`） | 状态 Disabled |
| 2026-08-09 | USB WiFi 设备 `USB\VID_2357&PID_0147\20220127` | 按用户要求用 `pnputil /disable-device` 禁用 | 问题码 `CM_PROB_DISABLED` |
| 2026-08-10 | 火绒 `HipsDaemon`/`hrdevmon`/`sysdiag` | 尝试程序化禁用失败：`sc config` 与直接 `reg add Start=4` 均 `Access denied (5)`，`sc stop` 驱动返回 `1052`，进程无法终止 | 确认是火绒自我保护拦截；用户仅退出托盘，需在设置中关闭自我保护后再退出 |
| 2026-08-10 | 火绒安全 | 用户确认已关闭/卸载；复核：`HipsDaemon` 服务、`hrdevmon`、`sysdiag` 驱动与进程均不存在 | 冲突源移除，可重试 VMware |
| 2026-08-10 | 旧调试 VM `Windows 10 x64` | 按用户要求移除：34 文件（约 20.7GB）移入 `D:\win10_trash_backup\Windows 10 x64`，VMware 清单已清理（备份 `inventory.vmls.bak-20260810-remove-win10x64`） | 原路径已空，仅剩“Windows 10”VM |
| 2026-08-11 | `YuanGuardHV/hv/protect.c` | Task 5 review 修复：可执行 stub 页、16B 绝对跳转、2MB split、函数页注册 NPF 策略、NULL 检查 | `Build SUCCESS`；v28 已复制并记录 SHA256 |

## 8. 2026-08-09 主机无响应/蓝屏调查

- 主机于 `15:56:14` 意外关机（事件 6008），`15:57:57` 重启；发生在启动 VMware + Win11 VM 后约 3 分钟。
- 本次没有生成新 minidump，`C:\Windows\MEMORY.DMP` 仍为 7/30 文件，无当日 WER 报告；判断为硬卡死/断电式重启，无法从 dump 精确定位。
- 线索：VMware 日志显示接管了 TP-Link `AIC8800DC` USB WiFi；重启后 `hcmon` 报警 `unrecognized USB driver (\Driver\hrevmon)`；项目历史 7/30 曾因 `usbwifi.sys` 蓝屏。
- 结论：最大嫌疑是 VMware USB 直通与宿主机 WiFi 驱动（`usbwifi.sys` 类）互相干扰。处置：禁用 `WLAN` WiFi 适配器，后续调试不让 VMware 接管 USB 设备；若再发生，优先收集当日 dump。

### 8.1 第二次事件（16:14:04 意外关机，已复现）

- 16:12:48 启动 Win10 VM，16:14:04 主机意外关机，16:16:44 重启；两次均为 VM 启动后约 2-3 分钟。
- VMware 日志在崩溃前出现 `serial0: Unable to read from the "\\.\pipe\yuanhv_debug" named pipe: Insufficient system resources exist to complete the requested service.`（系统资源耗尽）。
- 两次会话 VMware 均枚举到 `TP-Link AIC8800DC`（VID_2357 PID_0147，即 `MERCURY Wireless N Adapter`）；该设备当前处于 PnP `Error` 状态，`hcmon` 反复报警 `unrecognized USB driver (\Driver\hrevmon)`。
- 第一次崩溃时 KD 尚未成功连接管道（KD 打印 usage 后立即退出），可排除 KD 客户端是触发源；共同触发源为 **VM 启动 + USB WiFi 设备枚举**。
- 无任何新的 Windows/VMware dump（均停在 7/26-7/30），属于硬卡死/掉电式重启，无法离线分析。
- 处置决策：**在拔除/禁用该 USB WiFi 设备或移除 VM USB 控制器前，不再启动任何 VMware VM**；VM+KD 验证任务标记为阻塞。

### 8.2 第三次事件（16:28:50 意外关机，USB 已排除）

- 16:28:48 启动 Win10 VM，16:28:50 主机意外关机（VM 启动后约 2 秒），16:31:30 重启。
- 本次已禁用 USB WiFi 设备（`CM_PROB_DISABLED`）且 VMX 已去掉 USB 控制器，VMware 日志**未再枚举 TP-Link USB 设备**，但主机仍崩溃；USB WiFi 不再是直接触发源。
- 新发现：`hrdevmon` 是 **火绒安全（Huorong Internet Security）的内核驱动**（`D:\firefirefire\Huorong`），与 `sysdiag` 均为启动/系统级驱动并处于运行状态；VMware `hcmon` 每次启动都报警 `unrecognized USB driver (\Driver\hrdevmon)`。
- 结论：触发源收敛到 VMware 虚拟化启动路径（`vmx86`/`vhv.enable` 嵌套虚拟化）与火绒内核驱动的潜在冲突；硬件（WHEA）无错误记录，且无新 dump，仍无法离线确认。
- 处置决策：**在解决火绒驱动与 VMware 冲突（或关闭 `vhv.enable`、升级 VMware）前，不再启动任何 VMware VM**。

### 8.3 第四次事件（2026-08-10 09:38 意外关机，火绒已排除）

- 火绒已完全卸载、USB WiFi 已禁用、VM USB 控制器已移除后仍复现：VM 启动后数秒至 3 分钟内主机硬卡死/意外重启，仍无 dump、无 WHEA。
- 结论：触发源锁定为 **VMware Workstation 17.6.4 (build 24832109) 在这台 AMD Ryzen 5 5500 主机上的虚拟化启动路径**（`vmx86`/`vhv.enable`）。
- 网络资料：Broadcom 社区有同版本（17.6.4 build 24832109）持续 BSOD/稳定性报告；17.6.x 的 mksSandbox/渲染崩溃报告较多，社区普遍反馈回退 17.5.2 或升级修复版可解决。
- 下一步候选：A) 升级/重装 VMware（最新版或 17.5.2）；B) 单次受控测试关闭 `vhv.enable`；C) 放弃 VMware，转裸机/KVM 验证。均需用户确认后再执行。

### 8.4 第五次事件 + 全面诊断（2026-08-10 09:48 意外关机，`vhv.enable=FALSE` 仍复现）

- 受控测试：Win10 VM `vhv.enable=FALSE`、USB 控制器已移除、火绒已卸载、USB WiFi 已禁用，VM 启动后仍硬卡死（09:48:50 意外关机，09:50:27 重启）。
- VMware 日志显示已加载 `hv-svm.vmm`/SVM 能力（`cpuid.svm=1`、`svm_npt=1` 等），随后在 Tools/HGFS 阶段中断，本次无串口管道错误、无 USB 枚举。
- 全面诊断（只读）：
  - OS：Windows 10 Pro for Workstations 22H2，build 19045.7291；VBS/HVCI 均 disabled（事件 153，HypervisorPresent=False），排除 Hyper-V 干扰。
  - 无任何新 dump（Windows Minidump/MEMORY.DMP/LiveKernelReports/WER/CrashDumps/VMware 均无），无 WHEA、无资源耗尽事件、无磁盘/池错误。
  - 内存：2×8GB Crucial DDR4-3200（匹配套条），配置 3200MHz；当前可用 11.6GB；pagefile 17GB 在 D 盘。
  - VMware：17.6.4 build-24832109；`vmx86.sys` 17.6.0.0、`hcmon.sys` 8.11.14.0。
- 触发矩阵：USB WiFi 禁用 ✓、VM USB 控制器移除 ✓、火绒卸载 ✓、`vhv.enable=FALSE` ✓，均无法阻止崩溃 → 结论：**VMware Workstation 17.6.4 在这台 AMD 主机上启动任何 VM 即触发宿主硬卡死**，属 VMware/宿主兼容性或硬件问题，非项目配置可绕过。

### 8.5 第六次事件 + 本地重装结论（2026-08-10 10:31 意外关机，重装后仍复现）

- 执行了本地完全重装：卸载旧 17.6.4（`D:\vmware`），修复残留 CLSID 路径（`vmnetbridge.dll` 旧路径 → 新安装目录），并跳过 Networking 组件（MSI 功能名 `Networking`）完成静默安装到默认路径。安装成功（`vmware.exe`/`vmrun.exe` 就位，服务正常）。
- 重装后单次重试：VM 10:31:28 启动，10:31:33 主机意外关机（约 5 秒），10:33:32 重启。**第 6 次复现，6/6 全部崩溃。**
- 最终结论：**这台主机无论 VMware 安装状态/网络组件/USB/火绒/`vhv.enable` 如何，启动任何 VMware VM 都会硬卡死**；根因在宿主系统（“不忘初心”修改版内核/虚拟化兼容）或硬件（内存/CPU/主板/电源），无法在项目侧绕过。
- 处置：停止所有 VMware VM 尝试。VM+KD 验证路径在本机永久标记为不可用；后续只能走硬件诊断、换机/KVM 或裸机验证。
- 环境遗留：`D:\win11_trash_backup`（Win11 VM 38 文件约 27.8GB）待用户手动删除；VMware 17.6.4 已安装于默认路径且未装 Networking（若后续要网络功能需在稳定环境修复安装）。

## 9. VM 环境现状（2026-08-09）

| VM | 路径 | 状态 |
|---|---|---|
| Windows 10（保留，运行中） | `C:\Users\Administrator\Documents\Virtual Machines\Windows 10\Windows 10.vmx` | `vhv.enable=TRUE`、USB 开启、光驱挂 SeekOS ISO、串口 `COM1`；主机当前稳定 |
| Windows 11 x64（已移除） | `D:\vmware\Windows 11 x64.vmx` | 38 个文件已移入 `D:\vmware\_win11_trash`（约 27.8GB，待手动删除）；VMware 清单已清理 |
| Windows 10 x64（已移除） | `C:\Users\Administrator\Documents\Virtual Machines\Windows 10 x64\` | 34 文件移入 `D:\win10_trash_backup\Windows 10 x64`（约 20.7GB，待手动删除）；VMware 清单已清理 |

### 9.1 重要新线索（2026-08-10）

- 发现第二个 Win10 VM（`Windows 10`，位于 `C:\Users\Administrator\Documents\Virtual Machines\Windows 10\`），该 VM `vhv.enable=TRUE`、USB 开启、串口为 `COM1`，**正在运行且主机未崩**。
- 之前 6/6 崩溃的 VM（`Windows 10 x64`）与它的主要差异之一是**串口命名管道 `yuanhv_debug`（server）**，以及调试期相关的配置。
- 假设待验证：崩溃触发点可能不是 VMware 虚拟化本身，而是该 VM 的 `yuanhv_debug` 命名管道串口；后续可在稳定主机上做对照测试（新建 VM 仅保留管道 vs 无管道）。

### 9.2 KD 串口修复（2026-08-10 12:10）

- 现象：VMX 已带 `serial0` 命名管道配置，但 Guest `wmic Win32_SerialPort` 无 COM1，KD 停在 `Waiting to reconnect...`。
- 诊断：`pnputil /add-driver msports.inf /install` 成功并绑到 `ACPI\PNP0501\1`，但该设备是**幽灵设备（CM_PROB_PHANTOM / 已断开连接）**，说明本次启动 VMware 串口未被 Guest 枚举；`Serial`/`Serenum` 服务与 `serial.sys`/`serenum.sys` 均存在。
- 根因：管道串口缺少 `serial0.startConnected = "TRUE"`（对应 GUI“打开电源时连接”），等于串口存在但开机未连接，Guest ACPI 不暴露 COM 口；11:20 用物理 `COM1`（fileType=device）启动时 VMware 日志有完整 `serial0:` 初始化记录，改管道后的 3 次启动均无。
- 处置：软关机 VM → 关闭 VMware → 字节级插入 `serial0.startConnected = "TRUE"`（仅 +33 字节，未动其他内容，备份 `Windows 10.vmx.bak-20260810-startconnected`）→ 清除残留锁目录（改名 `Windows 10.vmx.lck-stale-20260810`）→ 重新启动 VM。
- 结果：`startConnected=TRUE` 后重启，Guest 仍无 COM1；删除幽灵节点 `ACPI\PNP0501\1` 后扫描仍不出现。改回物理 COM1 直通（`fileType=device`，备份 `Windows 10.vmx.bak-20260810-device-test`）后，VMware 日志出现完整 `serial0:` UART 初始化，但 Guest 依然没有任何 PNP0501 节点 → **VMware 未把串口写入 Guest ACPI 表**，与管道/物理后端无关。
- 下一步：单变量测试 `vhv.enable=FALSE`，确认是否嵌套虚拟化路径导致 ACPI 串口缺失；若仍不行，转向 kdnet 网络内核调试或换镜像/换机。
- 补充：`vhv.enable=FALSE` + 物理 COM1 直通后 Guest 仍无 COM1；hdwwiz 添加的传统端口 `ROOT\PORTS\0000/0001` 全部 `CM_PROB_NO_SOFTCONFIG`（代码 34），`MsPorts.dll` 在 Guest 中存在。结论：Guest“不忘初心”精简镜像的设备安装/ACPI 串口枚举链路异常，换官方镜像最稳。

### 9.3 官方 Win10 22H2 ISO 下载（2026-08-10）

- 经微软官方 `software-download-connector` 接口获取带令牌直链，下载 `Win10_22H2_Chinese_Simplified_x64v1.iso` 到 `C:\Users\Administrator\Downloads\`。
- 大小 6,096,424,960 字节（约 5.68 GiB）；SHA256 `D485D370406CBCB68959718817BD12ED87E537A14C885F84962E07136FC4A049`（与公开官方镜像哈希一致）。
- 用途待定：装到 Guest VM 替换“不忘初心”镜像（解决 COM1/KD），或作为宿主机官方镜像候选。

### 9.4 KD 调试通道打通（2026-08-10 13:19）

- 用户已用官方 ISO 重装 Guest：新 VM `C:\Users\Administrator\Documents\Virtual Machines\Windows 10 x64\Windows 10 x64.vmx`（Win10 Pro Build 19045.2965，官方镜像，新 UUID；旧 `Windows 10` VM 已移除）。
- 官方镜像下 `wmic Win32_SerialPort` 立即出现 `COM1 ACPI\PNP0501\1`，**实锤此前“不忘初心”精简镜像损坏 ACPI 串口枚举/设备安装链路**；管道后端在官方镜像下同样正常枚举。
- VMX 串口改为命名管道（备份 `Windows 10 x64.vmx.bak-20260810-kdpipe`）：`\\.\pipe\yuanhv_debug`，server 端，`startConnected=TRUE`；`vhv.enable=TRUE` 保持。
- Guest BCD：`debug Yes`、`dbgsettings Serial debugport:1 baudrate:115200`、`testsigning on`、`nointegritychecks on`。
- KD（`run_kd.bat`）已连接：`Connected to Windows 10 19041 x64 target`，`Kernel Debugger connection established`，Kernel base `0xfffff80127000000`。
- 注意事项：KD 需在 VM 创建管道后再启动；`bcdedit /debug on` 曾出现“成功”但未落到 `{current}`，需用 `bcdedit /enum {current}` 复核 `debug Yes` 后再重启。
- 宿主仍是不忘初心镜像，VM 启动曾 6/6 崩溃的问题未最终定性；当前新 VM 启动/运行稳定。

### 9.5 yuanguard_hv 1168 修复（2026-08-10）

- 现象：`sc start yuanguard` 返回 1168（找不到元素）；事件日志只有 Win32 错误，无 NTSTATUS。
- KD 定位：驱动映像加载成功、DriverEntry 命中；Guest `Win32_Processor.VirtualizationFirmwareEnabled=True`、SLAT=True，说明 SVM 已暴露。
- 根因（代码 bug）：`svm_core_init()` 用 `KeGetCurrentProcessorNumber()` 分配 `g_vcpus[core]`，`main.c` 却硬编码 `svm_core_get_vcpu(0)`；加载线程不在 0 号核心时取到 NULL → `STATUS_NOT_FOUND` → 1168。
- 修复：`hv/main.c` DriverEntry 开头加 `KeSetSystemAffinityThread((KAFFINITY)1);`，Phase 2a 单核固定在 CPU 0。
- 构建：`build.bat` SUCCESS，签名成功；新 SHA256 `cc367db6879b88a2970fb068ea0e575f18980b701bdb97e2f77b3b7b9a149e1f`。
- 验证：Guest 重新加载后 `sc query yuanguard` 为 `STATE RUNNING`；KD 日志提示 `EXWORKER: worker exit with system affinity set`（DriverEntry 返回前未恢复线程亲和性，待补 `KeRevertToUserAffinityThread()`）。
- 遗留：LOG 宏因未定义 `YGHV_DEBUG_LOG` 被编译掉，resident 循环第一轮退出原因待开日志或 KD 断点确认。
- 新增 `YuanGuardHV/kd_ctl.ps1`：KD 控制脚本（`-b` 开机断点 + `kd_cmd.txt` 命令文件通道 + `-logo` 日志），便于远程操作 KD。

### 9.6 VMCB 段属性编码修复 + 描述符诊断日志（2026-08-10）

- 现象：VMRUN 真实执行，但退出码为 `VMEXIT_INVALID`（`0xffffffffffffffff`），VMCB 段日志 `cs=10/9b/0 base=0x0`。
- 根因（代码 bug）：`yg_read_seg_descriptor()` 用 `(lo >> 40) & 0xF0FF` 提取 VMCB 属性，把 GDT descriptor 的 bits 52-55（AVL/L/D/B/G）放到了 VMCB 属性 bits 12-15；AMD VMCB 要求这些位放在属性 bits 8-11，因此 64 位长模式代码段需要 `L=1`（GDT bit 53）时，属性应为 `0xA9B` 而非 `0x9B`，CS 缺少 L 标志导致 VMRUN 非法。
- 修复：`svm_core.c` 属性编码改为 `((lo >> 40) & 0xFF) | (((lo >> 52) & 0xF) << 8)`；同时在 `yg_read_seg_descriptor()` 内为 CS/SS/DS/ES/TR 增加 `lo/hi` 原始值与 `attrib/limit/base` 提取结果的 `[YGHV] seg sel=...` 日志。
- 构建：`build.bat` SUCCESS，签名成功；新 SHA256 `e624c77e74b528d906530cda49344f2ad450a7b944a2b5ff09696bea43cb4363`。
- 共享目录复制：首次因 Guest 仍映射旧驱动（文件被占用）失败，需 Guest 重启后由主机先覆盖 `D:\aaaaaavm\yuanguard_hv.sys` 再在 Guest 重载。
- 待验证：重载后 KD 日志应看到 CS attrs `0xA9B`，VMRUN 不再以 `VMEXIT_INVALID` 退出；若通过，下一问题可能转移到 NPT/首个 VMEXIT（VMMCALL 已拦截）。

### 9.7 NPT 强制开启 vs 最小 VMRUN 测试（2026-08-10）

- 现象：CS 属性修复后（`cs=10/29b/0`）仍 `VMEXIT_INVALID`；对照 Windows x64 GDT，CS/SS limit=0、DS `0xCF3/FFFFFFFF` 均为正常长模式值，段解析无误。
- 根因（嫌疑）：`svm_prepare_vcpu()` 按“minimal test”把 `np_enable=0`，但 `main.c` inline NPT 段随后强制 `np_enable=1/ncr3=pml4_pa`，KD 日志 `np=0x1 ncr3=0x2395c7000`；VMware 嵌套 SVM 对 L1 直接开 NPT 的 VMCB 判非法。
- 修改：`hv/main.c` 删除 inline NPT 强制开启段，保留 `np_enable=0` 先跑通最小 VMRUN（用户已确认）。
- 构建：`build.bat` SUCCESS，签名成功；新 SHA256 `5f51de9d89a1e0fae42a3e1dcc1057b206d216abed3441f24aa37a11c532e3fa`。
- 待办：VM 重启释放共享目录占用后，主机覆盖 `D:\aaaaaavm\yuanguard_hv.sys` 再由 Guest 重载；若仍 INVALID，将段诊断日志从 INFO 提为 ERROR 或开 DbgPrint mask 抓完整 FS/GS/TR/GDTR。

### 9.8 TLB_CONTROL + 全量 VMCB 诊断（2026-08-10）

- 验证：NPT 关闭（`np=0x0 ncr3=0x0`）后重载，VMRUN 仍 `VMEXIT_INVALID`，排除 NPT；段值仍为 Windows x64 正常值。
- 新嫌疑：`tlb_control=0x01`（FlushByASID）在 VMware 嵌套 SVM 下可能未暴露 FLUSHBYASID（CPUID 0x8000000A.EDX bit1），保留编码触发 INVALID。
- 修改（用户已确认）：`svm_core.c` `tlb_control` 改为 `0x03`（flush all，不依赖 FLUSHBYASID）；`yg_read_seg_descriptor` 诊断日志提为 ERROR；`svm_prepare_vcpu` 增加全量 VMCB 段/表寄存器 ERROR dump + CPUID 0x8000000A（NPT/FLUSHBYASID）打印；`svm_defs.h` 增加 `CPUID_NPT_FEATURE_FLUSHBYASID`。
- 构建：`build.bat` SUCCESS，签名成功；新 SHA256 `e74349b59edf9ad0728305f3b872e7376f7d43d6c276a4b5e0bd23c0a4b044b2`。
- 待验证：重载后先看 CPUID EDX 是否暴露 FLUSHBYASID，再确认 VMRUN 是否仍 INVALID；若仍 INVALID，靠新增 ERROR dump 直接定位剩余字段。

### 9.9 VMLOAD/VMSAVE + G_PAT + 栈偏移修复（2026-08-10）

- 现象：`tlb_control=0x03` 重载后 VMRUN 仍 `VMEXIT_INVALID`（exitcode `0xffffffffffffffff`），FS/GS 手工填为 selector/attrib/limit=0 且未走 VMLOAD/VMSAVE。
- 决策（用户已确认，2026-08-10）：对照 SimpleSvm 参考实现，一次性修正最小 VMRUN 通路：
  - `svm_trampoline.S`：VMRUN 前补 `VMLOAD guest VMCB`，VMEXIT 后补 `VMSAVE guest VMCB` + `VMLOAD host VMCB`；`stgi` 移到 guest GPR 全部落盘之后。
  - `svm_trampoline.S`：修复 VMEXIT 后 vcpu 指针栈偏移 bug，`[rsp+0x18]` 改为 `[rsp+0x10]`（此前读写随机地址，是“执行完卡住”的最大嫌疑）。
  - `svm_core.c`：`tlb_control` 从 `0x03` 改为 `0`；新增 `yg_svm_vmsave/vmload` 内联汇编；`svm_prepare_vcpu` 设置 `g_pat`（MSR 0x277）并调用 `vmsave(guest_vmcb_pa)`、`vmsave(host_vmcb_pa)` 填充隐藏段/MSR 状态。
  - `vmcb.h`：状态区 `+0x268` 增加 `g_pat` 字段，去掉伪 `gif` 字段。
  - `svm_defs.h`：`CPUID_NPT_FEATURE_FLUSHBYASID` 从 `1<<1` 改为 AMD 正确定义 `1<<6`。
- 构建：`build.bat` SUCCESS，签名成功；最终（含 `stgi` 后移）SHA256 `84c097da525fef000d38a103651015820de36d30510858c484333f3db3f8378d`。
- 共享目录复制：首次复制 `D:\aaaaaavm\yuanguard_hv.sys` 失败（文件被 VM/旧驱动占用），需 Guest 先 `sc.exe stop yuanguard` / `sc.exe delete yuanguard` 或重启 VM 释放后，主机再覆盖并让 Guest 重新加载。
- 2026-08-10 16:52：用户 `sc stop` 报 1052，改用 `vmrun reset` 重启 VM；KD 16:57:10 重连，主机已成功覆盖共享目录新驱动。
- 待验证：重载后 KD 日志应看到 FS/GS/TR/LDTR 由 VMSAVE 填充的真实值、`tlb_ctl=0`；若 VMRUN 通过，首个出口应为 `SVM_EXIT_VMMCALL (0x81)`。

### 9.10 补 INTERCEPT_VMRUN（2026-08-10）

- 现象：9.9 全量 VMLOAD/VMSAVE + G_PAT + TLB_CONTROL=0 后 VMRUN 仍 `VMEXIT_INVALID`（`0xffffffffffffffff`）；FS/GS/TR 已由 VMSAVE 填充真实值，CPUID FLUSHBYASID=1，排除此前各嫌疑。
- 决策（用户已确认，2026-08-10）：AMD APM 嵌套 SVM 一致性检查/KVM nSVM/参考实现（SimpleSvm、back.engineering）均要求 VMCB `general2_intercepts` 置 `INTERCEPT_VMRUN`（bit0），当前只置 `INTERCEPT_VMMCALL`（bit1）。
- 修改：`svm_core.c` `general2_intercepts` 改为 `INTR_GEN2(SVM_INTERCEPT_VMRUN) | INTR_GEN2(SVM_INTERCEPT_VMMCALL)`。
- 待验证：重载后若 VMRUN 通过，首个出口应为 `SVM_EXIT_VMMCALL (0x81)`；若仍 INVALID，下一步打印 VMCB 控制区 `+0x000..+0x130` 原始字节，逐项对照 APM 一致性检查。

### 9.11 最小 VMRUN 通路打通 + 首次 STOP_INTERNAL（2026-08-10）

- 验证：9.10 补 `INTERCEPT_VMRUN` 后 VMRUN 成功，不再 `VMEXIT_INVALID`。KD 用 `!dq` 读 VMCB 物理页确认 `general2=0x3`、`exitcode=0x81 (SVM_EXIT_VMMCALL)`、`nRIP=svm_trampoline_test_guest_resume`。
- 新问题（0x139）：首次 VMMCALL 用 `HEARTBEAT` 时 resident 循环不退出，`svm_finish_exit` 把 guest RIP 推进到 `ud2` 哨兵；第二次 VMRUN 进入 guest 执行 `ud2`，且 guest 使用假栈，导致 `KERNEL_SECURITY_CHECK_FAILURE (0x139)`，`FAST_FAIL_INCORRECT_STACK`，崩溃点 `yuanguard_hv+0x2f9f`（`ud2`）。
- 决策（用户已确认，2026-08-10）：`svm_core_init` 首次 VMMCALL 从 `YGHV_CMD_HEARTBEAT` 改为 `YGHV_CMD_STOP_INTERNAL`，使最小测试路径为 `VMRUN -> VMMCALL(0x81) -> dispatch 停止 -> resident 退出 -> DriverEntry 返回`；多核 `svm_core_prepare_vcpu_other` 的 HEARTBEAT 暂不改动。
- 验证通过（2026-08-10）：Guest `sc.exe start yuanguard` 正常返回，`STATE 4 RUNNING`、`WIN32_EXIT_CODE 0`；说明 DriverEntry 已完成 resident 循环并返回，`VMRUN -> VMMCALL(0x81) -> STOP_INTERNAL -> 退出` 最小通路打通。构建 SHA256 `A95BA30BF64D02440AF857460A45B828A6A0688EFF052290945A9A75671A2AB3`。
- 说明：KD 日志因 `LOG_INFO` 被 DbgPrint mask 过滤，只显示到 `CPUID` 之前的 ERROR 日志；服务 RUNNING 输出即为 DriverEntry 返回的直接证据。

### 9.12 resident 循环有界压力测试（2026-08-10）

- 决策（用户已确认，2026-08-10）：把最小测试升级为有界 resident 压力测试，验证 `VMRUN/VMMCALL` 连续循环稳定，同时保证 `sc start` 能返回。
- 修改：
  - `svm_trampoline.S`：`svm_trampoline_test_guest_resume` 从 `ud2` 改为 `jmp svm_trampoline_test_guest`，guest 在 VMMCALL 后回到 `vmmcall` 形成循环。
  - `svm_core.c`：`svm_core_init` 首次命令恢复 `YGHV_CMD_HEARTBEAT`。
  - `vmmcall.c`：heartbeat 每 1000 次用 `LOG_ERROR` 打印 `heartbeat exits`，跑满 10000 次后调用 `svm_core_stop_all_residents()` 并返回停止，DriverEntry 正常结束。
- 预期：KD 日志依次出现 `heartbeat exits=1000/2000/.../10000`、`heartbeat limit reached`；`sc start` 返回 `STATE RUNNING`。
- 首轮测试发现并修复：HEARTBEAT 分发会把 RAX 写成 `YGHV_STATUS_OK(0)`，下一轮 guest 又以 0 发起 VMMCALL，导致大部分日志是 `Unknown VMMCALL cmd=0x0`。修复为 guest 测试代码在每次 `vmmcall` 前 `mov rax, 1`（HEARTBEAT），保证 10000 轮全部为正常心跳。
- 验证通过（2026-08-10）：KD 日志完整出现 `heartbeat exits=1000/2000/.../10000`、`heartbeat limit reached, stopping resident loop`，无 `Unknown VMMCALL`；Guest `sc start` 正常返回，服务 `STATE RUNNING`。resident 循环 10000 轮 VMRUN/VMMCALL 稳定。构建 SHA256 `6D5B244A3C381705E3A09BA51C110CF84B58472A3439C070C2AC6EC340C9372D`。

### 9.13 NPT 开启测试（2026-08-10）

- 决策（用户已确认，2026-08-10）：在 10000 次心跳有界验证通过后开启 NPT，检查 `NP_ENABLE=1 + nCR3` 是否影响 VMRUN/VMMCALL 通路。
- 修改：`main.c` `npt_init` 映射范围从 `0x40000000`（1GB）改为 `0x200000000`（8GB，匹配 VM 内存 8192MB）；`npt_init` 成功后调用 `svm_core_set_npt(0, g_npt.pml4_pa)` 开启 NPT，失败则清理退出；增加 `LOG_ERROR("NPT enabled...")` 便于 KD 确认。
- 预期：KD 日志先出现 `NPT enabled`，再完整出现 `heartbeat exits=1000/.../10000`；若出现 `VMEXIT_INVALID` 或 `NPF`，继续定位 VMCB/NPT 映射。

### 9.14 NPT 卡住 + NPF 诊断日志（2026-08-10）

- 现象：NPT 开启版 KD 日志停在 `NPT enabled: pml4=0x...`，没有心跳；guest 仍在运行，怀疑带 NPT 的 VMRUN 进入后卡在 NPF 循环（原 NPF 日志为 `LOG_INFO` 被过滤）。
- 决策（用户已确认，2026-08-10）：`vmexit.c` NPF 分支对前 5 次 NPF 使用 `LOG_ERROR` 输出 `exits/np/ncr3/info1/GPA/RIP`，用于确认具体未映射的 guest 物理地址。
- 待验证：重载后若出现 `NPF early`，根据 GPA 扩大/修正 NPT 映射；若仍无任何出口，则怀疑 VMware 嵌套 NPT 执行问题，需换 KVM/裸机验证。

### 9.15 NPT 映射扩到 16GB（2026-08-10）

- 诊断结果：NPT 开启后首 5 次 NPF 均为 `GPA=0x203fb10f0 RIP=0xfffff80038bb306c`，guest 取指地址刚好超过 8GB（`0x200000000`），超出此前 identity map 范围，导致 NPF 无限重试。
- 决策（用户已确认，2026-08-10）：`main.c` NPT 映射范围从 `0x200000000`（8GB）改为 `0x400000000`（16GB），覆盖该 guest 物理地址并留余量。
- 待验证：重载后应不再出现 `NPF early`，KD 日志恢复 `heartbeat exits=1000/.../10000`。
- 验证通过（2026-08-10）：16GB 映射下 KD 日志完整出现 `NPT enabled: pml4=0x...` + `heartbeat exits=1000/.../10000` + `heartbeat limit reached`，无 `NPF early`；NPT 开启状态下 10000 轮 VMRUN/VMMCALL 稳定。构建 SHA256 `365F8D3D2F21DE679DC9BAD6354BDCFFFD52E68D65353993D89F4F96D352BD4C`。

### 9.16 NPT 权限/缺页注入测试（2026-08-10）

- 决策（用户已确认，2026-08-10）：实现 NPT 权限 API 并做一次有界缺页注入验证。
- 修改：
  - `npt.h`：新增 `NPT_PERM_PRESENT/WRITABLE/NX` 权限标志。
  - `npt_core.c`：实现 `npt_set_page_perm()`，按 GPA 定位 2MB large-page 条目并切换 P/W/NX。
  - `svm_trampoline.S`：新增 `svm_trampoline_test_npt_guest`，先 `mov rax,[rdi]` 访问受保护页触发 NPF，再 `mov rax,2`（STOP_INTERNAL）后 `vmmcall`。
  - `main.c`：分配 2 个 2MB 的连续测试页，取其中 2MB 对齐槽位；记录 GPA、清 P 位、把 guest RIP/RDI 指向测试入口；退出后释放。
  - `vmexit.c`：NPF 分支在 `g_npt_test_active` 时打印 `NPT test NPF`，恢复页面映射并重执行 guest，不注入 #PF。
- 预期：KD 日志出现 `NPT test page: va/pa`、`NPT test NPF: GPA=0x...`，随后 guest 以 STOP_INTERNAL 退出，`sc start` 返回。
- 验证通过（2026-08-10）：KD 日志出现 `NPT test page: va=0xffffa3791ce69000 pa=0xbf400000`、`NPT test NPF: GPA=0xbf400000 RIP=... info1=0x100000004`，随后无 NPF 循环；guest 恢复映射后以 STOP_INTERNAL 退出，`sc start` 正常返回。构建 SHA256 `702FEB29405344FFA887FC4EA01261B446171E208866C8AC9386FAB191B7D80D`。

### 9.17 不重启反复测试：DriverUnload + NtUnloadDriver（2026-08-10）

- 决策（用户已确认，2026-08-10）：加 `DriverUnload` 和卸载脚本，用 `NtUnloadDriver` 卸载 `yuanguard`，解决内核驱动 `sc stop` 报 1052、必须重启 VM 才能重载的问题。
- 修改：
  - `hv/main.c`：`DriverEntry` 成功路径不再自行 `npt_cleanup()/svm_core_cleanup()`，资源保留到 `DriverUnload`；`DriverUnload` 固定到 CPU0 后执行 NPT/SVM 清理，再恢复线程亲和性。
  - `hv/main.c`：`DriverEntry` 显式设置 `DriverObject->Flags |= DRVO_LEGACY_DRIVER`；`NtUnloadDriver` 会先检查该 legacy 标志，缺失时返回 `0xC0000010 (STATUS_INVALID_DEVICE_REQUEST)`。
  - `hv/npt_core.c`：`npt_cleanup()` 改为遍历每个 PDPT 的所有 PD 页并释放，修复 16GB 映射下非 0 号 PDPT 的 PD 页泄漏（每轮约 30MB 非分页池）。
  - 新增 `YuanGuardHV/unload_driver.ps1`：P/Invoke `NtUnloadDriver("\Registry\Machine\System\CurrentControlSet\Services\yuanguard")`，先启用 `SeLoadDriverPrivilege` 再卸载；未启用该权限时 `NtUnloadDriver` 返回 `0xC0000061 (STATUS_PRIVILEGE_NOT_HELD)`。
- 构建：`build.bat` SUCCESS，签名成功；新 SHA256 `bde6a8c1cf92edcd89b09d79db3df1c4d993a40fd7d89647fbf4e42d9069e1db`（含 `DRVO_LEGACY_DRIVER`）。
- 已复制 `unload_driver.ps1` 到 `D:\aaaaaavm\`；`yuanguard_hv.sys` 复制被 VM 内旧驱动占用，待 Guest 卸载后覆盖。
- 验证通过（2026-08-10）：Guest 用 `unload_driver.ps1` 成功卸载旧/新版驱动，服务由 `RUNNING` 变 `STOPPED`；随后不重启再次 `sc.exe start yuanguard` 正常 `RUNNING`，再卸载仍 `STOPPED`，两轮“启动 -> 卸载”均通过。KD 日志确认每轮 NPT test NPF 正常。

### 9.18 实际代码核对（2026-08-10 晚）

- 正式构建只编译链接 `main.c svm_core.c npt_core.c vmexit.c vmmcall.c svm_trampoline.S`；`multi_core.c`、`loader_stealth.c`、`pool/*`、`test_*`、`min_drv.c` 不进入 `yuanguard_hv.sys`。
- `main.c` 只初始化 CPU0 + 单 VCPU + NPT/NPF 测试；多核 API 存在但未调用，`multi_core.c` 未编译。
- VMCB intercept 只开 `VMRUN|VMMCALL`；CPUID/MSR/CR handler 存在但当前不可达。
- `vmmcall.c` 只实现 `HEARTBEAT/STOP_INTERNAL/VERSION/STATS`；`PROTECT_HANDLE/UNPROTECT/SCAN_PROCESS/READ_MEMORY/GET_CONFIG/SET_CONFIG/SHUTDOWN` 仅枚举。
- NPT 单页 2MB 权限已实现并验证；`npt_set_page_perm_range` 假成功，`npt_translate` 返回 0，无 2MB→4KB 拆分。
- `loader_stealth.c` 未编译未调用；`stealth.c` 不存在；CPUID 隐身 handler 死代码。
- `tests/`、`mod/`、Java 层均不存在。
- 结论：当前可复用的是“单核 SVM + NPT + 卸载重载”基础；多核、安全地基、隐形、保护功能、Java 层都还未进入正式构建。

### 9.20 R1 安全地基在 VMware 嵌套环境受阻（2026-08-11）

- 已实现代码：
  - `npt_translate` 真实遍历、`npt_set_page_perm_range` 循环应用、`npt_split_2mb_to_4kb`、`npt_exclude_pa/range/self`。
  - VMMCALL `STOP_INTERNAL/SHUTDOWN` 增加随机 cookie 校验；未知命令 fail-closed。
  - RAM 范围映射（`MmGetPhysicalMemoryRanges`）与 guest 代码页复制逻辑。
- 调试发现：
  - 系统线程里读 core1 SS 返回 0，已通过固定 ring0 长模式 SS 值绕过。
  - `MmAllocateContiguousMemory` 返回的直映射页在 Windows 下默认 NX，guest 从该页取指会导致 VMware 报“invalid part of memory”。
  - 从 Guest NPT 剔除 NPT 自页或 VMCB/hsave 等私有页后，VMware 嵌套 SVM 在 VMRUN 阶段崩溃/拒绝。
  - v21（回到 v7 路径 + 认证注入）在当前 VM 上仍卡住，且多次崩溃后 VM/KD 状态不稳定，出现启动期 0xD1。
- 结论：R1 安全地基无法在当前 VMware 嵌套 SVM 环境继续验证；需换裸机、KVM 或重建干净的 VMware 调试 VM。相关代码保留在树中，待稳定环境继续。

### 9.21 v22 稳定基线恢复（2026-08-11）

- 回退策略：回到 v7 已验证路径（驱动代码段执行 + 16GB NPT + IPI 准备 + 每核线程只跑 resident），保留认证 cookie 注入，关闭 NPT 剔除与 NPT 注入测试。
- 修复：`g_guest_code_page` 为 NULL 时无条件 `MmFreeContiguousMemory` 导致 0xC2，已全部加 NULL 保护。
- 构建：SUCCESS，签名成功；SHA256 `d66fbf223e8720c88e71c9cd567b34fbcaf8dee63d2f19cf4b28307d2f5ec4ed`。
- 验证：双核 10000 轮心跳通过；`unload_driver.ps1` 卸载后不重启再次 `sc.exe start yuanguard` 通过，服务 `RUNNING`。
- 2026-08-11 压缩会话后完整复验：当前 VMware 调试 VM 上重新加载 v22，双核心跳推进到 10000 并正常收尾；卸载后不重启再次加载，双核心跳再次到 10000；全程无 VMCB dump 错误、无 0xC2/0xD1、无 VMware invalid memory。测试完成后已卸载，VM 保持运行。
- 状态：当前 VMware 调试 VM 恢复可用；R1 加固代码保留但需稳定环境继续验证。

### 9.22 R1 第一步：NPT API 安全单测（v23，2026-08-11）

- 用户确认后实现，不改 VMRUN/多核路径，只做 host 侧 NPT API 验证。
- 新增 `npt_read_entry()`（`npt.h`/`npt_core.c`）：按 GPA 读回 4K/2MB 页表项原始值，供测试与诊断。
- 新增 `yghv_r1_npt_unit_test()`（`main.c`，`YGHV_R1_NPT_UNIT_TEST=1`）：分配 4MB 对齐缓冲区，验证 `npt_split_2mb_to_4kb` / `npt_set_page_perm` / `npt_set_page_perm_range` / `npt_translate` 的拆分、权限位（RW/NX）与身份映射，结果全部经 KD 日志输出。
- 明确不做：guest 触碰测试页、私有页剔除、NPT 自剔除、NPF 注入（这些留给稳定环境）。
- 构建：SUCCESS，签名成功；SHA256 `d38bc4b02733e12044b95b6a2ac500d19c396253169e810d7c71f33295d8bede`；已复制 `D:\aaaaaavm\yuanguard_hv_v23.sys`。
- v23 首次加载失败（StartService 31）：4MB 测试缓冲区只能保证一个 2MB 对齐页，`test_pa2` 越界，KD 日志 `r1 unit: no aligned 2MB slots`。
- v24 修复：测试缓冲区改为 8MB（`HV_LARGE_PAGE_SIZE * 4`），保证两个连续 2MB 对齐页；SHA256 `81bf9b4549c8e422efe1f33976e329d748c45cc55eb6ac8aede8a4d45dd6aaf3`；已复制 `D:\aaaaaavm\yuanguard_hv_v24.sys`。
- v24 加载仍失败：定位到 `npt_entry_t.pfn` 位域是 bit 12..51（4K 格式），但 2MB 大页 PFN 是 bit 21..51；`npt_translate` 大页分支与 `npt_split_2mb_to_4kb` 按 `pfn<<21` 双移位，导致 `npt_translate(0xbec00000)=0x17d80000000`。
- v25 修复：`npt.h` 新增 `NPT_PFN_4K`（bit 12..51）与 `NPT_PFN_2MB`（bit 21..51），`npt_translate` 大页分支与 `npt_split_2mb_to_4kb` 的 `base_pa` 改用 2MB 宏；SHA256 `8d2d9806fb92279d70422144f18142e5d5d1324697211c7feb0c64a7b834e671`；已复制 `D:\aaaaaavm\yuanguard_hv_v25.sys`。
- v25 验证通过：`r1 unit: PASS`，大页/4K 翻译、RW/NX 权限位、perm-range、restore 全部 rc=0，随后双核心跳 10000 正常收尾，VMRUN 未受影响。
- v26（NPF 注入测试）：`YGHV_R1_SKIP_NPT_TEST=0` 重新启用 guest 触碰被剔除页的 NPF 测试；同时修复两处诊断日志在 `g_guest_code_page=NULL`（v22 起 guest 代码走驱动 `.text`）时取物理地址的空指针风险，改用 `g_guest_npt_va`。SHA256 `514826f9f2ff893b9d1d4743a1779b8d7aea029b71224b2bafd7d984a03dc359`；已复制 `D:\aaaaaavm\yuanguard_hv_v26.sys`。
- v26 验证通过：`r1 unit: PASS`；`NPT diag trans_test=0`（剔除页翻译正确为 0）；`NPT test NPF` 触发后恢复映射，guest 重执行，随后双核心跳 10000 正常收尾。
- 状态：VMware 内 R1 安全部分（API 单测 + NPF 触发/恢复）验证完成；私有页剔除与 NPT 自剔除仍建议留给裸机/KVM。
- 2026-08-11 固化：v26 作为当前可信基线，git 提交检查点，覆盖 v22→v26 全部调试与修复记录。
- 2026-08-11 Phase 3 设计：方案 A 已确认；AMD SVM 无 RET 拦截，终止/句柄保护修订为补丁 stub + VMMCALL 决策 + NPT 写保护，正式设计见 `docs/superpowers/specs/2026-08-11-phase3-protection-design.md`。
- 2026-08-11 Phase 3 实现计划：见 `docs/superpowers/plans/2026-08-11-phase3-protection.md`；计划中终止/句柄第一版先用导出函数 `NtTerminateProcess`/`NtOpenProcess` 跑通 stub 链路，`PspTerminateProcess`/`ObpCreateHandle` 模式扫描作为后续增强。

### 9.19 Phase 2c 多核接线实现（2026-08-10）

- 决策（用户已确认）：把多核 resident 从 DPC 方案改为每核系统线程，并接入正式构建。
- 修改：
  - `build.bat`：编译/链接 `multi_core.c`。
  - `hv/multi_core.c`：重写为 `PsCreateSystemThread` + `KeSetSystemAffinityThread` 固定每核，线程在 PASSIVE_LEVEL 跑 `svm_core_enter_resident_current`；停止后 `KeWaitForSingleObject` join 并回收句柄/上下文，替代原 KDPC 写法。
  - `hv/svm_core.c`：`svm_core_prepare_vcpu_other` 先保存每核 `old_hsave/old_efer`；新增 `svm_core_ipi_cleanup`，清理时用 `KeIpiGenericCall` 在每核恢复 MSR 并仅在“我们自己开启”时清除 `EFER.SVME`，再释放 VCPU。
  - `hv/main.c`：DriverEntry 按 `KeQueryActiveProcessorCount` 为所有在线核分配 VCPU -> `KeIpiGenericCall(svm_core_ipi_prepare_vcpu)` 每核准备 -> 每核设置 NPT -> 先跑 CPU0 NPT 测试 -> 复位 CPU0 为心跳 guest -> `svm_core_start_remote_residents` -> CPU0 心跳 resident -> `svm_core_wait_all_stopped`。
  - `hv/common/svm_vcpu.h`：`svm_core_start_remote_residents` 改为返回 `NTSTATUS`。
- 构建：SUCCESS，签名成功；SHA256 `671bb4b111e92d5e90e6e1ea41ab95c652740bd109b4256215e37b9e14af2082`。
- 调试过程中修复：
  - `KeWaitForSingleObject` 不能直接等线程 HANDLE，改为 `ObReferenceObjectByHandle` 获取 `PETHREAD` 再等待。
  - ntddk 下 `PsThreadType` 是 `POBJECT_TYPE *`，需传 `*PsThreadType`，否则返回 `0xC0000024`。
- 验证通过（2026-08-10）：v7（SHA256 `f6c1a89c7d5a8622c7d0d402bd4c47cbc6bbcc0b6973f27c8c66ac3a66478b2a`）在 2 核 VM 上每核心跳跑满 10000，无 `ObReferenceObjectByHandle` 错误、无蓝屏；随后两轮“卸载 -> 再启动”均不重启通过，KD 日志均完整。

### 9.23 Phase 3 Task 1：protect 模块骨架（2026-08-11）

- 实现（按 `docs/superpowers/plans/2026-08-11-phase3-protection.md` 的 Task 1 简报）：
  - 新增 `YuanGuardHV/hv/common/protect.h`：保护状态/页表项/hook 结构、目标状态、页表遍历、NPT arm/disarm、hook 接口声明。
  - 新增 `YuanGuardHV/hv/protect.c`：`yghv_protect_guest_va_to_pa`、init/cleanup/set_target/add/remove/find/arm/disarm/start/stop。
  - `YuanGuardHV/hv/common/svm_defs.h`：新增 `SVM_EXIT_EXCEPTION_DB`。
  - `YuanGuardHV/build.bat`：`protect.c` 加入编译循环与链接列表。
- 偏差（简报代码无法直接编译）：`ntddk.h` 不声明 `PsLookupProcessByProcessId`，在 `protect.c` 增加该 API 的 `NTKERNELAPI` 前向声明；其余按简报逐字实现。
- 验证：`cmd /c build.bat`（`D:\yuanguard\YuanGuardHV`）→ `Build SUCCESS`，签名成功；sys SHA256 `8298dd0ca4dd7acc6179e6e374542744b4ab0dd0ed2757b778ce2abaafd8afa0`。R1 测试宏未改动（`YGHV_R1_SKIP_NPT_TEST=0`、`YGHV_R1_NPT_UNIT_TEST=1`）。
- 提交：`git commit -m "feat: protect 模块骨架（目标状态/页表遍历/NPT arm-disarm）"`。

### 9.24 Phase 3 Task 1 审查修复（2026-08-11）

- 审查发现的 3 处问题已按结论修复，仅改 `YuanGuardHV/hv/protect.c`：
  - `yghv_protect_arm_page`：先调用 `npt_split_2mb_to_4kb(&g_npt, p->gpa)`，失败直接返回；成功后再 `npt_set_page_perm(..., NPT_PERM_PRESENT)`，避免整个 2MB 大页被只读。
  - `yghv_protect_set_target`：先释放旧目标引用并清空 `process/cr3/pid`，再 `PsLookupProcessByProcessId`；CR3 先读入局部变量，非零才提交，CR3==0 时释放新进程引用并返回 `STATUS_INVALID_PARAMETER`，不触碰 `g_protect` 字段。
  - `yghv_protect_remove_page`：armed 页 disarm 失败时返回该状态并保留页表项，不删除；`yghv_protect_stop` 遍历 disarm，任一失败记录首个错误并返回 `STATUS_UNSUCCESSFUL`（`g_protect.active` 保持 TRUE），全部成功才置 FALSE。
- 验证：`cmd /c build.bat`（`D:\yuanguard\YuanGuardHV`）→ `Build SUCCESS`，签名成功；sys SHA256 `B2760FFCBFBD10FC5A9686F0C85DBAAD1CF4BA333179B20DD15B5C63436D3764`。仅预存 WDK intrinsic 警告与 `YGHV_DEBUG_LOG` 重定义警告，`protect.c` 无新增警告。
- 提交：`git commit -m "fix: protect 模块审查问题（2MB 拆分/目标回滚/disarm 失败处理）"`（仅 protect.c；docs/报告不提交）。

### 9.25 Phase 3 Task 1 复查修复：protect start 回滚一致性（2026-08-11）

- 发现：`yghv_protect_start` 在 arm 失败回滚时忽略 `yghv_protect_stop()` 返回值；若回滚期间 disarm 失败，会返回失败但 `g_protect.active` 仍为 FALSE，与仍有页面处于 armed 状态不一致。
- 修复（仅改 `YuanGuardHV/hv/protect.c`）：arm 失败时保存 `yghv_protect_stop()` 的返回值。若全部 armed 页（含失败页 `armed == 1` 的情况）disarm 成功，则 `g_protect.active` 为 FALSE 并返回原始 arm 失败状态；若任一 disarm 失败，则记录回滚失败、将 `g_protect.active` 置为 TRUE（仍有页面受保护），并返回 `STATUS_UNSUCCESSFUL`。
- 验证：`cmd /c build.bat`（`D:\yuanguard\YuanGuardHV`）→ `Build SUCCESS`，签名成功；sys SHA256 `C57DF990C99D42EE28B469CC5441FA8C9E418A148032DCF6C19CCEE8793AC7BE`（`Get-FileHash`）。仅预存 WDK intrinsic 警告与 `YGHV_DEBUG_LOG` 重定义警告，`protect.c` 无新增警告。
- 提交：`git commit -m "fix: protect start 回滚状态一致性"`（仅 protect.c；docs/报告不提交）。
- 2026-08-11 Phase 3 Task 2：控制面扩展（VMMCALL 命令）完成，`control_plane.h` 新增 `SET_TARGET 0x20 / ADD_PAGE 0x21 / REMOVE_PAGE 0x22 / START_PROTECT 0x23 / STOP_PROTECT 0x24 / GET_STATE 0x25 / HOOK_QUERY 0x60 / STATUS_DENIED 2 / STATUS_INVALID 3`，`vmmcall.c` 接入六个保护命令（未认证返回 DENIED）；构建 SUCCESS（SHA256 `e100b6c49929f5b22086691850e9c7761917a606ffa42b47713257e0a10c1445`）；提交 `f87612c`（仅两个代码文件）。
- 2026-08-11 Phase 3 Task 2 审查修复：`vmmcall.c` 的 `GET_STATE` 增加 cookie 认证（未认证返回 `YGHV_STATUS_DENIED`），`STOP_PROTECT` 改为返回 `yghv_protect_stop()` 的真实 `NTSTATUS`；构建 SUCCESS（SHA256 `99FDECD003F40F90B1EDBC0FCFA3F796CB023567DA10D8CEB3B1FA1A25EBA95F`）；提交仅含 `vmmcall.c`。
- 2026-08-11 Phase 3 Task 3：NPF 写决策 + #DB 重新加锁 + 常驻心跳完成（`svm_vcpu.h` 尾部加 `rearm_gpa`；`vmexit.c` 新增 `SVM_EXIT_EXCEPTION_DB` 分支并在 NPF 分支加 protect 写决策；`vmmcall.c` 心跳在 `g_protect.active` 时常驻不停止）；构建 SUCCESS（SHA256 `667F458AFDAB053FD020F59FC0F479D34D25F798096B9CF68407EA2886B10E25`）；提交 `7bab812`（仅三个代码文件，docs/报告不提交）。
- 2026-08-11 Phase 3 Task 3 审查修复：`svm_core.c` 的 `svm_prepare_vcpu()` 启用 #DB 异常拦截（`exception_intercepts = (1ULL << 1)`），使 `SVM_EXIT_EXCEPTION_DB` 单步重新加锁路径可到达，覆盖单核/多核常驻路径；构建 SUCCESS（SHA256 `79AE2085C2E0D8071A52451D7AA86F09978F698CFC247886AC6A7E93F0D96F46`）；提交 `620a5d4`（仅 `svm_core.c`，docs/报告不提交）。
- 2026-08-11 Phase 3 Task 3 复审修复：`vmexit.c` 的 #DB 分支在 `rearm_gpa==0` 时重新注入 guest #DB（向量 1）；外来用户态写 #PF 错误码由 `NPF_INFO1_*` 派生（含 U 位）；`arm_page`/`disarm_page` 返回值检查并记录失败；构建 SUCCESS（SHA256 `8D2126E823EF581D2CCD67CF8078F3373F3F76EA841DA162C6687CE3245AFA4A`）；提交 `5a80208`（仅 `vmexit.c`，docs/报告不提交）。
- 2026-08-11 Phase 3 Task 4 v27 VM 验证通过：`r1 unit: PASS`、`NPT test NPF`、`protect test: policy PASS`、`protect target pid=4 cr3=0x1ad000`、`protect: ring0 write allowed gpa=0xbf791000`、`protect test: real write PASS`、双核 `heartbeat limit reached`；完整验证内存页写保护 NPF→放行→单步→#DB 重新加锁链路。
- 2026-08-11 Phase 3 Task 4：驱动内保护测试完成（`svm_trampoline.S` 新增 `svm_trampoline_test_prot_write` 写陷阱 guest；`main.c` 新增 `yghv_protect_test()` 策略矩阵 + 真实写陷阱，调用点位于 NPT 测试块后、`svm_core_prepare_vcpu_other(0)` 前）；构建 SUCCESS，sys SHA256 `904A03B195F7AC30B1EB412F780B7BA3A3C9BE07B09FA2C4AFA344A2766D4BC2`，已复制 `D:\aaaaaavm\yuanguard_hv_v27.sys`；提交 `feat: 阶段1 内存页写保护 + 驱动内测试 v27`（仅两个代码文件，docs/报告不提交）。

### 9.26 Phase 3 Task 4 审查修复：保护测试失败回滚与返回值检查（2026-08-11）

- 审查发现的 3 处问题已按结论修复，仅改 `YuanGuardHV/hv/main.c`：
  - `yghv_protect_test()` 检查 `yghv_protect_set_target()` 返回值；失败时记录 `protect test: set_target FAILED 0x%x`、清掉合成 `g_protect.cr3` 并返回原状态，不再以 `0x1000` 伪 CR3 继续。
  - setup 失败路径拆分：`add_page` 失败仅释放缓冲区；`start` 失败先 `yghv_protect_stop()` 再 `yghv_protect_remove_page(buf_va)` 后释放。测试末尾检查 `stop`/`remove_page` 返回值，失败分别记录并返回对应状态（避免 `g_protect.active` 遗留 TRUE）。
  - DriverEntry 的 protect-test 失败路径先释放 `npt_test_buf` 并置 NULL，再调用 `yghv_protect_cleanup()`，随后才 `npt_cleanup()`，避免 NPT 测试缓冲与保护状态泄漏。
- 验证：`cmd /c build.bat`（`D:\yuanguard\YuanGuardHV`）→ `Build SUCCESS`，签名成功；sys SHA256 `2CAF175A4E78C1F11493D9F4572E45314EA7874FF722DB3C0BB0A915F2170E06`（`Get-FileHash D:\aaaaaavm\yuanguard_hv_v27.sys`）；仅预存 WDK intrinsic 警告与 `YGHV_DEBUG_LOG` 重定义警告，`main.c` 无新增警告。
- 提交：`git commit -m "fix: Task4 保护测试失败回滚与返回值检查"`（仅 main.c；docs/报告不提交）。
- 2026-08-11 Phase 3 Task 5：终止保护补丁 stub + HOOK_QUERY 决策完成（protect.c 实现 stub 生成/安装/卸载/查询决策/函数定位，vmmcall.c 接入 HOOK_QUERY；CR0.WP 改用 clang-cl 内联汇编包装；构建 SUCCESS，SHA256 `B7F30324D044DD2D84742D6E2406FD9CCE85F27A9B2ACA68A7440A65CBB6D8CA`，已复制 `D:\aaaaaavm\yuanguard_hv_v28.sys`；提交 `665b23a`，仅代码文件，docs/报告不提交）。
- 2026-08-11 Phase 3 Task 6：终止保护测试 v28 完整验证完成（main.c 新增 `yghv_hook_test()`：定位 NtTerminateProcess → 安装 hook 0 → 验证函数页 NPT 只读 → HOOK_QUERY 合成决策 OK/DENIED → 卸载 hook → 验证页可写且原字节恢复，失败同 protect test 回滚；构建 SUCCESS，sys SHA256 `10AE134ECB13C665C52E2C979843C8AAE70313A68606CE1122C1E823040C6CC4`，已复制 `D:\aaaaaavm\yuanguard_hv_v28.sys`；提交仅含 main.c，docs/报告不提交）。
- 2026-08-11 Phase 3 Task 6 审查修复：`yghv_protect_cleanup()` 在 `yghv_protect_stop()` 与清零前，遍历 `YGHV_PROTECT_MAX_HOOKS` 对每个 `installed` hook 调用 `yghv_protect_remove_hook(i)`（best-effort），避免卸载失败时 `NtTerminateProcess` 补丁字节未恢复、stub 页泄漏；仅改 `YuanGuardHV/hv/protect.c`，构建 SUCCESS，sys SHA256 `5CAB3BF48AC6A1C86DAE7F8C1996A54E296F96E31F51D66454B7EDCE6000135F`，已复制 `D:\aaaaaavm\yuanguard_hv_v28.sys`；提交仅含 protect.c，docs/报告不提交。
- 2026-08-11 Phase 3 Task 6 运行修复：v28 在 VM 中 `protect hook test: locate FAILED`；原因为主机 ntoskrnl 仅导出 `ZwTerminateProcess`、不导出 `NtTerminateProcess`，`MmGetSystemRoutineAddress` 返回 NULL。`yghv_hook_test()` 定位名改为 `L"ZwTerminateProcess"`，仅改 `YuanGuardHV/hv/main.c`；构建 SUCCESS，sys SHA256 `559BA50BD8D1233DC5FAB7353CF246733AB95A3DB04E4E024A6C0A262FE9154B`，已复制 `D:\aaaaaavm\yuanguard_hv_v28.sys`（旧文件因 VM 占用改名 `.locked` 后写入新文件）；提交仅含 main.c，docs/报告不提交。
- 2026-08-11 Phase 3 Task 6 蓝屏根因修复：v28 在 VM 加载时 0x7E/0xC0000096（#UD）于 hook stub；根因为测试在核心未常驻时给真实 `ZwTerminateProcess` 打补丁，并发调用在宿主模式执行 `vmmcall`。修复仅改 `YuanGuardHV/hv/main.c`：新增驱动内 16-NOP+ret dummy 函数作为 hook 目标，`ZwTerminateProcess` 仅保留诊断日志不再安装 hook；构建 SUCCESS，v28 SHA256 `A2F6E7312CA1163DA38D670A1EF0D1BC531716415E720D44B1D79E4B6570826D`，已复制 `D:\aaaaaavm\yuanguard_hv_v28.sys`；提交 `645ca49`（仅 main.c，docs/报告不提交）。
