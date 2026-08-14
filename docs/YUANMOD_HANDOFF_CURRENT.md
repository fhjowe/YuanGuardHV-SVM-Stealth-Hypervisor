# YuanMod / YuanGuardHV 当前交接与决策记录

> 接管人: Codex (/root) | 日期: 2026-08-09 | 仓库根: D:\yuanguard
> 本文件按协作铁律第 3 条维护，是项目所有修改与决策的记录，优先级高于普通文档。

## 1. 项目一句话

AMD-V SVM/NPT 隐形 Hypervisor（YuanGuardHV），替代原 YuanGuard 内核驱动，把 Minecraft/Forge 进程保护逻辑放到虚拟化层。

## 2. 当前进度快照（2026-08-13，详见 9.84-9.131）

| 里程碑 | 状态 |
|---|---|
| Phase 1 骨架 + 核心头文件 | 完成 |
| Phase 2a SVM init + VMRUN 单核 | 完成：10000 轮 VMMCALL 心跳稳定 |
| Phase 2b NPT identity-map + NPF | 完成：16GB identity map + NPF 权限注入验证通过 |
| Phase 2c 多核 DPC | 完成：每核系统线程，双核 10000 轮心跳稳定 |
| Phase 2d 物理机验证 | 完成：实机回归（9.40/9.126） |
| Phase 3 保护一版（v27-v31） | 完成：内存写/终止/句柄/常驻全部 VM 验证，已合并 main（`bf67ebc`） |
| Phase 3 隐形/Java/真实目标接入 | Java/真实目标接入完成（9.41/9.114/9.130）；隐形剩余 |
| git 版本控制 | 本次初始化完成 |
| 构建基线 | 成功，见第 5 节 |
| VM+KD 调试通道 | 不可用：VMware 嵌套限制，转裸机/KVM（9.6/9.7） |
| 不重启反复测试 | 完成：`DriverUnload` + `unload_driver.ps1` 两轮验证 |
| v22 基线复验（压缩会话后） | 完成：加载→双核心跳 10000→卸载→不重启重载→双核心跳 10000，全部通过 |
| R1 NPT API 单测（v23） | 进行中：代码已实现并构建，待 VM 加载验证 |
| R1 NPT API 单测（v25） | 完成：translate/perm-range/split 全部 PASS，双核心跳通过 |
| R1 NPF 注入测试（v26） | 完成：权限剔除→NPF→恢复映射→重执行→双核心跳通过 |

注意：7/30 的 `TECHNICAL_REVIEW.md` 已于 9.132 删除，P0 复核结论以
`TASKS.md` 第 1 节为准。

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
| 2026-08-11 | 用户确认下一步第 1 项：内核控制设备 + IOCTL 配置通道（v32） | 常驻基架已有 VMMCALL 协议但无用户态传输通道 | 新增设备对象/IOCTL/测试工具；跨核加锁、进程生命周期、hook 加固、CPL/CR3 认证不在本次范围 |
| 2026-08-11 | 用户确认第 2 项：常驻模式接入真实受保护页与真实 hook，stub 在 resident guest 中跑 allow/deny | 常驻基架仍为空转，hook stub 未在 guest 中执行 | 新增一次性 hook guest 测试 + CPU0 workload 常驻；仅 CPU0 访问受保护页避免跨核竞态（加锁仍留第 3 项） |
| 2026-08-11 | 用户确认第 3 项：NPT 共享状态加锁（g_protect/g_protect_hooks 跨 VCPU 保护）v34 | `npt_split_2mb_to_4kb` 用 `MmAllocateContiguousMemory` 只能 PASSIVE，自旋锁不可用 | `protect.c` 用 `FAST_MUTEX` 串行化共享状态与 NPT 权限修改；NPF/#DB 改走 on_npf_write/rearm 定点重锁；GET_STATE/HEARTBEAT 改快照 API |
| 2026-08-11 | 用户确认第 4 项：目标进程生命周期（EPROCESS 校验、退出自动 disarm、PID 复用防护）v35 | 当前 set_target 无退出监控，目标退出后页仍武装、CR3 可能被复用 | `PsSetCreateProcessNotifyRoutineEx` 退出回调按 EPROCESS 身份清除；`g_persistent_mode` 让 auto-disarm 不退出 resident；新增 exit-test 工具验证 |
| 2026-08-11 | 用户确认第 4 项改用 v36 轮询方案 | VM 的 ntoskrnl 对测试签名驱动调用 `PsSetCreateProcessNotifyRoutine`/`Ex` 都返回 0xC000007A 且残留回调，两次 0xCE | 删除进程通知注册；HEARTBEAT 每 10000 次退出用 `PsGetProcessExitTime`（MmGetSystemRoutineAddress 动态解析）轮询目标退出，命中则自动 disarm |
| 2026-08-11 | 用户确认第 5 项 hook 加固 v38：页边界/指令边界校验 + 跨核 rendezvous | 现有 install_hook 直接覆盖 16 字节，无边界校验；运行时 hook 无跨核保护 | install_hook 增加边界校验；resident 循环协作暂停（pause_requested/ack + 事件）后写入补丁；新增负向测试与运行时 rendezvous 测试线程 |
| 2026-08-11 | 用户确认第 6 项控制面安全 v39：管理命令限制调用方 CPL/CR3 | IOCTL 设备 FILE_ANY_ACCESS、任意进程可下发；VMMCALL 仅 cookie 认证 | IOCTL 句柄级绑定打开者 EPROCESS+CR3；VMMCALL 管理命令增加 cpl==0 && cr3==g_control_cr3 |
| 2026-08-11 | 用户确认隐形基础 v40：resident guest 的 CPUID 隐身 | `svm_emulate_cpuid` 是死代码（未开 CPUID 拦截） | svm_prepare_vcpu 开 INTERCEPT_CPUID；新增 cpuid guest 测试 0x40000000 段清 0、0x80000001 清 SVM bit、0x8000000A 清 0 |

## 7. 变更日志

| 日期 | 文件 | 变更 | 验证 |
|---|---|---|---|
| 2026-08-09 | `.gitignore` | 忽略 bin/obj/log/dump 等 | git 状态检查 |
| 2026-08-09 | `docs/YUANMOD_HANDOFF_CURRENT.md` | 创建本记录 | - |
| 2026-08-09 | `docs/TASKS.md` | 创建任务清单 | - |
| 2026-08-09 | 仓库 | git init + 首次提交 | git log |
| 2026-08-09 | `YuanGuardHV/build.bat` | for 块改 `call :compile` 子程序；INCLUDES/LIBPATH 路径加引号 | 构建成功并签名 |
| 2026-08-09 | `YuanGuardHV/svm_trampoline.asm` | 移除 clang 生成的中间汇编并加入 .gitignore | git 状态干净 |
| 2026-08-11 | `YuanGuardHV/hv/{common/control_ioctl.h,common/control_device.h,control_device.c}`、`hv/main.c`、`build.bat`、`tools/yghv_ctl.ps1` | Phase 3 下一步 Task A：IOCTL 控制设备配置通道 v32 | 构建 SUCCESS + VM 验证通过 |
| 2026-08-11 | `YuanGuardHV/hv/{svm_trampoline.S,main.c,vmmcall.c}` | Phase 3 下一步 Task B：常驻模式接入真实受保护页与真实 hook v33 | 构建 SUCCESS + VM 验证通过 |
| 2026-08-11 | `YuanGuardHV/hv/{protect.c,vmexit.c,vmmcall.c,control_device.c,main.c,common/svm_vcpu.h,common/protect.h}`、`tools/yghv_ctl.ps1` | Phase 3 下一步 Task C：NPT 共享状态加锁 v34 | 构建 SUCCESS + VM 验证通过（含并发 selftest） |
| 2026-08-11 | `YuanGuardHV/hv/{protect.c,main.c,vmmcall.c,common/protect.h}`、`tools/yghv_ctl.ps1` | Phase 3 下一步 Task D：目标进程生命周期（v35-v37，最终为 KeWaitForSingleObject 轮询判活） | 构建 SUCCESS + VM 验证通过（selftest + exit-test） |
| 2026-08-11 | `YuanGuardHV/hv/{protect.c,main.c,svm_core.c,common/svm_vcpu.h,common/protect.h}` | Phase 3 下一步 Task E：hook 加固（页/指令边界校验 + 跨核 rendezvous）v38 | 构建 SUCCESS + VM 验证通过（boundary + rendezvous + selftest/exit-test） |
| 2026-08-11 | `YuanGuardHV/hv/{control_device.c,vmmcall.c,main.c,common/control_plane.h}` | Phase 3 下一步 Task F：控制面安全（CPL/CR3）v39 | 构建 SUCCESS + VM 验证通过（selftest/exit-test 回归，无 unauthorized 拒绝） |
| 2026-08-11 | `YuanGuardHV/hv/{svm_core.c,svm_trampoline.S,main.c}` | Phase 3 下一步 Task G：隐形基础（CPUID 隐身）v40 | 构建 SUCCESS + VM 验证通过（cpuid stealth PASS） |
| 2026-08-11 | `YuanGuardHV/hv/{vmexit.c,svm_trampoline.S,main.c}` | Phase 3 下一步 Task G 扩展：resident guest 隐身矩阵（leaf1 hypervisor bit）v41 | 构建 SUCCESS + VM 验证通过 |
| 2026-08-11 | 仓库整理（git mv 参考文档/lib、logs_archive、.gitignore）+ `hv/{svm_core.c,vmexit.c,svm_trampoline.S,main.c}` | Phase 3 Task G 收窄 v42：CPUID 隐身矩阵 + 仓库整理（MSR/IO 隐身留裸机/KVM） | 构建 SUCCESS + VM 验证通过 |
| 2026-08-11 | `docs/NEXT_WINDOW_PROMPT.md`、`docs/SESSION_20260811.md`、`PLAN.md` | 文档同步到 v42 状态（用户指示） | 进度快照/会话记录/计划状态已更新 |
| 2026-08-11 | `YuanGuardHV/hv/main.c` | 实机适配 v43：r1 单测与 NPT 测试缓冲上限 0xFFFFFFFF → 0x400000000ULL（16GB identity 范围） | 构建 SUCCESS；实机测试进行中 |
| 2026-08-11 | `YuanGuardHV/hv/main.c`、`build.bat` | 实机 v48-v50：r1 小分配/hex trace、NPT 全内存映射、`YGHV_BAREMETAL_NO_RESIDENT` 冒烟开关 | 裸机非 VMRUN 冒烟通过；VMRUN 裸机冻结留待 KD/换机 |
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
- 2026-08-11 Phase 3 Task 6 dummy 断言修复：dummy hook 测试的 NPT 只读/字节恢复断言全部指向 dummy 函数页与 `g_protect_hooks[0].original`，`ZwTerminateProcess` 仅定位日志（term==0 不阻塞测试），`remove_hook` 返回值检查；v28 SHA256 `CCB94DEDEDCC7798FAAD066135C7358728EB486C4AEA21C0849E1B3E42A47AFF`；提交 `0f535cc`（仅 main.c）。
- 2026-08-11 Phase 3 Task 6 v28 VM 验证通过：`r1 unit: PASS`、`protect test: policy PASS`、`protect test: real write PASS`、`protect hook test: ZwTerminateProcess=0xfffff804825f86f0`、`protect hook 0 installed/denied/removed`、`protect hook test: PASS`、双核 `heartbeat limit reached`；无蓝屏。期间修复：CR0 写入改直映射（`fc8d810`）、add_page 4K 对齐 GPA（`707c997`），最终 v28 SHA256 `E71BC27594A9D96075DD1580E5FC8CD04B9DACB7F640C05C3406F085BAB6D961`。
- 2026-08-11 Phase 3 Task 7 v29 VM 验证通过：`protect hook test: ZwTerminateProcess=0xfffff804825f86f0`、`NtOpenProcess=0xfffff804828b9850`、hook 0/1 均 `installed/denied/removed`、`protect hook test: PASS`、双核 `heartbeat limit reached`；无蓝屏；v29 SHA256 `8E22FAC750653CE1242DBA07BC9D8229BC3C926626B6A998625162EFB3A37C62`。
- 2026-08-11 Phase 3 Task 8 v30 VM 验证通过：测试全 PASS 后 `persistent protect mode active: 2 cores`，常驻心跳滚动至 96 万次以上不停止，`unload_driver.ps1` 干净卸载回 `STOPPED`，无蓝屏；v30 SHA256 `34F210769ED3576EA6E1E354F1255CA6E1EAAC8A7930ED35686D7AF78113344C`。期间修复：常驻 VMCB 重准备（含 CPU0）、resident 状态原子化与生命周期、IPI 重准备、卸载顺序（提交 `e50af05`/`17670b7`/`ae252ca`/`c3bd618`）。
- 2026-08-11 Phase 3 进程保护一版完成：内存页写保护（NPF+单步重放）、终止保护（stub+VMMCALL 决策）、句柄保护（同框架）全部在 VM 验证通过；常驻保护模式 v30/v31 当前是“空转测试基架”（0 个受保护页、无真实 hook），真实目标页/hook 接入、加锁、目标进程生命周期、用户态配置通道（IOCTL/Java）属于下一阶段。
- 2026-08-11 Phase 3 Task 6 蓝屏根因修复 2：v28 #UD 根因是 VMware nested-SVM 对 `mov cr0, r13`（清 CR0.WP）抛 #UD。`protect.c` 删除 CR0 读写包装，install/remove/fail 的 16 字节补丁改经 `MmGetVirtualForPhysical(page_pa)` 直映射写入并用 `KeInvalidateRangeAllCaches` 刷新；构建 SUCCESS，v28 SHA256 `E937FDF3899AFE495D39902788B54DFEE3AFE266210491A7BCA9A1A509E04C12`，已复制 `D:\aaaaaavm\yuanguard_hv_v28.sys`；提交仅含 protect.c。
- 2026-08-11 Phase 3 Task 6 运行修复 2：v28 在 `protect hook 0: function page missing from table` 失败；根因是 `yghv_protect_add_page()` 保存了含页内偏移的 GPA，而 `yghv_protect_find_page()` 按 4K 对齐比较。`protect.c` 在 `yghv_protect_guest_va_to_pa()` 后加 `gpa &= ~(uint64_t)0xFFFULL;`，仅改该文件；构建 SUCCESS，v28 SHA256 `E71BC27594A9D96075DD1580E5FC8CD04B9DACB7F640C05C3406F085BAB6D961`，已复制 `D:\aaaaaavm\yuanguard_hv_v28.sys`（旧占用文件改名 `.locked.1`）；提交 `707c997`（仅 protect.c，docs/报告不提交）。
- 2026-08-11 Phase 3 Task 7：句柄保护测试 v29 完成（`main.c` 新增 `yghv_hook_test_dummy2` 并扩展 `yghv_hook_test()` 覆盖 hook 0/1，`NtOpenProcess` 仅定位诊断不安装；构建 SUCCESS，v29 SHA256 `8E22FAC750653CE1242DBA07BC9D8229BC3C926626B6A998625162EFB3A37C62`，已复制 `D:\aaaaaavm\yuanguard_hv_v29.sys`；提交仅含 main.c，docs/报告不提交）。
- 2026-08-11 Phase 3 Task 8：常驻保护模式 v30 完成（`multi_core.c` 新增 `svm_core_start_persistent_residents` 且 wait 从 core 0 起；`main.c` 测试后进入常驻保护、`DriverUnload` 先 stop+join 再 `yghv_protect_cleanup`；构建 SUCCESS，v30 SHA256 `F4F7766FE2849E5CBEE2BCBF683D82EBBE1B805E590BFBA169B5EBCEB5EEFF87`，已复制 `D:\aaaaaavm\yuanguard_hv_v30.sys`；提交 `ef068b9`，仅三个代码文件，docs/报告不提交）。
- 2026-08-11 Phase 3 Task 8 审查修复：按 5 条 review 结论修改，仅改 `YuanGuardHV/hv/multi_core.c` 与 `YuanGuardHV/hv/main.c`：
  - 常驻启动前对 0..online-1 每核 `svm_core_prepare_vcpu_other(i)` + 重设 cookie/RIP + `svm_core_set_npt(i, g_npt.pml4_pa)`，恢复 VMMCALL/NPF 拦截与 NPT（新 set_npt 失败路径含完整回滚）。
  - 成功路径释放 `npt_test_buf` 后置 NULL，消除常驻失败路径双重释放。
  - `yghv_resident_thread` 在发 ready 事件前置 `resident_state = SVM_RESIDENT_ACTIVE`，缩小 stop 漏标窗口。
  - `DriverUnload` 先 stop+join 全部 resident，再钉 CPU0 做清理，最后恢复亲和。
  - `yghv_protect_start()` 常驻失败路径补调 `yghv_protect_cleanup()`。
  - 验证：`cmd /c build.bat` → `Build SUCCESS`，签名成功；新 v30 SHA256 `10D07DB871130E047AFACEB700F90E18434F0D2F9ED4F5E02669874015609B87`，已复制 `D:\aaaaaavm\yuanguard_hv_v30.sys`；仅预存 WDK intrinsic/`YGHV_DEBUG_LOG` 警告。残余风险：`svm_core_enter_resident_current`（svm_core.c，范围外）无条件重写 ACTIVE，若 VM 卸载测试出现挂起需保留 STOPPING 标记。
- 2026-08-11 Phase 3 Task 8 启动/停止竞态修复：仅改 `YuanGuardHV/hv/svm_core.c`，在 `svm_core_enter_resident_current()` 的 VCPU 空检查后若 `resident_state == SVM_RESIDENT_STOPPING` 则置 `SVM_RESIDENT_STOPPED` 并直接返回，不再进入 VMRUN 循环，避免 `svm_core_wait_all_stopped()` 在卸载时挂起；验证：`cmd /c build.bat` → `Build SUCCESS`，签名成功，新 v30 SHA256 `150FDBA1F2094B79EFC9A0ABCE332921ACD3D0C268C2F00C085036F8E2E126EF`，已复制 `D:\aaaaaavm\yuanguard_hv_v30.sys`；仅预存 WDK intrinsic/`YGHV_DEBUG_LOG` 警告。
- 2026-08-11 Phase 3 Task 8 复审修复：resident 状态原子化 + IPI 重准备 + 失败路径先等 ready（`svm_core.c` 的 `svm_core_stop_all_residents()` 用 `InterlockedExchange` 对所有已分配 VCPU 置 STOPPING，`svm_core_enter_resident_current()` 与 `multi_core.c` 的 resident 线程用 CAS 完成 OFF→ACTIVE，CAS 见 STOPPING 即置 STOPPED 退出；`main.c` 常驻重准备改 `KeIpiGenericCall(svm_core_ipi_prepare_vcpu, 0)` 每核保存自己的 MSR_VM_HSAVE/EFER，失败路径先 `svm_core_wait_remote_ready(online)` 再 stop/join）；验证：`cmd /c build.bat` → `Build SUCCESS`，签名成功，新 v30 SHA256 `F4096564DC2E6F7CEDAED5D688D10805FB9CA97D451C5C2D4ECE9851CFDA867C`，已复制 `D:\aaaaaavm\yuanguard_hv_v30.sys`；仅预存 WDK intrinsic/`YGHV_DEBUG_LOG` 警告。
- 2026-08-11 Phase 3 最终修复 v31 VM 验证通过：`protect test: real write PASS` → 第二次写再次 NPF → `protect test: real write #2 PASS`（TLB 刷新生效）；hook 0/1 PASS；`persistent protect mode active: 2 cores`，常驻心跳 621 万次以上；`unload_driver.ps1` 干净卸载回 `STOPPED`；无蓝屏。v31 SHA256 `2905F527236C544F3BAD25478ED9E43BB87B9FC5E9CE4968E22D70EFD09DE64A`（提交 `c5fee91`，含 TLB 刷新 + rearm 全页重锁 + 二次写测试）。

### 9.27 Phase 3 下一步 Task A：IOCTL 控制设备配置通道（2026-08-11）

- 用户确认方案：新增内核控制设备 `\Device\YuanGuardHV`（符号链接 `\\.\YuanGuardHV`）+ METHOD_BUFFERED IOCTL，映射现有 protect API：SET_TARGET/ADD_PAGE/REMOVE_PAGE/START_PROTECT/STOP_PROTECT/GET_STATE；新增 PowerShell 客户端 `YuanGuardHV\tools\yghv_ctl.ps1` 做外部下发与 selftest。
- 范围限定：本次只做配置通道；跨核加锁（下一步第 3 项）、目标进程生命周期（第 4 项）、hook 加固（第 5 项）、控制面 CPL/CR3 认证（第 6 项）不在本次范围，IOCTL 当前为 `FILE_ANY_ACCESS`，安全项留待后续。
- 实现：
  - 新增 `YuanGuardHV/hv/common/control_ioctl.h`：设备类型 `0x5947`，IOCTL 0x800-0x805，固定输入/输出结构体。
  - 新增 `YuanGuardHV/hv/common/control_device.h`：设备 init/cleanup 声明。
  - 新增 `YuanGuardHV/hv/control_device.c`：`IoCreateDevice` + `IoCreateSymbolicLink` + IRP_MJ_CREATE/CLOSE/DEVICE_CONTROL 分发，buffered IO，未知命令返回 `STATUS_INVALID_DEVICE_REQUEST`。
  - `YuanGuardHV/hv/main.c`：常驻准备完成后、启动 persistent residents 前创建设备；persistent 启动失败路径与 DriverUnload 删除设备。
  - `YuanGuardHV/build.bat`：`control_device` 加入编译循环与链接列表。
- 新增 `YuanGuardHV/tools/yghv_ctl.ps1`：`state / set-target / add-page / remove-page / start / stop / selftest`；selftest 用当前进程 PID + 固定 4KB pinned 缓冲区走完整链路。
- 构建：`cmd /c build.bat` → `Build SUCCESS`，签名成功；v32 SHA256 `C73AD89346055559A0C7BCA9C3B9766E90616E57383DE6A93768AC780540D8D9`（`Get-FileHash D:\aaaaaavm\yuanguard_hv_v32.sys`），已复制 `D:\aaaaaavm\yuanguard_hv_v32.sys` 与 `D:\aaaaaavm\yghv_ctl.ps1`。仅预存 WDK intrinsic/`YGHV_DEBUG_LOG` 警告，`control_device.c`/`main.c` 无新增警告。构建中修复：`control_ioctl.h` 显式 `#include <winioctl.h>` 被 clang-cl 解析到 10.0.26100 um 头导致 `DWORD` 未定义，改为自包含 `YGHV_CTL_CODE` 宏。
- 宿主侧脚本冒烟：`yghv_ctl.ps1 state` 正确返回 `CreateFile \\.\YuanGuardHV failed, Win32 error 0x00000002`（驱动未加载时的预期路径）；期间修复客户端把 `INVALID_HANDLE_VALUE(-1)` 当成功句柄的检查，已改为同时比对 `IntPtr.Zero` 与 `IntPtr(-1)`。
- 验证（2026-08-11，VM 双核）：`sc start yuanguard` → RUNNING；`yghv_ctl.ps1 selftest` → `pid=3456 buf_va=0x112142575C8`、`set-target/add-page/start OK`、`state active=1 page_count=1`、`user write/read OK`、`selftest: PASS`；KD 日志确认 `persistent protect mode active: 2 cores`、`protect target: pid=3456 cr3=0x186dfe000`、`protect add_page: va=0x112142575c8 gpa=0x1417fb000`、`protect start: 1 pages armed`、常驻心跳滚动至 20 万+；`unload_driver.ps1` 干净卸载回 `STOPPED`，无蓝屏。
- 结论：IOCTL 配置通道（外部 SET_TARGET/ADD_PAGE/START/GET_STATE/STOP/REMOVE）在常驻模式下可用，真实用户页 NPT arm/disarm 生效；真实写入被 resident guest NPF 拦截的 allow/deny 路径仍属下一步（第 2 项）。

### 9.28 Phase 3 下一步 Task B：常驻模式接入真实受保护页与真实 hook（2026-08-11）

- 用户确认方案，实现：
  - `YuanGuardHV/hv/svm_trampoline.S` 新增 `svm_trampoline_test_hook_guest`（`call rsi` → `mov rdx,rax` 捕获 stub 返回 → STOP_INTERNAL）与 `svm_trampoline_test_resident_guest`（`rdi!=0` 时写受保护页、`rsi!=0` 时调用已 hook 的 dummy 并把结果存 rdx、然后 HEARTBEAT 循环）。
  - `YuanGuardHV/hv/main.c` 新增 `yghv_hook_resident_test()`：System 目标 + dummy hook，CPU0 resident guest 跑 allow（guest CR3=目标 CR3，期望 `rdx==0`）与 deny（guest CR3 保持有效，临时把 `g_protect.cr3` 偏移 0x1000 造成不匹配，期望 `rdx==0xC0000022`），跑完恢复并卸载 hook。
  - 常驻接入用 `YGHV_RESIDENT_WORKLOAD_TEST 1` 宏包住：持久化前配置 System 目标 + 驱动内 4KB 测试页 + dummy hook，再 `yghv_protect_start()`；CPU0 用 workload guest 常驻，其余核保持心跳；`vmmcall.c` HEARTBEAT 分支按 `g_protect` 刷新 `rdi/rsi`（仅当 guest CR3==目标时给 workload 值，否则给 0，保证外来目标下安全空转），每 10000 次退出日志 workload 状态与最后结果。
  - 清理：新增测试页在 DriverUnload 与相关失败路径释放；hook/页仍由 `yghv_protect_cleanup()` 统一清理。
- 范围：仅 CPU0 访问受保护页，避免两核同时 disarm/rearm 同一页的跨核竞态；跨核加锁（第 3 项）与真实系统函数 hook（继续用驱动内 dummy）不在本次范围。
- 构建：`cmd /c build.bat` → `Build SUCCESS`，签名成功；v33 SHA256 `17E5F0BED100C9862476F7C45F85823B25BF6B7F1C190D36ADA336692BF3FE14`（`Get-FileHash D:\aaaaaavm\yuanguard_hv_v33.sys`），已复制 `D:\aaaaaavm\yuanguard_hv_v33.sys`；仅预存 WDK intrinsic/`YGHV_DEBUG_LOG` 警告，`main.c`/`vmmcall.c`/`svm_trampoline.S` 无新增告警。
- 验证（2026-08-11，VM 双核）：`sc start yuanguard` → RUNNING；KD 日志 `hook resident test: allow rdx=0x0`、`hook resident test: deny rdx=0xc0000022`、`hook resident test: PASS`；`protect start: 2 pages armed`（workload 页 + hook 函数页）；`persistent protect mode active: 2 cores`；CPU0 workload 心跳滚动至 871 万次以上，日志 `heartbeat protect exits=... core=0 page=0xffffa4004f45c000 hook=0xfffff8049e832e00 last=0x0`，并出现 workload 页 NPF 放行 `protect: ring0 write allowed gpa=0xbf791000`；卸载时 `protect hook 0 removed`，服务回 `STOPPED`，无蓝屏。
- 结论：常驻模式已接入真实受保护页与驱动内 dummy hook；hook stub 在 resident guest 中 allow/deny 两路径均验证通过，NPF 写保护放行→#DB 重锁在常驻 workload 中持续工作。

### 9.29 Phase 3 下一步 Task C：NPT 共享状态加锁（2026-08-11）

- 用户确认方案：
  - `protect.c` 新增 `FAST_MUTEX g_protect_lock`（`ExInitializeFastMutex`），串行保护 `g_protect`、`g_protect_hooks` 及与其绑定的 NPT 权限修改（split/perm/arm/disarm）。原因是 `npt_split_2mb_to_4kb` 内部用 `MmAllocateContiguousMemory`（只能 PASSIVE_LEVEL），不能用在 DISPATCH_LEVEL 的自旋锁；现有所有访问点（resident exit handler、IOCTL、测试）均在 PASSIVE_LEVEL。
  - protect.c 重构为“取锁 → 内部 `_locked` 实现 → 放锁”；`set_target` 的 `PsLookupProcessByProcessId` 放锁外，进程引用替换与 CR3 提交在锁内。
  - 新增 `yghv_protect_on_npf_write(vcpu, gpa)`（锁内 find+is_target+disarm，返回 ALLOW/DENY/NONE，记录 `vcpu->rearm_gpa`）、`yghv_protect_rearm(vcpu)`（只重锁 `rearm_gpa` 页，`active==FALSE` 跳过，替代“重锁全部未武装页”）、`yghv_protect_get_state()` / `yghv_protect_get_heartbeat()` 快照 API。
  - `vmexit.c` NPF 写决策走 `yghv_protect_on_npf_write`，#DB 重锁走 `yghv_protect_rearm`；`vmmcall.c`/`control_device.c` GET_STATE 与 HEARTBEAT 改快照 API；`svm_vcpu.h` 新增 `rearm_gpa`；`main.c` DriverEntry 早段调 `yghv_protect_init()`。
  - 行为变化：disarm 失败时 NPF 走 DENY（注入 #PF）而不是原“不推进 RIP 直接重执行”的潜在死循环；测试中直接写 `g_protect.cr3` 的临时 hack 仍只在单线程测试期发生。
- 范围：只做共享状态串行化与 rearm 定点化；hook 加固、进程生命周期、控制面认证不在本次范围。
- 构建：`cmd /c build.bat` → `Build SUCCESS`，签名成功；v34 SHA256 `904676893913CED8A99145528A4F2D576DC931475CF691F8429E2EF84634FC84`（`Get-FileHash D:\aaaaaavm\yuanguard_hv_v34.sys`），已复制 `D:\aaaaaavm\yuanguard_hv_v34.sys`；仅预存 WDK intrinsic/`YGHV_DEBUG_LOG` 警告，`protect.c`/`vmexit.c`/`vmmcall.c`/`control_device.c`/`main.c`/`svm_vcpu.h` 无新增告警。
- 验证（2026-08-11，VM 双核）：`sc start yuanguard` → RUNNING；`hook resident test: allow rdx=0x0`、`deny rdx=0xc0000022`、`PASS`；`protect start: 2 pages armed`；CPU0 workload 心跳滚动 20 万+（`page=0xffffa400536f0000 hook=0xfffff8049e842ce0 last=0x0`）且 `protect: ring0 write allowed` 持续出现；并发运行 `yghv_ctl.ps1 selftest` 时 `set-target/add-page/start` 全部成功，加锁后状态一致（`active=1 pid=4956 page_count=3`，含基架 2 内部页 + 新增 1 页）；切换目标到 PowerShell 后 workload 安全变空转（`page=0x0 hook=0x0`，`last=0xc0000022` 为切换瞬间旧 rsi 调用的正确 DENY 结果）；卸载回 `STOPPED`，无蓝屏。
- 发现并修复：selftest 原先断言 `page_count==1`，与常驻基架默认 2 个内部页（workload 页 + hook 函数页）冲突导致 `state mismatch after start`；已把 selftest 改为“基线页数 + 1”比较（读取起始 page_count 作为 baseline），并增加 add-page 注册校验。仅改 `tools/yghv_ctl.ps1`，驱动代码无问题；待重跑 selftest 确认 PASS。
- 验证补完（2026-08-11）：重载 v34 后 `sc start yuanguard` → RUNNING；`yghv_ctl.ps1 selftest` → `baseline page_count=2`、`set-target/add-page/start OK`、`user write/read OK`、`selftest: PASS`；卸载回 `STOPPED`，无蓝屏。结合上一轮日志，allow/deny PASS、workload 20 万+ 心跳、IOCTL 与常驻 VCPU 并发访问共享状态加锁生效。
- 结论：第 3 项完成。`FAST_MUTEX` 串行化 `g_protect`/`g_protect_hooks` 与 NPT 权限修改，NPF/#DB 改走 `on_npf_write`/`rearm` 定点重锁（`active==FALSE` 跳过重锁），GET_STATE/HEARTBEAT 走快照 API；残余项为目标进程生命周期（第 4 项）与 hook 加固（第 5 项）。

### 9.30 Phase 3 下一步 Task D：目标进程生命周期（2026-08-11）

- 用户确认方案：
  - `protect.c` 用 `PsSetCreateProcessNotifyRoutineEx` 注册退出回调（Ex 版回调带 `EPROCESS`，按对象身份比较）；新增 `yghv_protect_on_target_exit(process)`：锁内比较 `g_protect.process == process`，命中则 `stop_locked` disarm、卸载已装 hooks、清空 `pages/target`、释放 EPROCESS 引用，日志 `protect target exited: auto disarm`；新增 register/unregister 接口。
  - `set_target` 增加 EPROCESS 基础校验：`pid != 0`、`PsGetProcessId(proc) == pid`、`cr3 != 0`（已有）；PID 复用防护 = 持有原 EPROCESS 引用 + 退出回调按对象身份清除，复用 PID 的新进程不会被当成旧目标。
  - 常驻保活与保护状态解耦：新增 `g_persistent_mode`，HEARTBEAT 在 `active || persistent` 时保持 resident 循环；目标退出自动 disarm 后 resident 不退出，可重新 set-target/start。
  - `main.c` persistent 启动前注册回调（并入现有失败回滚路径），`DriverUnload` 开头注销；`vmmcall.c` 心跳判断改 `active || persistent`；`yghv_ctl.ps1` 新增 `exit-test` 命令（起短暂子进程 → set-target → 等退出 → 轮询 GET_STATE 自动清空）。
- 范围：不含 hook 加固（第 5 项）与控制面认证（第 6 项）。
- 构建：`cmd /c build.bat` → `Build SUCCESS`，签名成功；修正版 v35 SHA256 `F91A9A3469D3F009ACBC36D5DEE8ACC60CDB34D9FAFB221BF7185279C413045E`（`Get-FileHash D:\aaaaaavm\yuanguard_hv_v35.sys`，旧占用文件已改名 `.locked`），已复制 `D:\aaaaaavm\yuanguard_hv_v35.sys` 与 `D:\aaaaaavm\yghv_ctl.ps1`；仅预存 WDK intrinsic/`YGHV_DEBUG_LOG` 警告，`protect.c`/`main.c`/`vmmcall.c` 无新增告警。
- 运行修复（2026-08-11）：VM 首次加载 v35 时 `sc start` 报 127（0xC000007A），KD 定位 `yghv_protect_register_lifecycle()` 返回 `STATUS_PROCEDURE_NOT_FOUND`；根因为 `PsSetCreateProcessNotifyRoutineEx` 要求调用模块带 `IMAGE_DLLCHARACTERISTICS_FORCE_INTEGRITY`，当前测试签名驱动不满足。修复：改用 `PsSetCreateProcessNotifyRoutine`（无 FORCE_INTEGRITY 限制），回调按 PID 比较；由于驱动持有目标 EPROCESS 引用，PID 在引用释放前不会被复用，身份判定仍安全。仅改 `protect.c`/`protect.h`，构建后重测。
- 蓝屏 0xCE 根因（2026-08-11）：第一版 v35（Ex API）的 `PsSetCreateProcessNotifyRoutineEx` 虽返回 `0xC000007A`，仍在进程通知链表残留指向已卸载驱动的回调；`sc.exe` 退出时 `nt!PspCallProcessNotifyRoutines` 跳到 `<Unloaded_yuanguard_hv.sys>+0x7400` → 页故障 → `DRIVER_UNLOADED_WITHOUT_CANCELLING_PENDING_OPERATIONS (0xCE)`。KD `!analyze -v` 栈确认。处置：`vmrun stop hard` + `vmrun start gui` + 重启 `kd_ctl.ps1` + `g` 放行，VM 已恢复；修复版 legacy 回调不存在该半注册状态。
- 二次验证失败（2026-08-11）：换 legacy `PsSetCreateProcessNotifyRoutine` 后 VM 仍报 `0xC000007A` 并再次 0xCE（同一 `+0x7400` 回调地址）。KD 确认该 build 的 legacy/Ex 共用 `PspCreateProcessNotifyRoutine` 表且测试签名驱动下注册/反注册都失败，回调残留。KD 同时确认 `PsGetProcessExitTime` 导出存在、`_KPROCESS` 无 `State` 字段。
- v36 方案（用户确认）：彻底删除进程通知注册（register/unregister/回调）；`protect.c` 用 `MmGetSystemRoutineAddress` 动态解析 `PsGetProcessExitTime`（缺失则禁用轮询并记日志）；`vmmcall.c` HEARTBEAT 每 10000 次退出调用 `yghv_protect_check_target_exited()`，退出时间非零则走现有 `yghv_protect_on_target_exit(pid)` 自动 disarm。保留 `set_target` 校验、`g_persistent_mode`、`exit-test`。
- 构建：`cmd /c build.bat` → `Build SUCCESS`，签名成功；v36 SHA256 `22D4AD058B626A963E559438A6D76289F3B78AB430DAD4BB4454E97E692EAE06`（`Get-FileHash D:\aaaaaavm\yuanguard_hv_v36.sys`），已复制 `D:\aaaaaavm\yuanguard_hv_v36.sys`；仅预存 WDK intrinsic/`YGHV_DEBUG_LOG` 警告，`protect.c`/`main.c`/`vmmcall.c` 无新增告警。
- v36 验证（2026-08-11）：`sc start` RUNNING、`selftest PASS`、卸载干净、无蓝屏；但 `exit-test` 未自动清空（`active=0 pid=916 page_count=2`）。KD 日志无 “lifecycle 禁用” 告警，说明 `PsGetProcessExitTime` 解析成功却对已退出子进程返回 0，判活未生效。
- v37 修复（用户确认）：判活原语改用 `KeWaitForSingleObject(g_protect.process, Executive, KernelMode, FALSE, 零超时)`，进程对象退出后变为 Signaled，返回 `STATUS_SUCCESS` 即判定退出；无新增内核导出依赖，轮询节奏不变。构建 SUCCESS，v37 SHA256 `2EA38EA2FF1833DF27C4D887D675D6F357CC71A168DF2E9724527E8EB66E8B47`，已复制 `D:\aaaaaavm\yuanguard_hv_v37.sys`；仅预存 WDK intrinsic/`YGHV_DEBUG_LOG` 警告。
- 验证（2026-08-11，VM 双核）：`sc start yuanguard` → RUNNING；`selftest: PASS`；`exit-test: PASS (active=0 pid=0 page_count=0)`（child pid=1692 退出后轮询自动 disarm）；KD 日志 `protect target: pid=1692 ...` → `protect target exited: auto disarm, pid cleared` → 心跳继续（`page=0x0 hook=0x0`）；`unload_driver.ps1` 干净卸载回 `STOPPED`，无蓝屏。
- 结论：第 4 项完成。目标进程生命周期采用“持 EPROCESS 引用 + HEARTBEAT 轮询 KeWaitForSingleObject 判活 + 退出自动 disarm/清空目标与页表”，PID 复用防护靠引用期间 PID 不复用 + 退出即清空。残余项为 hook 加固（第 5 项）与控制面认证（第 6 项）。

### 9.31 Phase 3 下一步 Task E：hook 加固（2026-08-11）

- 用户确认方案：
  - 页边界：install_hook 校验 `[func_va, func_va+16)` 不跨 4KB 页边界。
  - 指令边界：新增最小 x86-64 长度解码器（REX/0F/ModRM/SIB/disp/imm 常见形式），要求第 16 字节处为指令边界，跨边界或未知指令拒绝。
  - 跨核 rendezvous：svm_vcpu 增加 `pause_requested/pause_ack` 与暂停/恢复事件；resident 循环 VMRUN 前检查暂停标志，协作暂停后写入 16 字节补丁（install/remove 都走 pause/resume）；带超时上限。
  - 测试：负向边界用例（跨页、指令流跨 16）在 DriverEntry 校验；运行时测试线程（卸载时 join）在 persistent 运行期间对独立池页 install/remove hook 1，验证 pause/resume 且 workload 心跳不断。
- 范围：不加 IOCTL hook 接口、不改真实系统 hook；VMRUN 边界暂停对无退出的长函数不保证，留作后续 stop/restart 方案。
- 构建：`cmd /c build.bat` → `Build SUCCESS`，签名成功；v38 SHA256 `DD9F76E2D09DC1D1953D3848461982C260E2638A7685A8CAC208EF8B09264D26`（`Get-FileHash D:\aaaaaavm\yuanguard_hv_v38.sys`），已复制 `D:\aaaaaavm\yuanguard_hv_v38.sys`；仅预存 WDK intrinsic/`YGHV_DEBUG_LOG` 重定义告警，无新增告警。
- 验证（2026-08-11，VM 双核）：`sc start` RUNNING；`selftest: PASS`；`exit-test: PASS`；KD 日志 `hook boundary test: PASS`、`hook rendezvous test: install rc=0x0 / remove rc=0x0 / PASS`（persistent 运行期间对独立池页 install/remove hook 1，pause/resume 生效）；workload 心跳持续滚动（4.6M+）；卸载回 `STOPPED`，无蓝屏。
- 结论：第 5 项完成。install_hook 增加页边界 + 指令边界校验；install/remove 补丁写入走协作式跨核暂停（VMRUN 边界，超时 5s）。残余项为控制面认证（第 6 项）与 R1 私有页/NX（裸机/KVM）。

### 9.32 Phase 3 下一步 Task F：控制面安全（2026-08-11）

- 用户确认方案：
  - `control_device.c`：IRP_MJ_CREATE 用 `IoGetRequestorProcess` 捕获打开者 `EPROCESS` + `DirectoryTableBase`（0x028），存入 `FileObject->FsContext`；每个 DEVICE_CONTROL 校验请求进程与当前 CR3 都等于句柄所有者，否则 `STATUS_ACCESS_DENIED`；IRP_MJ_CLEANUP 释放上下文。
  - `control_plane.h`/`vmmcall.c`/`main.c`：新增 `g_control_cr3`（`svm_core_init()` 后从 `g_vcpus[0]->vmcb->state.cr3` 捕获）；`yghv_vmmcall_authorized(vcpu)` = cookie + `cpl==0` + `cr3==g_control_cr3`，用于 SET_TARGET/ADD_PAGE/REMOVE_PAGE/START/STOP/GET_STATE/HOOK_QUERY/STOP_INTERNAL/SHUTDOWN；HEARTBEAT/VERSION/STATS 保持开放。
- 范围：不加 token/提权校验（记入残余项）；客户端每进程开自己的句柄，无需改动。
- 构建：`cmd /c build.bat` → `Build SUCCESS`，签名成功；v39 SHA256 `B2BAE80BC6F7A9519A69A053ACE65DE730C0DDC694E61627B2430604906B104F`（`Get-FileHash D:\aaaaaavm\yuanguard_hv_v39.sys`），已复制 `D:\aaaaaavm\yuanguard_hv_v39.sys`；仅预存 WDK intrinsic/`YGHV_DEBUG_LOG` 重定义告警，无新增告警。构建中修复：`IoGetRequestorProcess` 在所用 WDK 头未声明，补 `NTKERNELAPI` 前向声明。
- 验证（2026-08-11，VM 双核）：`sc start` RUNNING；KD `control cr3=0x1ad000`；`selftest: PASS`；`exit-test: PASS`；`hook boundary test: PASS`；`hook rendezvous test: install rc=0x0 / remove rc=0x0 / PASS`；全程无 `unauthorized caller` 拒绝日志（各客户端进程用自己的句柄，句柄级 CR3 绑定未误伤）；卸载回 `STOPPED`，无蓝屏。
- 结论：第 6 项完成。IOCTL 每个句柄绑定打开者 EPROCESS+CR3；VMMCALL 管理命令要求 cookie + cpl==0 + cr3==g_control_cr3。残余：token/提权校验、R1 私有页/NX（裸机/KVM）、隐形、Java 层、真实系统 hook。

### 9.33 Phase 3 下一步 Task G：隐形基础（CPUID 隐身，2026-08-11）

- 用户确认方案：
  - `svm_core.c` 的 `svm_prepare_vcpu()` 启用 `INTERCEPT_CPUID`（general1 bit 18），使 guest 的 cpuid 全部进入现有 `svm_emulate_cpuid` 隐身逻辑。
  - `svm_trampoline.S` 新增 `svm_trampoline_test_cpuid_guest`：依次执行 `cpuid 0x40000000 / 0x80000001 / 0x8000000A`，结果存入 r8-r13（cookie 先存 r15 防 cpuid 清 rcx），最后 STOP_INTERNAL。
  - `main.c` 新增 `yghv_cpuid_stealth_test()`：CPU0 resident 跑该 guest，断言 r8-r11 全 0、r12 的 SVM bit 被清、r13 为 0，失败走现有回滚。
- 范围：只覆盖合成 resident guest 的 CPUID 隐身路径；整机级隐形（内存特征/MSR 时序/真实 OS 拦截）与 R1（裸机/KVM）为后续项。
- v41 扩展（用户确认）：`svm_emulate_cpuid` 增加 `cpuid 1` ECX 清 bit31（hypervisor present）；cpuid guest 增加 leaf 1 结果存 r14；`yghv_cpuid_stealth_test()` 断言矩阵扩展，VM 回归验证。
- v42 尝试与收窄（2026-08-11）：曾加 MSR 隐身（MSRPM 拦截 `MSR_VM_HSAVE/CR`）+ IO 隐身（IOPM 拦截端口 `0x5658`）。首次加载 0x50 崩溃（KD：`or byte ptr [rax+10004045h],1`，rax=msrpm，MSRPM 偏移公式未按 AMD 3 段布局折算）；修正公式后加载通过，但 stealth 测试显示拦截未生效：`msr=0xbfbf7000/0`（guest 原生读到宿主 hsave PA）、`io=0xffffffff`（VMware 原生后门响应），即 VMware 嵌套 L0 不放行 L1 的 MSRPM/IOPM 拦截。结论：MSR/IO 隐身与 R1 一样留到裸机/KVM；v42 收窄为“CPUID 隐身矩阵（v40/v41）+ 仓库整理”。
- v42 最终构建：`cmd /c build.bat` → `Build SUCCESS`，签名成功；最终 SHA256 `EDBD04C9845BC1C65897E7168163D318C782D68ECF6518182F253B02D6962214`（`Get-FileHash D:\aaaaaavm\yuanguard_hv_v42.sys`），已复制 `D:\aaaaaavm\yuanguard_hv_v42.sys`；仅预存 WDK intrinsic/`YGHV_DEBUG_LOG` 重定义告警，无新增告警。待 VM 重测（CPUID 矩阵 + 仓库整理回归）。
- v42 验证（2026-08-11，VM 双核）：`sc start` RUNNING；`selftest: PASS`；`exit-test: PASS`；KD `cpuid stealth test: leaf1_ecx=0x7ef83203 hyper=0x0/0x0/0x0/0x0 svm_ecx=0xc003f9 svm_leaf_eax=0x0` → `PASS`；boundary/rendezvous 仍 PASS；卸载回 `STOPPED`，无蓝屏。
- 仓库整理完成：`docs/reference/` 收纳参考文档；`reference/SymbolicAccessKM.lib`；`YuanGuardHV/logs_archive/` 收纳历史日志与中间 asm（gitignore）；`.gitignore` 已补。

### 9.35 文档同步（2026-08-11，用户指示）

- 更新 `docs/NEXT_WINDOW_PROMPT.md`：进度快照改为 v32-v42 状态，补充 yghv_ctl.ps1、logs_archive、MSRPM/IOPM 嵌套限制与 PsSetCreateProcessNotifyRoutine 雷区，下一步改为“实机验证 → Java 层 → 整机级隐形 → tests/mod/vm”。
- 追加 `docs/SESSION_20260811.md` 第 7 节：v32-v42 会话摘要与提交哈希。
- `PLAN.md` 顶部新增“0. 进度更新（2026-08-11）”状态块。
- 下一步：实机（裸机/KVM）测试，需用户确认 testsigning 与物理机环境后执行。

### 9.36 实机测试（2026-08-11，用户确认）

- 前置检查：管理员；`testsigning=Yes`、`nointegritychecks=Yes`；Ryzen 5 5500 SVM/NPT 可用；无 Hyper-V 服务。
- 首次尝试（v42）：`sc start` 失败 1450；`C:\Windows\yghv_progress.log` 停在 `npt_init ok`，定位为 `yghv_r1_npt_unit_test()` 返回 `STATUS_INSUFFICIENT_RESOURCES`——`MmAllocateContiguousMemory(8MB, 上限 0xFFFFFFFF)` 强制低 4GB 连续分配，本机低 4GB 碎片化失败。
- v43 修复（用户确认）：r1 单测 8MB 与 NPT 测试 4MB 缓冲上限改为 `0x400000000ULL`（16GB identity 范围）。SHA256 `FE26F8EC1C6CDC7012496180CEC012DCF3A33FFD2090197B4C7302A36B04DE09`，已复制宿主 `C:\yuanguard_hv.sys`。
- 待验证：宿主 `sc start yuanguard` → RUNNING → selftest/exit-test → unload STOPPED。
- 实机调试进展（2026-08-11）：
  - v44：r1 单测加里程碑 + DriverEntry 失败路径补 `yghv_trace_close()`（修 trace 句柄泄漏）；仍 1450。
  - v45：r1 缓冲改 4MB + 对齐重试；sc 报 31/1450 不一致，未通过。
  - v46：r1 单测改为单个 4KB 池页（`test_pa`）+ `test_pa2 = test_pa + 2MB`（NPT identity 覆盖，无需分配）；裸机 r1 全 PASS（`r1 unit pass`），推进到 `before npt test` 后首次 VMRUN（NPT 测试 guest）挂起，服务 START_PENDING，无法 sc stop。
  - v47：观测埋点（resident 循环每 10 万次退出写 `resident exit tick`、NPF 写 `npf test`、STOP 写 `stop_internal`）；构建 SUCCESS，SHA256 `ECA61110D329DEC3391E5FFBE4DE30E3A5398BD1DC81437BB5F360DEA5356FD2`。
  - 已提交代码 `0f9db0a`。下一步：重启宿主清除卡住的 v46，加载 v47 读 `C:\Windows\yghv_progress.log` 定位 VMRUN 挂起点。
- 实机调试续（2026-08-11，重启后）：
  - v47：r1 在重启后失败（`r1 test_pa=0x42a38a000`、`trans=0`）——池页落在 16GB identity 覆盖外，宿主实际 RAM > 16GB。
  - v48：加无 CRT 的 hex trace 确认上述地址；npt_translate 返回 0。
  - v49：NPT 改为 `MmGetPhysicalMemoryRanges()` 全内存映射 + 测试缓冲上限 64GB；全量加载再次硬冻结（18:39:55 强制重启，无 dump/WHEA），确认裸机 VMRUN 路径会冻结宿主。
  - v50：新增 `YGHV_BAREMETAL_NO_RESIDENT` 冒烟开关（跳过所有 resident/VMRUN，仅 r1 NPT API + hook 边界 + hook install/remove + 控制设备）；裸机加载 RUNNING，r1 `trans=test_pa` 全 PASS，宿主侧 `state`/`selftest` PASS，卸载回 STOPPED，无冻结。SHA256 `177B7D8C1CCCF62C68573B68041FF4FFB3F058C0C8D3AF8CB6E14D1DF568F5C7`。
  - 结论：裸机上非 VMRUN 路径已冒烟通过；VMRUN 首次进入即冻结宿主，需内核调试器或换机定位，R1 私有页/自剔除/默认 NX 与 MSR/IO 隐身继续阻塞。
- 逐步逼近 v51（2026-08-11，每步构建+裸机加载，均用户确认）：
  - Step 1：最小 VMRUN（NPT 关、无拦截，STOP_INTERNAL）PASS。
  - Step 2：+NPT 全内存映射 PASS。
  - Step 3：+CPUID 拦截 + stealth 矩阵 PASS。
  - Step 4：+NPF（权限清零页 guest 读）PASS。
  - Step 5：+写保护 allow/#DB 重锁 PASS。
  - Step 6：12 核多线程 VMRUN 心跳 PASS。
  - Step 7：hook stub 在 guest 中执行（HOOK_QUERY allow，rdx=0）PASS。
  - 结论：单点机制裸机全部正常，冻结点收敛到“常驻 workload 组合（CPU0 持续写受保护页 + 调 hook + 全核常驻 + rendezvous 线程）”。
- 常驻限核测试（2026-08-11）：
  - Step 8：全核常驻 + CPU0 workload（写保护 + hook）→ 冻结重启。
  - Step 8a：全核常驻纯心跳 + `ZwYieldExecution`（每 32768 次退出）→ 冻结重启。
  - Step 8b：仅 CPU0/1 常驻纯心跳 + yield → 冻结重启。
  - 最终结论：**该宿主（Ryzen 5 5500，>16GB RAM）对“非停止常驻 VMRUN”即使 2 核也会硬冻结**；所有有界/单次 VMRUN（Step1-7、Step6 十二核 10000 次心跳）均正常。判定为平台/硬件兼容问题（与本机 VMware 虚拟化路径硬卡死史一致），非驱动逻辑问题。裸机常驻测试停止；换机/KVM/VM 继续验证。
- v41 构建：`cmd /c build.bat` → `Build SUCCESS`，签名成功；SHA256 `349C32A95528B87F8449F3759F4ECF22B1F149A596C245EF59F34E32DFD25D7A`（`Get-FileHash D:\aaaaaavm\yuanguard_hv_v41.sys`），已复制 `D:\aaaaaavm\yuanguard_hv_v41.sys`；仅预存 WDK intrinsic/`YGHV_DEBUG_LOG` 重定义告警，无新增告警。
- v41 验证（2026-08-11，VM 双核）：`sc start` RUNNING；`selftest: PASS`；`exit-test: PASS`；KD `cpuid stealth test: leaf1_ecx=0x7ef83203 hyper=0x0/0x0/0x0/0x0 svm_ecx=0xc003f9 svm_leaf_eax=0x0` → `PASS`（leaf1 bit31 已清）；boundary/rendezvous 仍 PASS；卸载回 `STOPPED`，无蓝屏。
- 构建：`cmd /c build.bat` → `Build SUCCESS`，签名成功；v40 SHA256 `5578A00FDC271A77084973CA668EC5B93698E413C84DE56A48D99CEDABC03F91`（`Get-FileHash D:\aaaaaavm\yuanguard_hv_v40.sys`），已复制 `D:\aaaaaavm\yuanguard_hv_v40.sys`；仅预存 WDK intrinsic/`YGHV_DEBUG_LOG` 重定义告警，无新增告警。
- 验证（2026-08-11，VM 双核）：`sc start` RUNNING；`selftest: PASS`；`exit-test: PASS`；KD `cpuid stealth test: hyper=0x0/0x0/0x0/0x0 svm_ecx=0xc003f9 svm_leaf_eax=0x0` → `PASS`（0x40000000 段清 0、0x80000001 SVM bit 清、0x8000000A 清 0）；boundary/rendezvous 仍 PASS；卸载回 `STOPPED`，无蓝屏。
- 结论：隐形基础完成（resident guest 的 CPUID 拦截隐身路径可用）。整机级隐形、R1（裸机/KVM）、Java 层、真实系统 hook 为后续项。

### 9.34 仓库整理（2026-08-11，与 v42 一起）

- 用户确认范围：
  - `git mv`：`YuanGuardHV/reference_design.md`、`reference_tasks.md` → `docs/reference/`。
  - `git mv`：`YuanGuardHV/hv/SymbolicAccessKM.lib` → `reference/`（未使用、未审计，保守保留）。
  - 新建 `YuanGuardHV/logs_archive/`（加入 `.gitignore`），移入历史 `kd_stdout*/kd_stderr*/kd_test*/kd_auto/vmrun_start*` 等日志；`kd_ctl.log`/`kd_cmd.txt` 等运行文件留原位。
  - 删除两个被忽略的 `svm_trampoline.asm`（clang 中间产物）。
  - `.gitignore` 追加 `YuanGuardHV/logs_archive/`。
- 不动：核心代码、docs、脚本、证书、PLAN.md/HANDOFF.md/TECHNICAL_REVIEW.md。
- 执行：`git mv` 参考文档到 `docs/reference/`、`SymbolicAccessKM.lib` 到 `reference/`；历史日志与中间 asm 移入 `YuanGuardHV/logs_archive/`（gitignore）；`.gitignore` 追加 logs_archive。

### 9.38 常驻 VMRUN 冻结研究 + INTR/NMI/SHUTDOWN 拦截修复（2026-08-11）

- 用户问题与范围：解释“非停止常驻 VMRUN”是什么；网络调研是否有解；没有它时的后果；确认后实施修复并做裸机验证。
- 研究结论：
  - 非停止常驻 VMRUN = 每核无限 `VMRUN → VMEXIT → 处理 → VMRUN` 循环，CPU 长期停留 guest mode；当前合成 guest 是心跳循环（`mov rax,1; vmmcall; jmp`），退出率百万级/秒。有界（10000 次/STOP_INTERNAL）全 PASS；无限常驻在裸机 2 核也硬冻结。
  - 代码缺口：`svm_prepare_vcpu()` 只开 CPUID 拦截，`INTERCEPT_INTR/NMI/SHUTDOWN` 均未开；`vmexit.c` 的 `SVM_EXIT_INTR/NMI/SHUTDOWN` 分支是死代码。guest RFLAGS.IF=1 → 物理中断在 guest mode 内用宿主 IDT/GDT/CR3 执行 Windows ISR；合成 guest 不是真 OS-as-guest，这是与 KVM/bhyve 最本质的差异。
  - AMD APM：不拦截 SHUTDOWN 时 guest 三重故障会直接处理器 shutdown（无 dump 硬复位），与现象吻合。
  - AMD Zen3 errata（56683，Milan 同代 19h）：1363 持续 APIC 寄存器访问流可致系统挂起/复位；1407/1415/1450 特定时序致命 IF/LS MCA 可挂起/复位，无软件绕过（靠 BIOS/microcode）。KVM 2025 STI shadow/VMRUN quirk（be45bc4）与事件注入相关，本 trampoline `CLGI→VMRUN` 大概率不中招。
  - 结论：有可修路径，优先验证 INTR/NMI/SHUTDOWN 拦截；若仍冻结再收集 WHEA/换 BIOS/换机/KVM 对照。
- 无常驻 VMRUN 的后果：NPT/NPF 与 hook stub vmmcall 只在 guest mode 生效，无法持续保护真实目标；真实系统 hook 在非 resident 核会 #UD（已记录）；整机隐形不可行；没有等价替代架构，只能修稳常驻或换环境。
- 修复（用户确认，v52）：
  - `svm_core.c` 的 `svm_core_enter_resident_current()` 入口 OR 入 `INTERCEPT_INTR | INTERCEPT_NMI | INTERCEPT_SHUTDOWN`（所有 resident 路径统一生效；退出 devirtualize 仍清零）。
  - `main.c` 新增 step 11 = 有界 2 核心跳 + 三拦截（把每核 `resident_interrupt_exits/exits` 写入进度日志）；step 10 保持 2 核非停止常驻纯心跳作为长跑测试。
- 构建：step11 SHA256 `987FC1637E456D154137E0ECDEA79E861130388980DCDC46CE88A6EF0C0AA4F9`（归档 `D:\aaaaaavm\yuanguard_hv_v52_step11.sys`）；step10 SHA256 `91599A1FA6C77833ED17AE99C17036DDB0A049FD50FF696082B8B2BA76299405`（归档 `D:\aaaaaavm\yuanguard_hv_v52_step10.sys`）。仅预存 WDK intrinsic/`YGHV_DEBUG_LOG` 告警。
- 裸机验证（Ryzen 5 5500）：
  - Step 11 有界 2 核：`sc start` RUNNING，日志 `bm step=0xb` → `bm bounded hb 2core intr done`；`bm s11 c0=0xf / ext=0x2032`、`c1=0x4 / ext=0x2710`，证明 INTR 拦截真实生效，无冻结。
  - Step 10 非停止 2 核：RUNNING 后连续观察约 7 分钟（90s + 300s + 操作时间），主机始终响应，核 0/1 `% Processor Time` 均 100%（resident 持续运行），`bm persistent hb 2core running`；`sc stop` 干净回 `STOPPED`，无冻结/蓝屏。
  - 结论：**INTR/NMI/SHUTDOWN 拦截是裸机常驻冻结的关键修复路径**（此前 Step8b 同配置 2 核纯心跳+yield 冻结；本版同配置 7 分钟稳定且可卸载）。
- 残余与下一步：全核常驻/真实 workload（Step8 形态）未测；VM 回归（selftest/exit-test）待用户启动 VM；BIOS/microcode errata 风险仍在，若长跑复现再查 WHEA 日志。

### 9.39 裸机常驻全矩阵验证（v53，2026-08-11）

- 用户确认继续下一步：全核常驻纯心跳 + Step8 形态（全核常驻 + CPU0 workload）裸机长跑。
- Step 9（全核常驻纯心跳 + INTR/NMI/SHUTDOWN 拦截）：构建 SHA256 `454CE0B3F1FEB507D39621238871CAB3356847EE38193A66B41A4871D0403800`；`sc start` RUNNING 后连续观察约 6.5 分钟（90s + 300s），主机始终响应，12 核均有活动，日志 `bm persistent hb running`；`sc stop` 干净回 `STOPPED`。此前 Step8a（同形态）冻结。
- Step 8（全核常驻 + CPU0 持续写受保护页 + hook 调用 + 其余核纯心跳）：构建 SHA256 `A40B734E3E8CFED65BB935B4A553B0208E393109D3A2C257B1506901C7881319`；`sc start` RUNNING 后连续观察约 6.5 分钟，主机始终响应，日志 `bm persistent running`；`sc stop` 干净回 `STOPPED`。此前 Step8（同形态）冻结。
- 结论：**INTR/NMI/SHUTDOWN 拦截修复覆盖全部裸机常驻场景（2 核/全核、纯心跳/workload），每项约 6.5 分钟长跑稳定且可卸载**；裸机常驻冻结问题基本关闭。
- 归档：`D:\aaaaaavm\yuanguard_hv_v53_step9.sys`、`D:\aaaaaavm\yuanguard_hv_v53_step8.sys`（step9 归档件为重编译，SHA256 `31D3FFFCFA316D99...`，与测试件 `454CE0B3...` 同源，仅 PE 时间戳差异）。
- 残余：VM 回归（selftest/exit-test）待用户启动 VM；小时级长跑与 BIOS/microcode errata 风险未排除；后续可选去掉 `ZwYieldExecution` 再做无 yield 常驻长跑，验证真实 OS-as-guest 形态。

### 9.40 默认版 v54 本机实机完整流程验证（2026-08-11）

- 用户决定跳过 VM 回归，直接实机测试（最终运行目标即本机）。
- 默认版 v54（无 `YGHV_BAREMETAL_STEP`/`YGHV_BAREMETAL_NO_RESIDENT`）：构建 SHA256 `F531C366A8B43688C1D368F2B8769174FBC7EA069176B62B4C832AA988A3F9C2`，归档 `D:\aaaaaavm\yuanguard_hv_v54.sys`。
- 本机加载：`sc start yuanguard` → RUNNING；进度日志完整走到 `r1 unit pass` → `npt set ok` → NPT 测试（`npf test`）→ protect/hook 各 resident 测试 → 多核 `before/after heartbeat` → `all stopped`，全程无冻结。
- 控制面回归（宿主侧 `yghv_ctl.ps1`）：`selftest: PASS`（pid=12652，set-target/add-page/start/user write-read 全 OK）；`exit-test: PASS`（child pid=2432 退出后自动 disarm）。
- 常驻观察：selftest/exit-test 后保持常驻约 90 秒，系统始终响应；`sc stop` 干净回 `STOPPED`。
- 结论：**完整产品流程（常驻模式 + IOCTL 控制设备 + 目标生命周期 + 干净卸载）已在本机实机通过**，裸机不再只是冒烟/有界测试。
- 下一步候选：小时级实机长跑；真实目标进程接入（Java/Minecraft 通过 IOCTL）；R1 私有页剔除/NPT 自剔除/默认 NX 在本机启用验证。

### 9.41 真实目标进程接入：Java/JNI IOCTL 客户端（2026-08-11）

- 用户选择方向 2：真实目标进程接入。本机有 JDK 21（Zulu，`D:\DevTools\zulu21`）。
- 新增 `YuanGuardHV\tools\yghv_client\`（未改驱动内核）：
  - `YghvCtl.java`：Java 命令行客户端，命令 `state / set-target / add-page / remove-page / start / stop / list-java / protect <pid> [maxPages]`。
  - `native\yghv_ctl_jni.c`：JNI 桥封装 `CreateFile/DeviceIoControl`（IOCTL 与 `control_ioctl.h` 对齐），`enumeratePages` 用 `VirtualQueryEx` 枚举目标进程已提交页，优先 `MEM_IMAGE` 可执行映像页（上限 64，匹配 `YGHV_PROTECT_MAX_PAGES`）。
  - `build_jni.bat / build.bat / run.bat / test\Sleepy.java / README.md / .gitignore`（构建产物忽略）。
- 实机验证（v54 默认版加载，真实 Java 进程 PID=13240）：
  - `protect 13240 64`：set-target OK，枚举 64 页，add-page 成功 37 页（`0x7FF680AF0000...` 可执行映像页），start OK，`state: active=1 pid=13240 page_count=39`。
  - 10 秒后 state 仍 `active=1 pid=13240 page_count=39`（目标存活期保护保持）。
  - 强杀 PID 13240 后轮询，state 自动变 `active=0 pid=0 page_count=0`（**真实 Java 目标退出自动 disarm 生效**）。
  - `run.bat list-java` 能列出 java 进程；与 PowerShell `yghv_ctl.ps1 state` 交叉核对一致；`sc stop` 干净卸载。
- 边界（已写入 README）：当前架构下真实进程代码仍跑 native，NPT/vmmcall 不会真正拦截它的写；本步交付的是真实目标接入的配置/页表通道与 Java 层基座。真实拦截需 OS-as-guest 里程碑。
- 残余：Minecraft/Forge 实机接入未测；`protect` 目前按枚举页批量 add（部分页因 PTE 非 present 翻译失败属预期）；后续可加驱动侧“按目标 CR3 的合成写测试”验证 NPF 决策。

### 9.42 v55：真实 Minecraft/Forge 目标接入修复（2026-08-11）

- 用户选择直接用真实 Minecraft/Forge 进程测试；本机环境为 PCLN 启动的 Minecraft Forge 1.20.1（Forge 47.4.20，JDK 17 javaw.exe，PID 1788，用户名 Sejit，游戏目录 `Desktop\114514\.minecraft\versions\1.20.1-Forge_47.4.20`）。
- 根因（实测定位）：
  - `javaw.exe` 映像页多为 demand-paging，进程 PTE Present=0，驱动的 `yghv_protect_guest_va_to_pa()` 手工四级遍历返回 0（`STATUS_INVALID_ADDRESS` → Win32 0x3B）；`ReadProcessMemory` 能读但不保证让进程 PTE 变 Present；同一 VA 时而 PowerShell 可加、时而 Java 全失败，表现像瞬态。
  - `set_target` 不清空上一目标页表，Sleepy 测试页占满 64 上限后 add-page 报 `STATUS_INSUFFICIENT_RESOURCES`（Win32 0x5AA）。
- v55 修复（仅改 `YuanGuardHV/hv/protect.c`）：
  - `add_page` 翻译改用 `KeStackAttachProcess(g_protect.process)` + `MmGetPhysicalAddress()`（内核自身页表翻译），首次失败时用 `MmCopyVirtualMemory(KernelMode)` 缺页调入 1 字节后重试；`#include <ntddk.h>` 改 `<ntifs.h>`（`KAPC_STATE/KeStackAttachProcess` 在 ntifs.h）。
  - `set_target` 切换目标时先 disarm 已 armed 页并清零旧页表，任一 disarm 失败则返回 `STATUS_UNSUCCESSFUL` 不切换。
  - 客户端（`YghvCtl.java`/`yghv_ctl_jni.c`）：枚举候选扩到 `maxPages*8`（512），用 `ReadProcessMemory` 预探测，add-page 只累计成功页并打印前 3 个失败错误码/地址；修复 JNI 候选缓冲区按 512 分配（曾因 64 分配堆溢出导致 JVM 崩溃）。
- 构建：v55 默认版 SHA256 `2F94C4C467CC22715F28F49A3646E0319342D72D5D775D4D2CCF46E3868882C7`，归档 `D:\aaaaaavm\yuanguard_hv_v55.sys`。
- 实机验证（Minecraft Forge PID=1788）：
  - `run.bat protect 1788 64` → set-target OK、枚举 512 页、**add-page ok=64（全部 javaw 映像页）**、start OK、`state: active=1 pid=1788 page_count=64`。
  - 60 秒常驻观察系统始终响应；`run.bat stop` disarm OK；`sc stop` 干净回 `STOPPED`；Minecraft 进程保留运行。
- 边界不变：本步是真实目标接入/页表 arm 通道；真实写拦截仍需 OS-as-guest。

### 9.43 v56-v60：VM 回归失败与实机冻结修复（2026-08-11）

- 用户要求先补 VM 回归（v52-v55 只在实机验证过）。
- v56（CPUID leaf1 bit31 嵌套检测，嵌套时关 INTR/NMI 只留 SHUTDOWN）：VM 仍整机冻结，说明 VMware 下问题不只是 INTR/NMI 拦截。
- v57（嵌套检测加强为 VMwareVMware/Hyper-V 字符串，嵌套时三个拦截全关，完全回到 v42 行为）：VM 仍冻结；日志定位停在 `before heartbeat`（多核心跳测试），r1/NPT/protect/hook 全过。
- v58（诊断：trace 每次同步刷盘 + 心跳每 1000 次退出写 `hb c0/c1`）：VM 仍冻结，且**实机 v58/v59 也硬冻结重启**。根因是诊断刷盘进了常驻热路径：12 核常驻下每秒成千上万次 `ZwFlushBuffersFile`，形成 I/O 风暴/文件锁等待死锁。
- v59（NPT 测试缓冲 `HighestAcceptableAddress` 0x1000000000 → -1，与 v49 全内存映射对齐）：实机仍冻结，且首次出现 1450；查内存只剩 2.2GB（VMware VM 占 8.3GB），关 VM 后空闲 10.7GB。
- v60（用户确认，清理诊断副作用）：
  - 移除 `yghv_trace` 的 `ZwFlushBuffersFile`（恢复普通缓冲写）；
  - 移除 vmmcall 心跳每 1000 次的 `hb c0/c1` trace；
  - 保留 v59 的分配上限 -1 修复；
  - `yghv_ctl.ps1 selftest` 断言改为适配 v55 语义：`set_target` 会清空旧页表 → set-target 后 page_count=0、add-page 后=1、stop+remove 后=0。
- 实机验证（Ryzen 5 5500，重启后内存充足，无 VM）：v60 默认版 SHA256 `35261AC747C8F7006DD67C19FCAE34C062296233CBEF064B5577FA314A7E0BC9`，归档 `D:\aaaaaavm\yuanguard_hv_v60.sys`；`sc start` RUNNING → `selftest: PASS` → `exit-test: PASS` → 60 秒常驻观察正常 → `sc stop` 干净回 STOPPED。
- 结论：**实机冻结由 v58 诊断刷盘引起，已清除；v60 与 v54/v55 稳定性一致**。VM 多核心跳冻结仍未解决（用户决定暂停 VM，走实机路线）；下一步回到 OS-as-guest Phase A（实机有界试点）。

### 9.44 OS-as-Guest Phase A：单核无缝有界试点（2026-08-11，待实机加载）

- 用户确认进入整机（OS-as-guest）里程碑，按“直接实机测试”路线推进。
- 设计文档：`docs/superpowers/plans/2026-08-11-os-as-guest.md`。
- 实现（step 12，单核 core1 有界 OS guest）：
  - `svm_trampoline.S` 新增 `svm_trampoline_os_enter`：保存当前线程上下文为 guest，
    切专用 host 栈，trampoline 内自持 VMRUN/VMEXIT 循环；停止后跳转 C 收尾。
  - `main.c` 新增 `yghv_os_guest_main`（guest 内执行 5000 次 CPUID/RDTSC + 共享计数，
    结束置 stop 旗标并触发一次 CPUID）、`yghv_os_guest_host_done`（host 收尾）、
    `yghv_os_guest_thread`（core1 线程 + OS guest VMCB profile：只开
    CPUID/SHUTDOWN/VMMCALL，guest IF=0）、step 12 编排（60s 超时等待）。
  - `vmexit.c`：`svm_dispatch_exit` 顶部检查 `g_os_guest_stop`，guest 请求停止即停。
  - `svm_vcpu.h`：声明 `svm_trampoline_os_enter`/`svm_prepare_vcpu`。
- 构建：step12 SHA256 `F154215B0D74837E27A42BCCB7664C306E9192D45B49F85B94A5B242BC18BE02`，
  归档 `D:\aaaaaavm\yuanguard_hv_v61_step12.sys`（复制后）。
- 状态：已编译通过，**未在实机加载**；加载有硬冻结风险（VMRUN 不退或 guest 上下文错），
  需用户确认后执行；判据见计划文档。

### 9.45 OS-as-Guest Phase A：实机两次崩溃与根因修复（2026-08-11）

- 用户确认加载 step12（v61，F154215B）。首次实机蓝屏 **0x7E / 0xC0000005**，
  dump 定位 `yuanguard_hv+0x98c7`（`mov [rcx+0xE8],rax`）写入非法地址，调用方
  `yuanguard_hv+0x24b3`（`yghv_os_guest_thread` → `svm_trampoline_os_enter`）。
- 根因（反汇编实锤）：`svm_trampoline_os_enter` 入栈顺序为
  `rax,rdx,rcx,rbx,...`，但我 pop 顺序写成了 `...,rbx,rdx,rcx,rax`，
  **rdx/rcx 顺序颠倒** → `rcx` 拿到调用者 rdx 的垃圾值 → `mov [rcx+0xE8],rax`
  越界写 → 0x7E。
- 第二次（修复 host_done 栈恢复后，v61 修正版 E8C97F0D）：实机硬冻结无 dump；
  同一 pop 顺序 bug 仍在（rcx 被破坏后 VMRUN 进入坏上下文 → 冻结）。
- 修复：
  - `svm_trampoline.S`：pop 顺序改为 `rcx → rdx → rax`（对应 push 逆序），
    `movq %rcx,0xe0(%rax)` / `movq %rax,0xe8(%rcx)` 已用 objdump 确认；
  - 保留“进入 C 收尾前恢复线程栈”（`vcpu->host_rsp`），避免内核 API 跑在专用 host 栈。
- 构建：step12 v62 SHA256 `3975A99FDCA654136C826F9B9B938229721FC9864688CCEC065658164D8AB833`，
  归档 `D:\aaaaaavm\yuanguard_hv_v62_step12.sys`。
- 状态：根因已修，未再加载；下次加载仍有进一步风险（首次真实 OS 进 guest）。

### 9.46 OS-as-Guest Phase A：第三次崩溃（0x101）与 RIP 覆写修复（2026-08-11）

- v62 实机再试仍蓝屏：**0x101 CLOCK_WATCHDOG_TIMEOUT**，挂起处理器 index=1
  （测试核），bucket `CLOCK_WATCHDOG_TIMEOUT_INTERRUPTS_DISABLED`。
- 根因（代码审查 + dump 交叉确认）：`svm_prepare_vcpu()` 末尾执行
  `yg_svm_vmsave(vcpu->vmcb_pa)`，会把**当前 RIP/RSP 覆写进 VMCB state 区**；
  其它测试都在 prepare 后重新设置 `state.rip`，而 `yghv_os_guest_thread` 漏了，
  导致 guest 从驱动内错误地址开始执行、无 CPUID 退出、IF=0 → 看门狗 0x101。
- 修复（仅 `main.c`）：prepare 后显式
  `v->vmcb->state.rip = (uint64_t)yghv_os_guest_main;`。
- 构建：step12 v63 SHA256 `A611682000161A7778204C4976B7416C7253565BC445DC2DF9BE071530982592`，
  归档 `D:\aaaaaavm\yuanguard_hv_v63_step12.sys`。
- 三次崩溃均已有 dump/代码级根因：pop 顺序（0x7E）→ 同 bug 冻结 → RIP 覆写（0x101）；
  均未再加载验证。

### 9.47 OS-as-Guest Phase A：第四次崩溃（仍 0x101）与“无拦截指令”修复（2026-08-11）

- v63 实机再试仍蓝屏：**0x101 CLOCK_WATCHDOG_TIMEOUT**，处理器 1，与上次相同。
- 根因（objdump 实锤）：`yghv_os_guest_main` 的 `__cpuidex(cpu_info,...)` 结果未使用，
  **编译器整段删掉**，guest 循环只剩 `rdtsc + lock incl`，没有任何被拦截指令 →
  永不 VMEXIT → guest IF=0 → core1 收不到时钟中断 → 看门狗。`volatile int cpu_info[4]`
  仍被优化掉，无效。
- 修复（`main.c`）：
  - guest 循环改用内联 asm `cpuid` 并把 EAX 喂给共享计数（`InterlockedExchangeAdd`），
    反汇编确认 `cpuid` 已保留；
  - VMCB 拦截增加 `INTERCEPT_RDTSC` 作为兜底（vmexit 已有 RDTSC 分支推进 RIP）；
  - 停止路径的 `cpuid` 也改内联 asm。
- 构建：step12 v64 SHA256 `70D071622DF524FAC6603C8C9587BBB6D2195C593B74E68B5555277AE739072C`，
  归档 `D:\aaaaaavm\yuanguard_hv_v64_step12.sys`。
- 现状：四次崩溃均有 dump/反汇编级根因并已修；v64 未加载。

### 9.48 OS-as-Guest Phase A：第五次崩溃（0x139）与 vcpu 指针截断修复（2026-08-11）

- v64 实机再试蓝屏：**0x139 KERNEL_SECURITY_CHECK_FAILURE，Arg1=4**（线程栈指针不在
  合法栈范围），异常源 `yuanguard_hv+0x56b1` = `svm_dispatch_exit` 入口
  `mov (%rcx),%rax` 读取 vcpu 指针时 #PF。
- 根因（反汇编实锤）：trampoline 调用 `svm_dispatch_exit` 前用了
  **`mov ecx, edi`（32 位）**，把 64 位 vcpu 指针截断成低 32 位 → 无效地址读 →
  #PF；异常在专用 host 栈上分发 → Windows 安全检查 0x139。
- 修复（`svm_trampoline.S`）：`mov ecx, edi` → `mov rcx, rdi`，
  objdump 确认 `movq %rdi,%rcx`。
- 构建：step12 v65 SHA256 `6C5DCA647334463D0A63FFDEEB598C824F3EA882C640465A8D875D587E1505B0`，
  归档 `D:\aaaaaavm\yuanguard_hv_v65_step12.sys`。
- 现状：五次崩溃均有 dump/反汇编级根因并已修；v65 未加载。

### 9.49 OS-as-Guest Phase A：实机首次通过（2026-08-11）

- v66（修复计数为 `InterlockedIncrement`，cpuid 结果存 `g_os_guest_cpuid_acc`
  保活）在实机加载：**`sc start` RUNNING**，日志完整
  `bm os guest start → os guest thread enter → os guest host done →
  os guest counter=0x1388(5000) → bm os guest done → bm done`；
  `sc stop` 干净回 STOPPED，无蓝屏/冻结。
- 意义：**真实内核线程在 guest mode 中执行 Windows 内核代码（5000 次 CPUID/RDTSC +
  共享计数），有界跑完、host 收尾、干净卸载——OS-as-guest 无缝进入/VMEXIT 分发/
  host 栈切换核心路径在实机验证通过**。
- 构建：step12 v66 SHA256 `A9E6A8176BFEACC8B6611854A7FCB5305002371A3478B19B2D3538F844C13156`，
  归档 `D:\aaaaaavm\yuanguard_hv_v66_step12.sys`。
- 五次崩溃复盘（全部 dump/反汇编级根因）：pop 顺序（0x7E）→ 同 bug 冻结 →
  VMSAVE 覆写 RIP（0x101）→ cpuid 被优化删掉无拦截指令（0x101）→
  `mov ecx,edi` 截断 vcpu 指针（0x139）。
- 下一步 Phase B：全核 OS guest + OS profile 拦截矩阵；Phase C：真实 hook/NPF；
  Phase D：整机隐形与 R1。

### 9.50 Phase B step13：双核 OS-guest 有界试点实机通过（2026-08-11）

- 机制调整：guest 停止改由 host 按每核退出次数限停（`YGHV_OS_GUEST_EXIT_LIMIT=10000`），
  不再依赖 guest 全局 stop 旗标；`yghv_os_guest_thread` 参数化为 core，host_done
  改为带 `vcpu` 参数、按 `resident_index` 触发每核完成事件。
- step13（2 核，core1/2）：实机 `sc start` RUNNING，日志
  `os guest thread enter=1/2` → `os guest host done=1/2` →
  `os guest c1 exits=0x2710`、`c2 exits=0x2710`、`os guest counter=0x2710` →
  `bm os guest multi done`；`sc stop` 干净回 STOPPED，无蓝屏/冻结。
- 构建：step13 v67 SHA256 `A45850CB25B2677D05687617593856AAF82E9ADC6A59DDCE47EBC20FA0BFF971`，
  归档 `D:\aaaaaavm\yuanguard_hv_v67_step13.sys`。
- 下一步：step13b 全核（12 核）OS-guest 有界试点 → step14 无缝全核 OS 常驻（真正 OS-as-guest）。

### 9.51 Phase B step14：全核 12 核 OS-guest 有界试点实机通过（2026-08-11）

- step14（全部 12 核，core0-11）：实机 `sc start` RUNNING，日志 12 个
  `os guest thread enter` / `os guest host done`，每核 `os guest exits=0x2710`
  （10000 次退出），`os guest counter=0xea60`（5000 次迭代 × 12 核），
  `bm os guest all done`；`sc stop` 干净回 STOPPED，无蓝屏/冻结。
- 构建：step14 v68 SHA256 `B6C716D4D167598C075684AA98C62784BB4D68D7CC45077412862D806265972A`，
  归档 `D:\aaaaaavm\yuanguard_hv_v68_step14.sys`。
- 修一个构建坑：`RtlStringCchPrintfA` 在 /Zl 无 CRT 链接下引入
  `__stdio_common_vsprintf` 未解析符号，step14 日志改为顺序 `os guest exits`。
- 下一步：无缝全核 OS 常驻（真正把整个 Windows 放进 guest，最高风险项）需单独立项设计。

### 9.52 Phase B step15：单核无缝延续有界试点（2026-08-11，待实机加载）

- 新增 `svm_os_seamless_cont` 标签：guest 从该标签 `ret` 回 trampoline 调用者，
  调用者后续代码在 guest mode 中继续（无缝延续语义）。
- 新增 `yghv_os_guest_seamless_thread` + step15：core1 单核有界无缝版，5000 次
  CPUID/RDTSC，host 按 10000 次退出限停。
- 设计文档：`docs/superpowers/plans/2026-08-11-os-as-guest-step15.md`。
- 构建：step15 v69 SHA256 `B7F18153DFE727881C85E0AB28BB8D9BD9288C21B3725FF9F8C31D518E8C7CE4`。
- 状态：编译通过，未加载；step16 全核无缝常驻为最高风险项（卸载/devirtualize 未实现）。

### 9.53 Phase B step15：单核无缝延续实机通过（2026-08-11）

- v69 实机加载：`sc start` RUNNING，日志
  `bm os seamless start → os seamless enter=1 → os guest host done=1 →
  os guest counter=0x1388(5000) → bm os seamless done → bm done`；
  `sc stop` 干净回 STOPPED，无蓝屏/冻结。
- 意义：**“VMRUN 后调用者代码无缝继续”语义实机验证通过**（guest 从
  `svm_os_seamless_cont` ret 回调用者，调用者在 guest mode 中执行 5000 次
  CPUID/RDTSC）。
- 归档：`D:\aaaaaavm\yuanguard_hv_v69_step15.sys`（SHA256
  `B7F18153DFE727881C85E0AB28BB8D9BD9288C21B3725FF9F8C31D518E8C7CE4`）。
- 下一步：step16 全核无缝有界（扩展 step15 到 12 核）→ step17 全核无缝常驻
  （真正 OS-as-guest，最高风险，需先解决卸载/devirtualize）。

### 9.54 Phase B step16：全核 12 核无缝延续实机通过（2026-08-11）

- step16（全部 12 核，seamless 延续）：实机 `sc start` RUNNING，日志 12 个
  `os seamless enter` / `os guest host done`，每核 `os seamless exits=0x2710`，
  `os guest counter=0xea60`（5000×12），`bm os seamless all done`；
  `sc stop` 干净回 STOPPED，无蓝屏/冻结。
- 构建：step16 v70 SHA256 `F7EC6C5E606F4CF9B89502E09528D3785AE86B66D1AEEAEB51B5E63D3B6A4537`，
  归档 `D:\aaaaaavm\yuanguard_hv_v70_step16.sys`。
- 里程碑：**单核/双核/全核的 OS-guest 有界 + 无缝延续机制全部实机通过**。
- 下一步：step17 全核无缝常驻（真正 OS-as-guest），需先设计卸载/devirtualize，
  全程最高风险。

### 9.55 step17 设计文档（2026-08-12）

- 新增 `docs/superpowers/plans/2026-08-11-os-as-guest-step17.md`：
  - 每核真实上下文经 `svm_os_seamless_cont` 无缝进入，常驻线程 guest 内永久阻塞；
  - OS profile 拦截矩阵：NPF/VMMCALL/CPUID/SHUTDOWN 开，INTR/NMI/RDTSC/MSR/CR 关；
  - 未知 VMEXIT 不再停机，日志后继续；
  - **卸载/devirtualize 未实现**：停止 VMRUN 会丢弃 guest 内线程上下文，
    step17 验证只能“加载→观察→重启清除”；`sc stop` 会挂起，不接受；
  - 测试顺序：17a 单核常驻（IF=1，30-60 秒观察后重启）→ 17b 全核常驻。
- 状态：设计已定，待用户确认后实现 step17a。

### 9.56 step17a：单核无缝常驻实现（2026-08-12，待实机加载）

- `svm_trampoline_os_enter` 增加 `if1` 参数：if1=1 时保留 guest RFLAGS.IF
  （OS 自己收中断）；既有 step12-16 传 0（IF=0 有界测试）。
- 新增 `yghv_os_guest_resident_thread`：core1 常驻线程以 `svm_os_seamless_cont`
  无缝进入 guest（IF=1、无退出上限），guest 内永久阻塞让调度器运行其它 Windows
  线程；VMCB 只开 CPUID/SHUTDOWN/VMMCALL 拦截。
- `vmexit.c`：`g_os_resident_mode=TRUE` 时未知 VMEXIT 日志后继续（不再停机）。
- step17：创建常驻线程后 DriverEntry 返回 RUNNING；**不可卸载**，验证后重启清除。
- 构建：step17 v71 SHA256 `CAC5B3ADC5F8ABBCBC200C27FB591753D3318734B1838BA3019D1601204FDD7D`。

### 9.57 step17a 冻结修复：GIF 隔离 + 存活日志（2026-08-12）

- 首次 step17a 实机硬冻结（无 dump，00:18 重启），无法定位现场。
- 修复一（`svm_trampoline.S`）：host 分发期间**保持 GIF=0**（去掉分发前 `stgi`），
  时钟中断不会落在专用 host 栈上；stop 路径才 `stgi` 再进 C 收尾。
- 修复二（`main.c`）：新增 `yghv_resident_alive_thread`（core0 每 5 秒写并 flush
  `resident alive Ns`），冻结后可从 `C:\Windows\yghv_progress.log` 看存活时长。
- 构建：step17 v72 SHA256 `C67C50C5BC2DF7E61D6E79C2BCD95E1F1FF7413758D23356D5A1B4F78707110A`，
  归档 `D:\aaaaaavm\yuanguard_hv_v72_step17.sys`。
- 待验证：实机或 VM（建议 VM，崩了只崩 VM）。

### 9.58 step18 自旋常驻实机冻结：变量隔离证据（2026-08-12）

- step17a（阻塞常驻，IF=1）v71/v72/v73 实机均 <5 秒整机硬冻结，无 dump；
  看门狗也无法落盘（系统级挂死）。
- step18（自旋常驻，IF=1，guest 为无限 CPUID/RDTSC 循环，不阻塞不切线程）
  v74 实机加载（C 盘哈希 `7B9D63D6...` 已核对）：进度日志完整写到
  `bm os resident spin running → bm done`，但 **没有 `resident alive=5`** →
  常驻开始后 5 秒内整机冻结；无 minidump（自旋 guest 持续退出，心跳不断，
  看门狗不会触发）。
- 结论（变量分离）：**排除了“guest 内调度器/上下文切换”**；冻结主因收敛为
  **IF=1 下物理中断在 guest 模式投递 / guest 模式 APIC 交互**，与 v52 之前
  合成 resident 冻结同类，且与 AMD 56683 errata 1363（持续 APIC 访问流可致
  系统挂起/复位）高度吻合。
- 下一步实验 step19：step18 基础上**加回 INTR/NMI 拦截**（ISR 在宿主态执行，
  v52 思路），验证“guest 中断投递是冻结根因”；若 step19 存活，再做宿主收中断
  + 事件注入回 guest 的正规方案。若仍冻结，判定平台级限制，转 VM/换机。
- 环境备注：`C:\Windows\yghv_progress.log` 现为同步落盘（每次 trace 即 flush），
  硬冻结后可保留最后里程碑；VM 已停止；live KD 无法连接（guest 未开实时调试）。
- 归档：`D:\aaaaaavm\yuanguard_hv_v74_step18.sys`（SHA256
  `7B9D63D6AD9CAE708ADDE8004167ABD5889BEDB3F041DC4524CF3304B97DE320`）。
- 提交：`a037044`（step18 + trace 落盘 + alive/看门狗）。

### 9.84 2026-08-12 交接文档两次损坏恢复 + v93/v94 结果 + v95 决策

- 14:28 / 14:38 两次发现 `docs/YUANMOD_HANDOFF_CURRENT.md` 被硬冻结写成全 NUL
  （分别 157,210 / 125,661 字节，无任何非零字节）；已从 HEAD `06458a6`
  （9.58 版，blob `fc1935c1`）恢复；9.59-9.83 未提交记录不可恢复，按会话摘要
  重建核心结论，并同步备份到 `D:\aaaaaavm\yghv_handoff_backup_*.md`。
- 9.59-9.83 重建要点：
  - 唯一 PASS：step20 自旋常驻 + INTR/NMI 拦截 + 宿主 ISR（无 AVIC）。
  - 阻塞变体 step22/23/24 硬冻结；AVIC 分支 step25/27/30/31 硬冻结、
    step32 v90/v91 `0x101` 蓝屏、step33 v92 硬冻结。
- v93（MSR_PROT + `VM_CR` 读返回 `VM_CR_SVMDIS`、写丢弃）step33 实机硬冻结：
  SHA256 `404D6DF7...270D2AD`，14:32 重启，无 `resident alive`、无 dump。
- v94（每次 VMRUN 前清 `[vcpu->vmcb + 0xC0]`）step33 实机硬冻结：SHA256
  `E40C00E9...110D194A`，14:36 重启，无 `resident alive`、无 dump。
- 结论：VM_CR 虚拟化与 VMCB clean bits 均未解冻；AVIC 分支连续失败。
- v95 决策：用当前代码（含 v93/v94 改动）重测 **step23**（阻塞常驻 + host ISR，
  无 AVIC），排除或确认“guest 上下文切换”为冻结根因。
- v95 构建：SHA256 `9662EF540EA17437D82BEC079F83F94AB20F2C29294CAF7B881B1C531990C6F7`，
  归档 `D:\aaaaaavm\yuanguard_hv_v95_step23.sys`；已复制 `C:\yuanguard_hv.sys`。
- v95 实机结果：step23（阻塞 + host ISR，无 AVIC）仍硬冻结，C 盘哈希已核对
  `9662EF54...`，14:43:38 重启，日志到
  `bm os resident block delay running → bm done`，无 `resident alive`、无 dump。
  文档与 v95b 备份再次被硬冻结写成全 NUL，已从 git blob `af5282e5` 恢复。
- 结论更新：v93（VM_CR）/ v94（clean bits）/ v95（无 AVIC 阻塞）全部未解冻；
  唯一 PASS 仍是 step20（自旋 + INTR/NMI 拦截 + 宿主 ISR）。冻结触发点收敛到
  “guest 模式内 Windows 调度/上下文切换”或“持续物理 APIC 访问流（errata 1363）”。
- v96 提案（待用户确认）：在 step20 PASS 基线上，让 guest 自旋循环额外持续写
  物理 APIC TPR（0xFEE00080）。若 v96 冻结 → APIC/1363 根因，转 xAPIC MMIO
  虚拟化（NPT 剔除 APIC 页 + 宿主模拟）；若 v96 存活 → 根因在上下文切换本身，
  转研究 guest 上下文切换的状态保存。
- v96 已确认实现：新增 step96 入口（`bm step=0x60`），复用 step20 编排，guest
  自旋循环每轮写物理 APIC TPR（`MmMapIoSpace` 0xFEE00000 + 0x80，写 0）。
  构建 SHA256 `193CD493530E186CE72240456642737A1C98094B28666F23070927CBFA8BD9C4`，
  归档 `D:\aaaaaavm\yuanguard_hv_v96_step96.sys`，已复制 `C:\yuanguard_hv.sys`。
- v96 实机结果：**PASS**，`resident alive` 持续到 45 秒（`0x2d`），服务 RUNNING、
  机器全程响应。结论：**持续的物理 APIC TPR 写流单独不会冻结** step20 基线；
  冻结触发点进一步收敛到 guest 模式内的 Windows 阻塞/空闲路径（调度器切换或
  guest 内执行 HLT/MWAIT）。
- v97 实现：新增 step97 入口（`bm step=0x17`），复用 step23（阻塞 + host ISR），
  并拦截 guest `HLT/MWAIT/MWAIT_COND`（HLT=24 general1，MWAIT=43/44 general2），
  让空闲指令走 VMEXIT + RIP 推进模拟，而不是在 guest 模式内真正 halt。
- v96 延长观察：`resident alive` 到 140 秒（`0x8c`）仍 RUNNING、机器响应，暂未
  手工卸载（OS-as-guest 常驻不可安全卸载，需重启清除）。
- v97 构建：SHA256 `8D31D61E0F98AAF4EFE3262AE4699BF8EB87896F469AD414E7F775CA70F3FD73`，
  归档 `D:\aaaaaavm\yuanguard_hv_v97_step97.sys`；待重启后复制 `C:\yuanguard_hv.sys` 测试。
- v97 实机结果：**FAIL**，C 盘哈希已核对 `8D31D61E...`，15:04:46 重启；日志停在
  `bm os resident block delay start → os resident delay enter=1`，未到
  `block delay running`，无 `resident alive`、无 dump。HLT/MWAIT 拦截未解冻，
  且本次冻结点比 v95 更早。
- 阶段结论：单核 OS-as-guest 常驻 + guest 内 Windows 调度器切换，在
  AVIC / VM_CR / clean bits / TPR 写流 / HLT/MWAIT 拦截全部变量下均冻结；
  剩余路径（xAPIC MMIO 拦截+宿主模拟、全核常驻、换平台）成本高且无收敛保证。
  非驻留保护路线保持稳定，未受影响。
- v98 实现（用户确认走 xAPIC MMIO 虚拟化）：新增 `npt_map_page`（GPA→任意 SPA
  4KB 重映射）；step98 复用 step23，把 guest `0xFEE00000` 重映射到影子 APIC 页，
  每次 VMEXIT 扫描影子页，仅 TPR/ICR/LVTT/TMICT/TDCR 变化时转发真实 APIC
  （避免持续写流），EOI 留在影子页；NPT 变化后刷 TLB。
  实现修正：原 TPR 门控方案会因 pending 中断在 VMRUN 后立刻重退出而死锁，
  改为转发 TPR 到真实 APIC，让硬件按真实 TPR 决定 INTR 拦截。
  构建 SHA256 `69E31E6DD874410A5A628D0BA8180410FDE85DAFA841DBA28B44F8524B7FA5E0`，
  归档 `D:\aaaaaavm\yuanguard_hv_v98_step98.sys`，已复制 `C:\yuanguard_hv.sys`。
- v98 实机结果：**FAIL**，C 盘哈希已核对 `69E31E6D...`，15:14:54 重启；日志完整到
  `v98 apic shadow armed=0xc7a2f000 → os resident delay enter=1 →
  bm os resident block delay running → bm done`，无 `resident alive`、无 dump。
  结论：guest APIC 访问重定向到影子页后，阻塞常驻仍冻结；xAPIC MMIO 虚拟化
  单变量未能解冻，冻结触发点进一步收敛到 guest 模式内上下文切换本身。
- v99 全核常驻（用户确认）：计划见
  `docs/superpowers/plans/2026-08-12-os-as-guest-allcore-resident.md`。
  已实现 step99：12 核 barrier 后同时 seamless 进入 guest，延续体永久阻塞，
  沿用 INTR/NMI 拦截 + 宿主 ISR + MSR_PROT + clean bits，无 AVIC/影子 APIC。
  构建 SHA256 `DC1EAA13FD95E99B0D06D3CA4453A702A01A414617FE62F737DDD30A5CBC4F06`，
  归档 `D:\aaaaaavm\yuanguard_hv_v99_step99.sys`，已复制 `C:\yuanguard_hv.sys`。
- v99 实机结果：**FAIL**，C 盘哈希已核对 `DC1EAA13...`，15:34:05 重启；日志
  `bm os allcore resident start` 后 **12 核全部出现 `allcore enter=0..11`**，
  随后冻结，未见 `bm os allcore resident running`，无 `resident alive`、无 dump。
- 最终结论：全核对称常驻同样硬冻结，**排除“单核不对称”假设**；在 Ryzen 5 5500
  上以当前透明 OS-as-guest 方式常驻不可行，判定为平台级限制，停线收尾。
  非驻留保护路线保持稳定可用。

### 9.85 2026-08-12 云服务器 KD 调试配置（WinDbg over network）

- 用户提供云服务器（公网 `192.140.176.241`）作为 KD 主机；实际系统为
  Windows Server 2022 Datacenter x64，SSH 22 关闭，RDP 3389 / WinRM 5985 开放。
- 本地远程通道：PowerShell `New-PSSession` 缺 WSMan 客户端、`winrs.exe` 不可用；
  改用本机 `pywinrm`（NTLM over HTTP 5985）成功。
- 已安装 Debugging Tools for Windows 10.0.26100.1 到
  `C:\Program Files (x86)\Windows Kits\10\Debuggers\x64`
  （`kd.exe` / `cdb.exe` / `windbg.exe` 均存在）。
- 已放行云服务器 Windows 防火墙 UDP 50000 入站（规则 `yghv_kd`）。
- 已后台启动 KD 网络监听：
  `kd.exe -y srv*C:\kd\symbols*https://msdl.microsoft.com/download/symbols -k net:port=50000,key=1.2.3.4 -logo C:\kd\kd.log`
  状态 `Waiting to reconnect...`，进程 PID 4576，UDP 50000 已监听。
- 待用户操作：云厂商安全组放行 UDP 50000 入站。
- 实机侧准备命令（执行前需备份 BCD）：
  `bcdedit /dbgsettings net hostip:192.140.176.241 port:50000 key:1.2.3.4`
  `bcdedit /debug on`
- 云服务器凭据不写入项目文档。

### 9.86 2026-08-12 云服务器信息落档（凭据存仓库外）

- 云服务器公网 `192.140.176.241`，Windows Server 2022 Datacenter x64，
  Administrator 账号；密码与远程操作模板只保存在
  `D:\aaaaaavm\yghv_cloud_kd_server.md`（不写入仓库）。
- 管理通道：WinRM HTTP 5985（本机用 pywinrm NTLM）；SSH 22 关闭。
- KD 监听：UDP 50000，`kd.exe -g` 自动继续，日志 `C:\kd\kd.log`；
  Windows 防火墙与云安全组均已放行，UDP echo 端到端测试 PASS。
- 本机 BCD：已备份 `C:\yghv_bcd_backup_20260812.bcd`，已设置
  `net hostip:192.140.176.241 port:50000 key:1.2.3.4`，`debug on`。
- 下一步：实机重启验证云 KD 连接。

### 9.87 2026-08-12 云 KD 连接失败：Realtek 网卡不被 KDNET 支持

- 实机重启后云端 `kd.log` 仍 `Waiting to reconnect...`，KD 未连上。
- 本机 BCD 正确（NET hostip 192.140.176.241 port 50000 key 1.2.3.4，debug Yes）；
  网卡为 Realtek RTL8168（PCI 8.0.0，VEN_10EC DEV_8168），驱动 2015/4/10
  9.1.410.2015。
- 新旧 kdnet（2020 / 10.0.26100）均报“Network debugging is not supported on
  any of the NICs”，并 `Failed to parse the busparams:PCI 8 0 0`；
  显式 `kdnet /busparams 8.0.0 192.140.176.241 50000` 报
  “The specified debug Device was not found”。
- 驱动二进制 `rt640x64.sys` 无 KDNET 字符串；驱动存储内也无更新版本。
- 云端 KD 端保持就绪（UDP 50000，`-g`）；UDP 端到端 echo 测试 PASS。
- 待用户决策：a) 更新最新 Realtek 公版驱动后再试；b) 更换受支持网卡；
  c) 放弃云端 KD，沿用现有进度日志/看门狗方案。
- 注意：本机以太网为静态 IP，LAN 若无 DHCP，KDNET 会退回 APIPA，无法经
  公网到达云服务器；即使网卡支持也需 LAN 有 DHCP。

### 9.88 2026-08-12 Realtek 驱动更新后 KDNET 仍不支持（结论）

- 用户从 Station-Drivers 安装最新驱动：`10.80.50.407`（2026/4/7），
  `rt640x64.sys` 已更新为 1,473,064 字节（2026/7/20）。
- 新版 kdnet 10.0.26100 仍报 `Network debugging is not supported on any of
  the NICs`；显式 `kdnet /busparams 8.0.0 192.140.176.241 50000` 仍报
  `The specified debug Device was not found`。
- 最终结论：板载 Realtek RTL8168（REV_15，SUBSYS 8677）为硬件级
  KDNET 不支持，驱动更新无法解决；云端 KD 方案需换受支持网卡或改走
  其他调试/日志方案。

### 9.89 2026-08-12 KDNET 扩展模块试验：云端 Server 2022 DLL 仍不识别

- 新发现：WDK 自带 Realtek KDNET 扩展模块完整源码
  `C:\Program Files (x86)\Windows Kits\10\Debuggers\ddk\samples\kdnet\ethernet\realtek\`
  （产出 `kd_02_10ec.dll`）；本机 Win10 19045 的 `System32` 原本完全没有
  `kd_02_*.dll`，怀疑是系统组件缺失。
- 云端 Windows Server 2022 的 `C:\Windows\System32\kd_02_10ec.dll` 存在，
  430,416 字节，微软签名有效，SHA256
  `4D6F0B041B1678AC2993D4DCCCE4A3049A88BFCBC798212E1EEA50087438C208`；
  反汇编显示比 WDK 示例多支持 `2C000000/2C800000/48/4C/54/60/64` 等
  新版 Realtek 内部版本，曾推测可覆盖 REV_15。
- 用户确认试验“复制 DLL + 跑 kdnet，不重启、不改 BCD”。已把云端 DLL
  复制到本机 `C:\Windows\System32\kd_02_10ec.dll`，并归档到
  `D:\aaaaaavm\kd_02_10ec_srv2022.dll`。
- 结果：`kdnet.exe` 输出与之前完全相同，仍报
  `Network debugging is not supported on any of the NICs`，8.0.0 仍
  `Failed to parse the busparams`。仅加 DLL 不改变 kdnet 检测结果。
- 已按约定回滚：活动文件改名为
  `C:\Windows\System32\kd_02_10ec.dll.bak`，未改 BCD、未重启。
- 待用户确认的下一步：用 `bcdedit /copy {current}` 建独立测试启动项，
  直接强制 `busparams=8.0.0` 后重启验证云端 KD 是否连接；或继续编译
  并补改 WDK Realtek 扩展模块源码。

### 9.90 2026-08-12 独立 KDNET 测试启动项（用户确认，已准备重启）

- 用户确认继续强制 `busparams` 测试。已把云端 Server 2022 的
  `kd_02_10ec.dll` 重新复制为活动文件
  `C:\Windows\System32\kd_02_10ec.dll`（SHA256
  `4D6F0B041B1678AC2993D4DCCCE4A3049A88BFCBC798212E1EEA50087438C208`）。
- 已导出测试前 BCD 备份：`C:\yghv_bcd_backup_20260812_kdnet_test.bcd`。
- 已用 `bcdedit /copy {current}` 创建独立测试项：
  `{72407a44-68b4-11f1-9ddb-bc907795b50b}`，描述
  `YuanGuardHV KDNET Test`；默认启动项保持 `{current}`。
- 测试项设置：`debug on`、`kerneldebugtype NET`、
  `kernelbusparams 8.0.0`、`kernelhostip 192.140.176.241`、
  `kernelport 50000`、`kernelkey 1.2.3.4`、`kerneldhcp Yes`、
  `bootstatuspolicy IgnoreAllFailures`、`recoveryenabled No`。
- 首次设置 `kernelhostip` 十进制算错（曾变成 192.140.184.193），已修正为
  3230445809，`bcdedit /enum` 复核显示 192.140.176.241 正确。
- 准备用 `bcdedit /bootsequence {guid}` 做一次性启动到测试项；云端
  `kd.exe` PID 1912 监听 UDP 50000，`kd.log` 为 `Waiting to reconnect...`。
- 重启后核对云端 `C:\kd\kd.log` 是否出现连接/断点；若启动失败，正常项
  `{current}` 仍是默认，可进 WinRE 改名 `kd_02_10ec.dll` 并恢复 BCD。

### 9.91 2026-08-12 独立测试项重启结果 + 本地编译 Win10 版模块

- 第一次测试项重启结果：成功进入 `YuanGuardHV KDNET Test`（描述已确认），
  云端 `kd.log` 仍 `Waiting to reconnect...`，未收到任何 KD 连接。
- 本地 `kd -kl` 能连接但仅显示 `nt` 模块；`KdDebuggerEnabled=0`、
  `KdDebuggerNotPresent=1`，说明 KDNET 传输实际未初始化成功。
- 归因：Server 2022 版 `kd_02_10ec.dll`（10.0.20348）与本机
  `kdcom.dll`（10.0.19041）未成功接上，或该版本模块不加载。
- 已从 WDK 示例源码手工编译 Win10 19041 版 `kd_02_10ec.dll`：
  clang-cl + MSVC link，修正 `KdInitializeController` 签名为
  `PKDNET_SHARED_DATA`，补本地 `memset`/`__security_init_cookie` stub，
  产物 35,840 字节、无导入、只导出 `KdInitializeLibrary`；
  SHA256 `EE373DC2B24F4A09320B5E97DFF8A76F5B01799BB02C9AD954C780293F882320`，
  归档 `D:\aaaaaavm\kd_02_10ec_win10_19041_local.dll`。
- 用户确认后已把本地编译版复制为
  `C:\Windows\System32\kd_02_10ec.dll`；原 Server 2022 版改名为
  `kd_02_10ec_srv2022.dll.bak` 保留。
- 准备再次用 `bcdedit /bootsequence {guid}` 一次性启动测试项并重启，
  继续核对云端 `C:\kd\kd.log` 与本机 `KdDebuggerEnabled`。

### 9.92 2026-08-12 导入数量修正：31 导入 + 13 导出

- 本地 30 导入版（WDK 示例头）重启后 `KdDebuggerEnabled=0`、
  `KdDebuggerNotPresent=1`，云端仍无来自本机的包；`kd.log` 中的
  `Bad packet sent from 198.235.24.228` 经本机主动 UDP 测试判定为公网
  扫描（本机实测源 IP 为 117.172.241.116），不是 KDNET 握手。
- 反汇编本机 `C:\Windows\System32\kdstub.dll` 发现它校验
  **ImportTable->FunctionCount == 0x1F（31）**、导出表
  `FunctionCount == 0x0D（13）`；WDK 示例头为 30 导入，导致
  `KdInitializeLibrary` 直接返回 `STATUS_INVALID_PARAMETER`，模块未接入。
- 已复制示例头到临时构建目录，把 `KDNET_EXT_IMPORTS` 改为 31 后重新
  编译/签名/归档：`D:\aaaaaavm\kd_02_10ec_win10_19041_imp31.dll`，
  37,248 字节，SHA256
  `3260BC3604C59BA238A946246106FEE1FE497FF183CC491724C4DBB26CD3C945`；
  反汇编确认 `cmp [rdi],1Fh` 与 `cmp [rcx],0Dh`。
- 用户确认后已把 imp31 复制为 `C:\Windows\System32\kd_02_10ec.dll`；
  上一版 30 导入本地模块改名为 `kd_02_10ec_imp30.dll.bak`。
- 准备再次一次性启动测试项并重启，核对云端 `kd.log` 与
  `KdDebuggerEnabled`。

### 9.93 2026-08-12 常驻冻结线恢复 + step100 冻结现场环形缓冲

- 用户明确放弃云 KD 路线，回到本机解决 OS-as-guest 常驻冻结（先前 9.84
  记录到 v99 全核 FAIL 后停线，现恢复推进）。
- 现状核对：`C:\yuanguard_hv.sys` = v99 step99 冻结版（SHA256
  `DC1EAA13...`），服务 Stopped；加载即会重演整机硬冻结。
- 复盘实验矩阵：唯一 PASS 仍是 step20 自旋 + INTR/NMI 拦截 + 宿主 ISR
  （v96 延长 140 秒 PASS）；阻塞变体 step22/23/24 与 AVIC/VM_CR/clean
  bits/TPR/HLT-MWAIT/xAPIC 影子/全核 step99 全部 FAIL，冻结特征为
  alive 日志都停、无 dump。
- step100 决策（用户确认）：保持 step23 阻塞编排不变，在 VMEXIT 热路径
  加每 vcpu 64 项环形缓冲（exitcode/exitinfo1/2/RIP/CR3/RSP/RFLAGS/CPL +
  seq），由 core0 专用监控线程每 250ms 看 vcpu1 的 exits/seq；连续 10 次
  不推进即把 VMCB 现状 + 最近 64 条退出现场同步落盘（`v100 freeze site`、
  `v100 <seq> exit=...`），首次拿到冻结前 guest 现场。
- 实现：`svm_vcpu.h` 增 `v100_seq` + `v100_ring[64]`；`vmexit.c`
  `svm_dispatch_exit` 顶部 `yghv_v100_record`（仅 `g_v100_monitor_active`
  时记录）；`main.c` 增 `yghv_v100_dump`、`yghv_v100_monitor_thread`、
  step100 入口（复用 `yghv_os_guest_resident_delay_thread` + alive +
  monitor，**不挂看门狗**，避免 3 秒 bugcheck 抢在落盘前；监控线程等
  `g_v100_guest_entered` 后才开始计时，防初始误判；`seq==0` 不落盘）。
  默认版与 step100 版均构建通过。
- 构建：step100 SHA256 `E300CF39EC9E705B0041F70FA39D4EADD6009E5DF99FBD5F9CF867DB1836C55D`，
  归档 `D:\aaaaaavm\yuanguard_hv_v100_step100.sys`（待复制 `C:\yuanguard_hv.sys`）。
- 下一步：复制到 C 盘后实机加载 `sc start yuanguard`；观察
  `C:\Windows\yghv_progress.log`，冻结后重启读取 `v100` 现场。

### 9.94 2026-08-12 step100 构建事故根因与修正（重要）

- 症状：step100 版构建后 `C:\yuanguard_hv.sys` 不包含任何 step 字符串
  （`bm step`/`freeze-site` 等全无），驱动加载走的是默认路径，无法测试。
- 根因：`yghv_baremetal_step_test` 开头 `if (step > 99) return
  STATUS_NOT_IMPLEMENTED;`。step100 是编译期常量，clang `/O2` 把该分支
  常量折叠为恒真，整个函数体（含全部 step 字符串与逻辑）被判不可达并
  裁成只剩 `svm_core_get_vcpu(0)` 的空壳；DriverEntry 的 `bm done` 仍保留，
  造成“调用点在、函数体无”的假象。v99 及以前能构建是因为 99 未超上限。
- 修正：上限改为 `step > 100`；新发现链接期依赖 `KeQueryPerformanceCounter`
  （HAL，当前仅链 ntoskrnl）与 `RtlStringCchPrintfA`（编译器引 UCRT），
  已改为 `__rdtsc()` + `yghv_trace_u64` 逐行落盘，去除外部符号依赖。
- 构建验证：/O2 下 step100 版 64,384 字节，`freeze-site`/`bm step`/
  `resident alive`/`os resident delay enter`/`v100 seq` 字符串全部存在；
  默认版（`YGHV_BAREMETAL_STEP=0`）恢复可构建。
- 最终产物：step100 SHA256
  `322C7286082A1A6BDDF80093472D2B684D4971CA2E4466C958D9BF5DDEA42D17`，
  归档 `D:\aaaaaavm\yuanguard_hv_v100_step100.sys`；默认版 SHA256
  `C8D5AD6863173555245F000642E753D5116DDE520132474C3E7CFC090A30C4C8`。
- 待办：把 step100 归档复制回 `C:\yuanguard_hv.sys`（当前 C 盘是默认版），
  再 `sc start yuanguard` 实机测试。

### 9.95 2026-08-12 step100 首次实机：复现冻结 + 日志增强

- 首次实机加载 step100（SHA256 `322C7286...`）：日志完整出现
  `bm step=0x64 → bm os resident freeze-site start → os resident delay
  enter=1 → bm os resident freeze-site running → bm done`，随后整机冻结
  （无 `resident alive`、无 `v100 freeze site`，18:18:30 重启）。
- 判定：冻结发生在 guest 进入后极短窗口，core0 的监控线程（250ms 轮询 +
  10 次 stall）来不及落盘；连 guest 延续体的 `os resident delay guest
  block` 都没打到，说明冻结点在 trampoline 返回 guest 到首次
  KeDelayExecutionThread 之间。
- 日志增强（本版 E66A2AAD，已复制 C 盘）：
  - VMEXIT 前 200 次退出逐条同步落盘（`v100 exit/rip/cr3/rsp/rf/cpl`），
    冻结瞬间若已有 VMEXIT 即有现场；
  - 监控线程每 250ms 落盘 `v100 pulse` 心跳，stall 阈值 10→4 次。
- 待实机复测：重启加载后读 `C:\Windows\yghv_progress.log` 找
  `v100 exit` 序列，定位冻结前最后一次退出类型与 RIP/CR3/RSP。

### 9.96 2026-08-12 step100 实机蓝屏 0x139：首次拿到崩溃 dump（关键证据）

- 实机加载增强版 step100（SHA256 `E66A2AAD...`）后蓝屏：
  **0x139 KERNEL_SECURITY_CHECK_FAILURE，Arg1=4
  （FAST_FAIL_INCORRECT_STACK）**，18:36:23，minidump
  `C:\Windows\Minidump\081226-19625-01.dmp`，已归档
  `D:\aaaaaavm\yghv_bsod_0x139_step100_20260812_1836.dmp`。
- 崩溃线程：`yuanguard_hv` delay 常驻线程（System 进程，Win32 Start
  Address 指向驱动），正在 `NtWriteFile`（guest 延续体里 `yghv_trace`
  写进度日志）→ 文件锁等待 → `KeWaitForSingleObject` →
  `KiSwapThread` → **`KiAbProcessContextSwitch` 触发 GP 异常** →
  异常分发时线程栈指针 `ffffc685994f6810` 越过栈底
  `ffffc68599521000` → 0x139。
- 关键结论：崩溃发生在 **guest 模式内部、Windows 调度器上下文切换
  路径**，且整个过程没有产生任何 VMEXIT（前 200 次 v100 落盘一条都
  没有），所以 ring buffer 无法捕获；之前的“硬冻结”与本次蓝屏应为
  同一机制（guest 内上下文切换 GP + 栈损坏），只是此前无 dump。
- 推断：常驻 OS-as-guest 要成立，必须让 Windows 在 guest 模式下正常
  完成上下文切换；当前 SVM 状态在此路径上会 GP。下一步待用户确认的
  隔离实验：guest 延续体不再调用 `yghv_trace`（文件写是本次阻塞/切换
  直接触发点），只留纯 `KeDelayExecutionThread` 阻塞，验证是否任何
  guest 内调度都会崩，还是仅阻塞文件 I/O 触发。

### 9.97 2026-08-12 隔离实验两轮蓝屏 + v100c 修复

- v100b（guest 延续体 quiet，无文件写；哈希 `C41D8821...`）实机仍蓝屏
  **0x139/Arg1=4**（18:46:56，minidump `081226-12390-01.dmp`），崩溃栈
  与 v100 完全一致：`yuanguard_hv+0x132d`（`yghv_trace` 的 `ZwWriteFile`）
  → `NtWriteFile` → 文件锁等待 → `KiSwapThread` →
  `KiAbProcessContextSwitch` GP → 栈越界。
- 关键：guest 已 quiet 但仍在写文件，来源是 **v100 逐条落盘在
  `svm_dispatch_exit` VMEXIT 路径内同步 `yghv_trace`**，与 alive/monitor
  线程抢同一日志文件锁；写阻塞后 Windows 在 hypervisor 分发中途上下文
  切换，栈损坏。结论：VMRUN 循环内任何阻塞 I/O + 上下文切换都会崩。
- v100c 修复：删除 `yghv_v100_record` 中前 200 次逐条同步落盘，仅保留
  内存 ring；guest 延续体保持 quiet；磁盘写入只剩 core0 monitor/alive
  线程。SHA256 `E4BA6C78F7E0127FD89446C7A9BFD8DA7F50D0556213B1E56519F05FD9D092C9`，
  归档 `D:\aaaaaavm\yuanguard_hv_v100c_step100_nofileio.sys`，已复制
  `C:\yuanguard_hv.sys`。
- 待测：`sc start yuanguard` 验证无任何阻塞 I/O 时纯 guest 阻塞是否仍崩。

### 9.98 2026-08-12 v100c 实机：纯 guest 阻塞仍硬冻结（隔离结论）

- v100c（无 guest 文件写、无 VMEXIT 路径文件写；SHA256 `E4BA6C78...`）
  实机：日志推进到 `bm os resident freeze-site running → bm done` 后
  整机硬冻结，18:53:34 重启；无新 minidump、无 `resident alive`、
  monitor 的 `v100 pulse` 也一条未写（250ms 内即冻结）。
- 三轮变量分离结论：
  - v100（guest 内 `yghv_trace` 文件写）：0x139 BSOD，栈在
    KiAbProcessContextSwitch GP；
  - v100b（guest quiet，但 VMEXIT 路径 v100 逐条同步写盘）：仍 0x139
    BSOD，同栈；
  - v100c（guest quiet + VMEXIT 路径零文件写）：硬冻结、无 dump。
- 结论：冻结/崩溃的根因是 **guest 模式下 Windows 调度器上下文切换本身**
  （`KiSwapThread`/`KiAbProcessContextSwitch`），与文件 I/O 无关；移除
  I/O 后从可诊断的 0x139 变为不可诊断的整机硬冻结。与 9.84 阶段结论
  （平台级限制）一致，当前代码/平台下透明 OS-as-guest 常驻不可行。

### 9.99 2026-08-12 v101 #GP/#DF 拦截实验：未捕获，仍硬冻结

- v101 实现：delay 线程 VMCB 打开 `exception_intercepts` 的
  #DF(8)/#NP(11)/#SS(12)/#GP(13)，`svm_dispatch_exit` 新增 0x48/0x4B/
  0x4C/0x4D 分支捕获现场并停止 guest，`yghv_os_guest_host_done` 落盘
  `v101 gp captured`。SHA256 `BE30E65FE2A0C51853C214EB121366C4B83738689D9BD2009497C899F5F159E2`，
  归档 `D:\aaaaaavm\yuanguard_hv_v101_step101_gpcap.sys`。
- 实机：日志到 `bm step=0x65 → bm os resident freeze-site start →
  os resident delay enter=1 → bm os resident freeze-site running →
  bm done` 后整机无响应（19:07:56 重启）；**无 `v101 gp captured`、
  无 alive、无新 minidump**。
- 结论：冻结不经过可拦截的 #DF/#NP/#SS/#GP 异常向量，且连 core0 的
  monitor/alive 线程都无法执行，属整机级硬停（疑似平台/SVM 行为，
  非普通 guest 异常）。常驻 OS-as-guest 继续硬啃成本极高、无收敛证据，
  建议停线并恢复稳定默认版。

### 9.100 2026-08-13 v102 全异常 + HLT 拦截（决定性诊断）

- v102 实现：delay 线程 VMCB `exception_intercepts = 0xFFFFFFFF`，
  `general1_intercepts |= HLT`；`svm_dispatch_exit` 顶部对任何
  `0x40-0x5F` 异常退出与 `0x78` HLT 捕获完整现场并停止 guest；
  `yghv_os_guest_host_done` 落盘 `v102 fault captured`。SHA256
  `D693577F8EBF02364FAC678B2D7C9F21D0294B4EA5282A8344612078749BE4F2`，
  归档 `D:\aaaaaavm\yuanguard_hv_v102_step102_catchall.sys`，已复制
  `C:\yuanguard_hv.sys`。
- 判据：若出现 `v102 fault captured` → 找到可拦截异常向量（含 #MC/HLT）；
  若仍整机硬停且无任何捕获 → 确认为平台级停机，常驻线在 Ryzen 5 5500
  停线，转非驻留保护路线或换平台验证。

### 9.101 2026-08-13 v102 实机阴性：平台级停机确认，常驻线停线

- v102（全 32 异常向量 + HLT 拦截；SHA256 `D693577F...`）实机：日志
  `bm step=0x66 → bm os resident freeze-site start → os resident delay
  enter=1 → bm os resident freeze-site running → bm done` 后整机硬停；
  无 `v102 fault captured`、无 alive、无 v100 pulse、无新 minidump
  （14:30:59 重启）。
- 决定性结论：guest 模式 Windows 调度器上下文切换导致的整机停止
  **不经过任何可拦截的异常向量，也不执行可拦截的 HLT**。结合此前
  v100c/v101 与“文件 I/O 触发 0x139”的实验，判定为平台级停机
  （最可能是中断/锁交互在 SVM guest 态的死锁或硬件级 halt，符合
  AMD 56683/errata 1363 方向）。
- 常驻 OS-as-guest 在 Ryzen 5 5500 当前方案下停线：step20 自旋 +
  宿主 ISR 是唯一 PASS 形态（guest 永不阻塞），无法支撑真实 Windows
  调度器。剩余路径为完整 APIC/TSC 虚拟化（AVIC 分支此前已失败）或
  换平台验证，成本高且无本机收敛证据。
- 收尾：建议把 `C:\yuanguard_hv.sys` 换回稳定默认版（SHA256
  `C8D5AD6863173555245F000642E753D5116DDE520132474C3E7CFC090A30C4C8`，
  `D:\aaaaaavm\yuanguard_hv_v100_step100.sys` 非此；默认版需重新构建或
  用此前 v54 归档），恢复非驻留保护路线可用状态。

### 9.102 2026-08-13 仓库整理、文档校对与提交

- 用户要求完整整理仓库、校对文档、同步内容、提交代码、清理。
- 校验结果：`git status` 11 个修改文件 + 2 个未跟踪
  （`build.bat.bak`、`docs/superpowers/plans/2026-08-12-os-as-guest-allcore-resident.md`）；
  handoff 共 75 节、无 NUL 字节，9.93-9.101 完整连续；`build.bat` 与
  原始版无内容差异（仅换行，git 不显示 diff）；`main.c` 默认
  `YGHV_BAREMETAL_STEP 0`，无临时硬编码。
- 清理：临时调试产物（`main_*.obj`/`main_*.i`、`build.bat.bak`）已移出
  仓库到 `D:\aaaaaavm\yghv_cleanup_20260813\`（文件删除被沙箱策略禁止，
  采用移出方式；均为 git 忽略文件，不影响仓库）。
- 同步：`docs/TASKS.md` 增补 2026-08-13 阶段结论；本节记录整理动作。
- 提交：将 v52-v102 常驻实验全部代码与文档（step100-102 诊断、
  0x139 两次 dump 结论、v102 全异常+HLT 拦截阴性、平台级停机停线）
  作为一次提交合入 main。
- 默认版重新构建验证通过：SHA256
  `EB4CA2A5B48EFE39D4FC2EA86DD9A841ED22860CBB1D0B77D1C734462786EF12`，
  归档 `D:\aaaaaavm\yuanguard_hv_default_20260813.sys`（含 v100-102
  代码、默认 step=0 路径），可用于恢复 C 盘安全版。
- 遗留：`C:\yuanguard_hv.sys` 当前仍为 v102（冻结版），恢复默认版需
  用户确认后另行构建替换；`D:\aaaaaavm\` 保留全部归档与 dump。

### 9.103 2026-08-13 决策：不常驻，转非驻留保护路线（含下一步规划）

- 决策：不再推进 OS-as-guest 常驻（平台级停机已确认），后续开发全部
  走非驻留保护路线（驱动加载 + NPT/NPF + VMMCALL hook + 控制通道 +
  Java/JNI 客户端）。
- 影响评估：功能层无损失（内存页写保护/终止保护/句柄保护/真实目标
  hook/add_page 翻译/Java 客户端均稳定 PASS）；失去的是“整机隐形 +
  不可卸载”两个远期目标（真实 OS 不在 guest 模式，CPUID/MSR/IO 隐身
  不覆盖真实系统，驱动架构上可卸载）；风险层反而更稳（无整机硬停）。
- 下一步路线（按优先级）：
  - P0 安全地基：R1 私有页剔除（VMCB/hsave 自剔除）+ NPT 默认权限收紧
    （默认 NX/RX，仅受保护页按需 RW），在实机非驻留路径验证；VMMCALL
    认证加固（现有 auth cookie 扩展为 per-handle/per-process）；
    控制面补 CPL/CR3 校验 + 目标进程生命周期处理。
  - P1 产品化：真实目标 hook 加固（terminate/handle 双 hook 落到真实
    进程）、多目标与配置 IOCTL 完善、驱动服务持久化与防卸载、Java
    客户端补 config/unprotect/scan 等命令。
  - P2 隐形与完整性：loader_stealth.c 接线（从加载模块列表隐藏）、
    MSR/IO 隐身（仅对受保护进程生效的替代方案）、hook 边界验证扩展。
  - 验证：默认版实机回归（selftest/exit-test）、VM 回归仅限安全路径
    （嵌套下关闭 INTR/NMI 拦截等已知限制）、每次改动先确认并记档。
- 待确认操作项：把 `C:\yuanguard_hv.sys` 从 v102 冻结版恢复为默认版
  （SHA256 `EB4CA2A5B48EFE39D4FC2EA86DD9A841ED22860CBB1D0B77D1C734462786EF12`，
  已归档 `D:\aaaaaavm\yuanguard_hv_default_20260813.sys`）。

### 9.104 2026-08-13 P0 R1 私有页剔除实现（实机验证版）

- 已确认并执行：恢复 C 盘默认版（`EB4CA2A5...`）→ 开始 P0 R1。
- 实现 `yghv_exclude_hv_private`（原为空壳）：对每个 vcpu 从 guest NPT
  剔除 vmcb/host_vmcb/hsave/host_stack(4 页)/msrpm(2 页)/iopm(3 页)，
  并 `npt_exclude_self` 剔除 NPT 表自身；**驱动镜像保持映射**（合成 guest
  从驱动 `.text` 执行，DriverEntry 设 `g_guest_hb_va = svm_trampoline_test_guest`）。
- 新增 `yghv_r1_exclude_check`：校验 vmcb PA 与 pml4 PA 的 NPT entry 已
  非 present、guest 代码页仍 present；DriverEntry 在
  `yghv_exclude_hv_private` 后调用，失败即清理并返回。
- 构建：默认版 SHA256
  `5B158E1B97489B3F1C6E6C2C8B328ECC62FF7F965241E54DF8482460EB401961`，
  归档 `D:\aaaaaavm\yuanguard_hv_default_r1exclude_20260813.sys`，已复制
  `C:\yuanguard_hv.sys`。
- 待实机验证：`sc start yuanguard`，期望日志 `exclude private ok →
  exclude check ok`，随后默认 NPT VMRUN 测试照常 PASS；若 guest 触碰
  私有页应 NPF 而不会读到 hypervisor 内存。

### 9.105 2026-08-13 R1 剔除实机回归：hook deny 路径死循环，改为编译门控

- 实机加载剔除版（`5B158E1B...`）：`exclude private ok → exclude check
  ok` 通过，NPT 测试 PASS；但默认路径 `yghv_hook_resident_test` 的
  deny 阶段陷入无限 `resident exit tick`，服务 StartPending、`sc stop`
  1052 无效，需重启清除。
- 结论：剔除与 hook deny 路径存在交互（大概率 NPF/rearm 或 VMMCALL
  deny 决策在私有页不可见后行为变化），不能直接并入稳定默认路径。
- 修正：新增 `YGHV_R1_EXCLUDE_PRIVATE` 编译开关（默认 0），
  `yghv_exclude_hv_private`/`yghv_r1_exclude_check` 及其 DriverEntry
  调用整体置于开关内；build.bat 支持 `YGHV_R1_EXCLUDE_PRIVATE=1`。
- 构建：稳定默认版（开关关）SHA256
  `35498382A8AA0E5220E634B612109357363C09A09FF2BD29D1AFE6F6D161B4FD`，
  归档 `D:\aaaaaavm\yuanguard_hv_default_stable_20260813.sys`（日志走
  `exclude private n/a`，不含剔除代码）；R1 门控版（开关开）SHA256
  `D2ED3F3CD8FB180B4506DDDBEACCFB5AB7654CA737174D2349258988C48B8524`，
  归档 `D:\aaaaaavm\yuanguard_hv_r1exclude_gated_20260813.sys`。
- 待办：用户重启清除卡死驱动后，把 `C:\yuanguard_hv.sys` 恢复为稳定
  默认版；R1 剔除与 hook deny 的交互单独排查（先复现 deny 死循环，
  再决定剔除范围或调整 deny 决策）。

### 9.106 2026-08-13 稳定默认版恢复实机验证 PASS

- 用户重启后，`C:\yuanguard_hv.sys` 已恢复为稳定默认版
  （SHA256 `35498382...`，`D:\aaaaaavm\yuanguard_hv_default_stable_20260813.sys`）。
- 实机 `sc start yuanguard`：服务 RUNNING，日志完整走完
  `exclude private n/a → NPT 测试 PASS（npf test → stop_internal）→
  protect/hook 测试（多轮 resident+stop_internal）→ cpu0 reset →
  remote started/ready → before heartbeat → after heartbeat → all stopped`，
  默认完整流程回归 PASS。
- 机器恢复可安全加载状态；R1 剔除与 hook deny 的交互问题保持门控，
  待单独排查。

### 9.107 2026-08-13 hook deny 死循环根因定位 + guest 栈修复

- 升级诊断：每次 `svm_core_enter_resident_current` 进入时编号并落盘前
  200 次退出（`r1 entry` / `r1 deny exit` / info2 / RIP），diag2 版
  SHA256 `C7E716DF...`。
- 实机现场：入口 4（hook deny 阶段）死循环为 **固定 RIP 反复 NPF 写
  GPA `0xc7de1fe8`**；该 GPA 与 vcpu `host_stack`（R1 已剔除的私有页）
  吻合 —— 合成 guest 的 `call rsi` 需要 push 到 guest 栈，而 guest
  RSP 一直继承自 hypervisor host_stack，剔除后写即 NPF → 注入 #PF →
  重执行 → 无限循环。前 3 个入口（NPT/protect/hook allow）不用栈或
  恰好在别处，故通过。
- 修复：每 vcpu 新增 `guest_stack`（1 页连续内存，NPT identity 可见、
  不剔除），`svm_core_enter_resident_current` 进入前把
  `vmcb.state.rsp` 指向 `guest_stack_pa + 页大小 - 0x10`；字段追加在
  `svm_vcpu_t` 末尾避免破坏汇编固定偏移断言；分配/释放/fail 路径已补。
- 构建：R1 修复版 SHA256
  `74FC87C546112259E99536035EDB9C1BED934DA46F46BE6C2989EB470D024C27`，
  归档 `D:\aaaaaavm\yuanguard_hv_r1exclude_gueststack_20260813.sys`；
  默认版（门控关）SHA256
  `A1E9743B38ACC1984874032090CE6CE0E7D902D35464712AE03215F1CB07C795`，
  归档 `D:\aaaaaavm\yuanguard_hv_default_stable2_20260813.sys`。
- 待验证：重启后加载 R1 修复版，预期入口 4 走 NPF/#DB/VMMCALL →
  stop_internal，hook resident PASS，后续测试继续到 `all stopped`，
  服务 RUNNING。

### 9.108 2026-08-13 guest 栈修复后 deny 仍整机硬冻结（R1 继续门控）

- 实机加载 R1 修复版（SHA256 `74FC87C5...`）：入口 1/2/3（NPT、protect、hook allow）全部 PASS；入口 4（hook deny）仍为进入后 0 次 VMEXIT 即整机硬冻结（日志停在 `resident start`，无 `r1 entry=4`、无 dump）。guest 栈修复消除了 NPF 写 host_stack 死循环，但 deny 路径在 guest 内直接停机，疑似 guest 内异常未被拦截后走 Windows 异常分发导致机器级 halt。
- 决策：R1 私有页剔除继续保留 `YGHV_R1_EXCLUDE_PRIVATE=0` 门控；`C:\yuanguard_hv.sys` 已恢复稳定默认版（SHA256 `A1E9743B38ACC1984874032090CE6CE0E7D902D35464712AE03215F1CB07C795`，归档 `D:\aaaaaavm\yuanguard_hv_default_stable2_20260813.sys`）。
- 后续选项：a) 继续 R1 排查（deny 入口加全异常/HLT 拦截捕获 guest 内异常现场，仍需重启验证）；b) 先转 P0 其它低风险项（VMMCALL 认证加固 / 控制面 CPL-CR3），R1 留待单独窗口。

### 9.109 2026-08-13 Codex 重启后配置与会话恢复

- Codex 重启后项目/会话在侧边栏丢失，用户已手动加回 D:\mcmodwork 与 D:\yuanguard。
- 核对结果：18 个非归档会话的正文文件与 state_5.sqlite 记录全部完整，未做覆盖；已归档会话按要求不恢复。
- 恢复动作：在 C:\Users\Administrator\.codex\config.toml 补回 5 个 primary-runtime 插件启用项（documents、pdf、spreadsheets、presentations、template-creator），保留当前模型/供应商/项目设置。
- 备份目录：C:\Users\Administrator\.codex\backup-20260813-160358-config-session-restore（config.toml、.codex-global-state.json、session_index.jsonl、state_5.sqlite）。

### 9.110 2026-08-13 deny 全异常+HLT 拦截仍零退出硬冻结：R1 停线门控

- 在 hook deny 入口对 VMCB 设置 `exception_intercepts=0xFFFFFFFF` +
  HLT 拦截 + `g_v102_catchall=TRUE`（`svm_dispatch_exit` 对任何异常/HLT
  退出即时落盘 `r1 catchall fault`）。构建 SHA256
  `7E44C9AF18AF4ED365848E034221768D7341B3875112F16251F5A7B96BBB2901`，
  归档 `D:\aaaaaavm\yuanguard_hv_r1exclude_catchall_20260813.sys`。
- 实机结果：入口 1/2/3 PASS，入口 4 仍停在 `resident start` 后整机硬冻结，
  **0 次 VMEXIT、无 `r1 catchall fault`、无 dump**。全异常 + HLT 全拦也
  抓不到，确认 deny 路径为 SVM 不可拦截的机器级停机（与 OS-guest
  v101/v102 同型）。
- 结论：R1 私有页剔除在 Ryzen 5 5500 上无法通过现有 hook deny 自测完成
  端到端验证，继续保持 `YGHV_R1_EXCLUDE_PRIVATE=0` 门控；guest 栈修复
  （guest_stack 独立页 + RSP 指向）保留（对默认路径无害，且消除合成
  guest 借用 host_stack 的问题）。`C:\yuanguard_hv.sys` 已恢复稳定默认版
  （`A1E9743B...`）。
- 建议下一步：转 P0 低风险项（VMMCALL 认证加固 / 控制面 CPL-CR3）；
  R1 剔除如需继续，建议换平台或在能抓 MCE/机器级现场的硬件调试器上验证。

### 9.111 2026-08-13 VMMCALL 认证加固实现 + 冻结根因修正（guest_stack 门控）

- 实现：`svm_vcpu_t` 末尾新增 `auth_key`（svm_alloc_vcpu 用系统时间 +
  私有 PA + core_id 派生）；`yghv_vmmcall_authorized(vcpu, cmd)` 分层：
  运行时命令（HEARTBEAT/VERSION/STATS）开放；Cookie 命令
  （HOOK_QUERY/STOP_INTERNAL/SHUTDOWN）要求 `rcx==cookie`；控制命令
  （SET_TARGET/ADD_PAGE/REMOVE_PAGE/START/STOP_PROTECT/GET_STATE）额外
  要求 `r8==vcpu->auth_key`。
- 构建：认证默认版 SHA256
  `1BD333E22C4A382FD318688DFD0E6020E12B94BCBFEF6E201AAEEACB8DECE47B`，
  归档 `D:\aaaaaavm\yuanguard_hv_default_auth2_20260813.sys`，已复制
  `C:\yuanguard_hv.sys`。
- 实机验证：服务 RUNNING，默认流程完整走完（NPT → protect/hook 含
  deny → 多核心跳 → `all stopped`），认证加固无回归。
- 冻结根因修正：此前认证默认版（`7E25457C...`）在 hook deny 整机冻结，
  定位为 `svm_core_enter_resident_current` **无条件把 guest RSP 指向
  guest_stack** 的行为改动；已改为仅在 `YGHV_R1_EXCLUDE_PRIVATE` 下
  设置 RSP，默认路径恢复原行为（与已验证稳定版一致）。R1 门控版仍
  存在 deny 机器级停机（见 9.108-9.110），保持门控。

### 9.112 2026-08-13 控制面提权校验 + VMMCALL 进程绑定实现

- 控制设备：`yghv_control_ioctl_authorized` 在原有 owner/CR3 绑定之上
  增加 `SeSinglePrivilegeCheck(SE_DEBUG_PRIVILEGE, UserMode)`，控制端
  必须是启用了 SeDebug 的提权客户端（Java/CLI 需以管理员并启用
  SeDebug 运行）。
- VMMCALL 进程绑定：
  - SET_TARGET：必须把目标设为自己（`PsLookupProcessByProcessId` +
    EPROCESS DirectoryTableBase 与调用者 CR3 比对），不能替别的进程
    设目标；
  - ADD_PAGE/REMOVE_PAGE：调用者 CR3 必须是当前受保护目标
    （`yghv_protect_is_target_cr3`），页面操作锁死在目标进程。
- 构建：默认版 SHA256
  `70888311B38EF8D386252271CAF8EC8ADE8D96FAA472C7260E30692B95F92E3B`，
  归档 `D:\aaaaaavm\yuanguard_hv_default_cpcr_20260813.sys`，已复制
  `C:\yuanguard_hv.sys`。
- 实机验证：服务 RUNNING，默认流程完整到 `all stopped`，无回归。

### 9.113 2026-08-13 阶段收尾：下一窗口交接摘要

- 提交：`b762594`（HEAD，main）；工作区干净。机器已停驱动（服务
  STOPPED），`C:\yuanguard_hv.sys` = 稳定默认版
  `70888311B38EF8D386252271CAF8EC8ADE8D96FAA472C7260E30692B95F92E3B`
  （归档 `D:\aaaaaavm\yuanguard_hv_default_cpcr_20260813.sys`）。
- 当前结论：OS-as-guest 常驻线停线（平台级停机，见 9.84-9.102）；R1
  私有页剔除门控关闭（`YGHV_R1_EXCLUDE_PRIVATE=0`，deny 机器级停机见
  9.108-9.110）；非驻留保护路线稳定。已完成 P0：VMMCALL 认证分层 +
  per-vcpu auth_key（9.111）、控制面 SeDebug + VMMCALL 进程绑定
  （9.112）。
- 待续：Java/CLI 客户端需管理员+SeDebug 验证 `protect/list-java`；
  剩余 P0/P1（真实 hook 加固、多目标/配置 IOCTL、持久化防卸载、
  loader_stealth 等）见 9.103 路线。

### 9.114 2026-08-13 Java/CLI 客户端 SeDebug 显式启用 + 实机回归 PASS

- 背景：9.112 控制面新增
  `SeSinglePrivilegeCheck(SE_DEBUG_PRIVILEGE, UserMode)` 后，CLI/Java
  客户端均未显式启用 SeDebugPrivilege，管理员默认也不自动启用，
  `protect/selftest/exit-test` 可能 ACCESS_DENIED。
- 实现（用户已确认第一阶段，仅改客户端，驱动未改动）：
  - `tools\yghv_ctl.ps1`：新增
    `YghvPrivilege.EnableSeDebugPrivilege()`（OpenProcessToken +
    LookupPrivilegeValue + AdjustTokenPrivileges，
    TOKEN_ADJUST_PRIVILEGES|TOKEN_QUERY，SE_PRIVILEGE_ENABLED），打开
    设备前调用，失败即报错退出；
  - `tools\yghv_client\native\yghv_ctl_jni.c`：新增
    `Java_YghvCtl_enableSeDebug`（同逻辑，返回 Win32 错误码）；
  - `tools\yghv_client\build_jni.bat`：link 补 `advapi32.lib`；
  - `tools\yghv_client\YghvCtl.java`：`open()` 先调 `enableSeDebug()`，
    失败即退出。
- 实机回归（稳定默认版 `70888311...`，`sc start yuanguard` → RUNNING）：
  - PowerShell `state` PASS；`selftest` PASS（set-target/add-page/start/
    写读/stop/remove 全链路）；`exit-test` PASS（子进程退出后 auto
    disarm 到 0）；
  - Java `state`/`list-java` PASS；
  - Java `protect 10704 64` PASS（枚举 512 页、add 64 页封顶、start，
    state active=1 page_count=64）；终止 Sleepy 后 auto disarm 到
    active=0 pid=0 page_count=0 PASS；
  - `sc stop yuanguard`：正常卸载 STOPPED。
- 发现（记录待办，本次不修改）：驱动启动自测后 `g_protect.page_count`
  残留 2 页（稳定默认版可复现：`state` 观察到 3→2 后稳定在 2；selftest
  set-target 清空后不再回来）。疑似 hook 自测/rendezvous 的 remove 路径
  在持久 resident 下未完全清理，下一窗口定位（与本次客户端改动无关）。
- 构建说明：JNI DLL/.class 均在 .gitignore，不入库，仅源码入库；C 盘
  驱动未改动，仍为 `70888311...`。

### 9.110 2026-08-13 Codex 再次重启后项目注册表恢复

- 第二次重启后 `.codex-global-state.json` 的 `local-projects` 被应用重置为空，项目再次从侧边栏消失；`config.toml` 与 `state_5.sqlite` 会话记录仍完整。
- 修复：将备份中的 yuanguard/mcmodwork 项目记录、`project-order`、projectless thread 元数据合并回 `.codex-global-state.json`，并同步 `.codex-global-state.json.bak`。
- 新备份：`C:\Users\Administrator\.codex\backup-20260813-173415-restore-after-restart`。
- 若再次重启后项目又消失，可用该备份里的 `.codex-global-state.json` 恢复，或重新执行合并。

### 9.111 2026-08-13 Codex 运行时改写全局状态说明 + 一键恢复脚本

- 第二次直接合并 `.codex-global-state.json` 后，运行中的 Codex 约 1 分钟内把 `local-projects` 又写回空（文件 mtime 17:34:58）。
- 初步根因：应用以内存中的空项目列表覆盖磁盘状态；会话正文、state_5.sqlite 与 config.toml 均未损坏。
- 新增恢复脚本：`C:\Users\Administrator\Documents\Codex\2026-08-13\w\restore-codex-projects.ps1`。
- 使用方式：完全退出 Codex 后运行 `powershell -ExecutionPolicy Bypass -File C:\Users\Administrator\Documents\Codex\2026-08-13\w\restore-codex-projects.ps1`，再重新打开 Codex；脚本会从 `backup-20260813-160358-config-session-restore` 合并项目并同步 `.bak`。

### 9.112 2026-08-13 Codex 项目恢复脚本修正（进程名检测）

- 原因：桌面应用进程名是 `ChatGPT`，旧恢复脚本只检测 `Codex*`，导致在应用仍在运行时写入 `.codex-global-state.json`，随后被应用内存中的空项目列表覆盖。
- 修复：`restore-codex-projects.ps1` 现在检测 `ChatGPT` / `Codex*` / `OpenAI.Codex*` 进程，应用未退出时直接拒绝写入；写入后还会自检 `local-projects` 非空。
- 新增双击入口：`C:\Users\Administrator\Documents\Codex\2026-08-13\w\restore-codex-projects.cmd`。
- 使用方式：完全退出 Codex（确认任务管理器无 ChatGPT 进程）→ 双击 `restore-codex-projects.cmd` 或运行 `powershell -ExecutionPolicy Bypass -File ...\restore-codex-projects.ps1` → 重新打开 Codex。

### 9.113 2026-08-13 Windows PowerShell 5.1 UTF-8 读取修复

- 用户运行脚本报 `ConvertFrom-Json : 传入的数组无效`；原因是 `powershell.exe`（5.1）默认按 ANSI/GBK 读取 UTF-8 的 `.codex-global-state.json`，`prompt-history` 中文被读坏。
- 已确认文件本身是有效 UTF-8 JSON。
- 脚本已改为用 `[System.IO.File]::ReadAllText(..., UTF8)` 读取、`WriteAllText(..., UTF8 no BOM)` 写入；Windows PowerShell 5.1 下模拟合并自检通过，恢复出 yuanguard/mcmodwork。
- 使用方式不变：完全退出 Codex 后运行 `restore-codex-projects.cmd`。

### 9.114 2026-08-13 桌面恢复工具（参考 Pankaj-1N 实现）

- 参考 GitHub 上的 `Pankaj-1N/codex-desktop-windows-sidebar-recovery` 实现，把恢复工具放入桌面单独文件夹：`C:\Users\Administrator\Desktop\Codex-Project-Restore`。
- 内容：`restore-codex-projects.cmd`、`restore-codex-projects.ps1`、`recovery_core.py`、`codex-sidebar-recovery.ps1`、`restore-backup.ps1`、`restore-thread-titles.py`、`codex-projects-backup.json`、README.md、LICENSE/NOTICE。
- 改进：正式恢复会重建 `project-writable-roots`、`thread-project-assignments`、`sidebar-project-thread-orders`，强制项目视图模式，修复 `session_index.jsonl`，再合并备份中的 yuanguard/mcmodwork，保留后续新增项目。
- 使用：双击 `restore-codex-projects.cmd`；若 Codex 仍在运行会等待退出，然后自动执行恢复，完成后重新打开 Codex。
- 已做 dry-run 验证：sqlite_integrity=ok，active=17，real=13，recoverable_projects=3（yuanguard、mcmodwork、plugin-e2e），无写入。

### 9.115 2026-08-13 真实 hook 加固：MDL 可写映射 + 原生 allow/deny 验证（PASS）

- 蓝屏事件复盘：门控版真实 hook 首次实机在安装 `ZwOpenProcess` 时触发
  `0x50 PAGE_FAULT_IN_NONPAGED_AREA`（17:28，dump 已归档
  `D:\aaaaaavm\yghv_bsod_0x50_realhook_20260813_1728.dmp`）。根因：
  `MmGetVirtualForPhysical` 对 ntoskrnl 映像物理页返回的是只读映像地址，
  `RtlCopyMemory` 直接写入口即 0x50；不是 CR3 跳板逻辑错误。
- 修复（用户已确认）：`protect.c` 新增
  `yghv_protect_map_writable_page`（`IoAllocateMdl` +
  `MmBuildMdlForNonPagedPool` + `MmMapLockedPagesSpecifyCache`）与
  `yghv_protect_unmap_writable_page`，install/remove 一律通过 MDL 临时
  可写映射写补丁/恢复原字节，写完立即 unmap；NPT 对函数页剔除写权限的
  保护语义不变，临时写映射只在安装/移除瞬间存在。
- 测试修正（用户已确认）：dump 反汇编确认 19045 上 `ZwTerminateProcess`
  入口经 `KiServiceLinkage`/`KiServiceInternal` 进真实服务分发，guest 内
  `call` 真实 `Zw*` 的模拟不成立（allow/deny 都返回同一怪值
  `0x101bbcff`）。`main.c` 的 `yghv_real_hook_test` 改为原生调用
  `ZwTerminateProcess((HANDLE)0xDEADBEEF)` 验证：allow 期望正常错误码
  （实测 `0xC0000008`），deny 期望 `0xC0000022`（实测 PASS）。
- 断言修正：`NTSTATUS` 为 32 位 `long`，比较常量改为 `0xC0000022L`；
  此前 `0xC0000022ULL` 与 `long` 比较恒真/恒假，会导致 deny 永远 FAIL。
- 实机验证（门控版 `YGHV_REAL_HOOK_TEST=1`，SHA256
  `88D7D9866722C74CEE59E7909C9BC3E13F969AE3E2E11F6BA8DBAA7A31DE0CDC`，
  归档 `D:\aaaaaavm\yuanguard_hv_realhook_mdl_native_20260813.sys`）：
  服务 RUNNING，完整自测 PASS——hook test / hook resident allow+deny /
  boundary / real hook open ok / real hook allow=0xC0000008 /
  deny=0xC0000022 / 多核心 heartbeat / all stopped，无回归。
- 收尾：`sc stop yuanguard` 后 `C:\yuanguard_hv.sys` 已恢复稳定默认版
  `70888311B38EF8D386252271CAF8EC8ADE8D96FAA472C7260E30692B95F92E3B`
  （归档 `D:\aaaaaavm\yuanguard_hv_default_cpcr_20260813.sys`），服务
  STOPPED。本轮提交 5 个文件（build.bat、protect.h、protect.c、main.c、
  交接文档），保留上一轮遗留的 9.110-9.114 Codex 恢复记录。
- 待续：阶段 2B 多目标/配置 IOCTL + 定位驱动启动自测后
  `g_protect.page_count=2` 残留；阶段 2C 驱动服务持久化/防卸载与
  `loader_stealth.c` 接线。

### 9.116 2026-08-13 阶段 2B-1：配置面 IOCTL + 客户端命令 + 冻结根因修复（PASS）

- `page_count=2` 澄清：默认 `YGHV_RESIDENT_WORKLOAD_TEST=1`，稳定默认版
  启动即进入持久保护模式（workload 页 + dummy hook 页 = 2 页），
  `state` 显示 `page_count=2` 是设计行为，不是残留；9.114 记录为待办是
  误判，本节修正。
- 新增 IOCTL（`control_ioctl.h`）：`GET_TARGET 0x806`、
  `GET_PAGES 0x807`、`GET_HOOKS 0x808`、`CLEAR 0x809`、
  `SET_CONFIG 0x80A`、`GET_CONFIG 0x80B`；`protect.h` 新增
  `yghv_protect_config_t`（auto_disarm/deny_status）与查询信息结构。
- `protect.c`：新增目标/页/hook 查询、`yghv_protect_clear`、
  `set/get_config`；NPF 写策略受 `config.auto_disarm` 控制（默认 1）；
  hook stub deny 路径改为动态读取 `g_protect.config.deny_status`
  （`movabs rax,&deny_status; mov eax,[rax]; ret`），deny 返回码可配置。
- 客户端：`tools\yghv_ctl.ps1` 新增
  `target/list-pages/list-hooks/clear/config` 命令，`selftest` 增加
  `list-pages` 校验；`YghvCtl.java` 同步新增相同命令。
- 冻结调查（本轮三次无 dump 硬冻结：18:26/18:32/18:46/18:54，Event
  6008 意外关机）：稳定默认版 `70888311` 对照连续 IOCTL 不冻结；给
  控制面新增独立运行期日志 `C:\Windows\yghv_ioctl.log` 后，最后一条
  为 `SET_CONFIG (0x59472028)` 且无返回。根因：`auto_disarm=0` 时 NPF
  对持久 workload guest 的受保护页写注入 `#PF`，裸机 guest 无异常处理，
  平台级硬冻结（无 minidump）。
- 修复（用户已确认）：`yghv_protect_set_config` 在 `g_protect.active`
  时拒绝 `auto_disarm=0`（返回 `STATUS_ACCESS_DENIED`），停止保护后
  才允许设置；同时默认关闭 `YGHV_HOOK_RENDEZVOUS_TEST`（原 persistent
  模式 8 秒后后台 install/remove hook，持 `g_protect_lock` 全核心暂停，
  与 NPF handler 存在 AB-BA 死锁风险，属自测残留，产品线不启用）。
- 实机验证（新默认版 SHA256
  `3F9B85B456C9884C4126B6792304A447BB350A7850417B11A1F0086EA1D36E0C`，
  归档 `D:\aaaaaavm\yuanguard_hv_default_cfg_20260813.sys`）：只读 5 命令
  序列、`GET_CONFIG`、active 下 `auto-disarm 0` 拒绝（error 0x5 不冻结）、
  stop 后设置/恢复、`deny-status` 设置/恢复、新 `selftest`（含
  list-pages 校验）PASS、`clear`、Java `config/target/list-pages/list-hooks`
  及拒绝路径均 PASS；服务重启后持久保护恢复 2 页。
- 已知限制（记录待办）：`clear` 后 `hook_count` 可能残留 1
  （remove_hook 的全核心 pause 超时被忽略），需 `sc stop/start` 或后续
  阶段 2B-2 优化 remove 的 pause/锁顺序；`auto_disarm=0` 只能在保护停止
  时启用；运行期 IOCTL 日志写入 `C:\Windows\yghv_ioctl.log`。
- 收尾：`sc stop yuanguard`，`C:\yuanguard_hv.sys` 恢复稳定默认版
  `70888311B38EF8D386252271CAF8EC8ADE8D96FAA472C7260E30692B95F92E3B`，
  服务 STOPPED。提交包含驱动、`control_ioctl.h`、PowerShell/Java 客户端
  源码与交接文档；Java `.class`/JNI DLL 不入库。

### 9.117 2026-08-13 阶段 2B-2：真实 hook 接入控制面（PASS）

- 新增 IOCTL：`INSTALL_HOOK 0x80C` / `REMOVE_HOOK 0x80D`。
  `yghv_protect_install_hook_info_t`（hook_id/flags/func_va/name[64]，
  支持按导出名或 VA 安装）、`yghv_protect_remove_hook_info_t`。
- 安全绑定：`INSTALL_HOOK` 若调用者 CR3 不是当前 target，自动
  `yghv_protect_set_target(调用者)`（等价“保护当前进程”，与 P0 只能设
  自己一致，但会清空原有保护页）；`REMOVE_HOOK` 严格要求调用者就是
  当前 target，否则 `STATUS_ACCESS_DENIED`；名字未以 null 结尾返回
  `STATUS_INVALID_PARAMETER`。
- 客户端：PowerShell/Java 新增
  `install-hook <name|hex_va> [hook_id]`（默认 hook_id=2）和
  `remove-hook <hook_id>`；`list-hooks` 复用。
- 实机验证（新默认版 SHA256
  `FB410209D9D15736C850B21DF3FC2A3AE93EF0951738FF010595444299D5B5C7`，
  归档 `D:\aaaaaavm\yuanguard_hv_default_hookctl_20260813.sys`）：
  - 后台存活 PowerShell 进程 `install-hook ZwOpenProcess 1`，期间
    `list-hooks` 显示 id=0 dummy + id=1 ZwOpenProcess（真实地址），
    `remove-hook 1` 后恢复；调用者进程退出后 heartbeat 自动清理 target，
    `state/target` 回 active=0 pid=0 page_count=0；
  - Java `install-hook ZwTerminateProcess 3` OK；全程无冻结。
- 语义确认：hook 目标是内核 `Zw*` 入口（内核态调用路径），用户态
  syscall 不走该入口；allow/deny 决策语义已在 9.115 门控真实 hook 测试
  验证（原生调用 allow 返回正常错误码、deny 返回 deny_status）。
- 已知限制（延续记录）：dummy hook 0 在切换 target/clear 后可能残留；
  INSTALL_HOOK 自动切换 target 会清空原保护页，需要先
  `sc stop/start` 恢复默认持久保护；remove 的全核心 pause 锁序优化
  留待后续阶段。
- 收尾：`sc stop yuanguard`，`C:\yuanguard_hv.sys` 恢复稳定默认版
  `70888311...`，服务 STOPPED；本轮提交驱动、`control_ioctl.h`、客户端
  源码与交接文档。

### 9.115 2026-08-13 Codex 项目/会话正式恢复 + 状态文件保护

- 修复桌面脚本：进程判断改为只认 `ChatGPT.exe`（OpenAI.Codex 路径）与 `codex.exe`（WindowsApps/resources 路径），避免误判 `D:\mcmodwork\codex-patched\codex.exe`。
- `restore-codex-projects.cmd` 改为先运行 `kill-codex.ps1`，再执行 `-Apply -Protect`；新增 `unprotect-codex.cmd` 用于解除只读。
- 正式恢复已执行：backup `C:\Users\Administrator\.codex\backups\windows-sidebar-recovery-20260813-194000`；local-projects=4、assignments=12、orders=4、mode=project、session_index=32 行。
- `.codex-global-state.json` 已临时设为只读，防止 Codex 启动时覆盖；项目显示后运行 `unprotect-codex.cmd` 解除。

### 9.116 2026-08-13 修复只读文件导致的恢复报错

- 用户运行恢复时报 `[WinError 5] 拒绝访问: .codex-global-state.json.windows-recovery.tmp -> .codex-global-state.json`；原因是上一步恢复后状态文件被设为只读，恢复核心原子替换时被系统拒绝。
- 修复：`restore-codex-projects.ps1` 在正式恢复前先解除 `.codex-global-state.json` 只读，恢复完成后再按 `-Protect` 决定是否重新加锁。
- 已重新执行正式恢复：backup `C:\Users\Administrator\.codex\backups\windows-sidebar-recovery-20260813-194307`，状态正常，文件再次处于只读保护。
- 用户下一步：完全退出 Codex 后重开；项目显示后运行 `unprotect-codex.cmd` 解除只读。

### 9.117 2026-08-13 主进程内存空项目修复（CDP 注入）

- 根因：磁盘 `.codex-global-state.json` 已正确，但 Codex 主进程内存中的 `local-projects` 仍为空，导致侧边栏显示“没有项目”；只改磁盘不刷新运行中的主进程。
- 修复：通过 Codex 调试端口（127.0.0.1:9229）调用应用自身 RPC `add-workspace-root-option`，把 `D:\mcmodwork`、`D:\yuanguard` 注入运行中的主进程。
- 结果：侧边栏已显示 `yuanguard`、`mcmodwork` 及对应会话；主进程 `get-global-state` 返回两个项目。
- 磁盘状态仍为只读保护，重启后也会读取正确状态；确认显示正常后运行桌面 `unprotect-codex.cmd` 解除。

### 9.118 2026-08-13 2B-2 锁序优化实机失败回退（保留 9.117 基线）

- 尝试：为修复 `clear`/remove 的 `hook_count=1` 残留，把 install/remove/
  clear 的 `svm_core_pause_residents_for_patch` 从持锁内部移到取锁之前，
  新增 `g_hook_patch_active` 补丁互斥（`yghv_protect_patch_begin/end`），
  locked 函数不再自行 pause/resume。
- 实机结果：新构建启动自测 PASS、服务 RUNNING；但控制面
  `install-hook ZwOpenProcess`（后台存活进程）触发整机硬冻结
  （19:19:18 Event 6008 意外关机，无 minidump），与 9.117 已验证版本
  相同流程两次通过形成对照，判定锁序改动在本机不稳定。
- 决定：回退 `protect.c` 至 9.117 提交基线（`d5631ef`），`clear` 的
  `hook_count=1` 残留与 remove 全核心 pause 锁序问题保留为已知限制，
  不再本机硬啃；如需继续建议换平台或硬件调试器。
- 当前状态：C 盘恢复稳定默认版 `70888311...`，服务 STOPPED；工作区
  `protect.c` 已回退，无锁序实验代码残留。docs 中 9.115-9.117 Codex
  恢复记录为用户工作区已有内容，保留未动。

### 9.118 2026-08-13 桌面新增在线注入脚本

- 新增 `restore-live-inject.ps1` / `restore-live-inject.cmd`：Codex 已打开但项目消失时，通过调试端口 9229 调用 `add-workspace-root-option` 注入 D:\mcmodwork、D:\yuanguard。
- 已测试幂等：项目已存在时自动跳过，不会重复创建。
- 使用方式：Codex 运行中直接双击 `restore-live-inject.cmd`；磁盘恢复仍用 `restore-codex-projects.cmd`。

### 9.119 2026-08-13 阶段 2C：loader_stealth 接线 + 服务持久化/防卸载（PASS）

- 2C-1 `loader_stealth` 接线：`build.bat` 把 `loader_stealth.c` 纳入编译
  与链接；`main.c` 新增 `YGHV_LOADER_STEALTH` 门控（默认 0），开启时
  DriverEntry 最早调用 `yghv_loader_stealth`（从 `PsLoadedModuleList`
  摘链并清模块名）。`loader_stealth.c` 补 `<stdint.h>`、去掉重复
  `YGHV_DEBUG_LOG` 定义。门控版 SHA256
  `7B9CEECB37129839536B6437AC405BA0AAAC0551C4A3826612158797B51B5034`
  （归档 `D:\aaaaaavm\yuanguard_hv_loader_stealth_20260813.sys`）实机
  加载 RUNNING，`state/list-hooks` 正常，无蓝屏；模块列表隐藏效果需
  kd `!driver yuanguard` 复核，后续窗口记录。
- 2C-2 服务持久化/防卸载（`yghv_ctl.ps1`）：
  - `set-auto-start` / `unset-auto-start`：`sc config yuanguard start=
    auto/demand`，实机验证注册表 `Start` 2↔3；
  - `harden-service`：先 `sc sdshow` 备份 SDDL 到
    `D:\aaaaaavm\yghv_service_sddl_backup.txt`，再用 `sc sdset` 加
    `(D;;SD;;;BA)` Deny DELETE；实测 `sc delete yuanguard` 返回 error 5
    Access denied，服务保留；
  - `unharden-service`：用备份 SDDL 恢复，实测恢复默认 SDDL。
- 方案更正：注册表键 ACL 防删对 SCM 无效（services.exe 已持有键句柄，
  DeleteService 不重新 AccessCheck），已弃用改为 `sc sdset` 服务安全
  描述符。
- 收尾：C 盘恢复稳定默认版 `70888311...`，服务 STOPPED（demand、
  unharden）；工作区提交 build.bat、loader_stealth.c、main.c、
  yghv_ctl.ps1 与交接文档（docs 同时包含用户工作区已有的 9.118 Codex
  在线注入记录）。
- 待续：2C-3 内核侧防卸载（不注册 DriverUnload / 拦截 NtUnloadDriver）
  未做，风险高需独立门控评估；loader_stealth 默认关闭，产品化时再并入
  默认构建。

### 9.120 2026-08-13 阶段 2C-3：内核侧防卸载门控实验（PASS）

- `harden-service` 扩展：Deny ACE 由 `SD` 改为 `SDWP`（拒绝管理员
  DELETE + STOP），实测 `sc stop yuanguard` 返回 error 5 且服务保持
  RUNNING；`unharden-service` 恢复默认 SDDL 后 `sc stop` 正常。
- 新增 `YGHV_UNLOAD_GUARD` 门控（`build.bat` 支持，默认 0）：开启时
  DriverEntry 不注册 `DriverUnload`（`d->DriverUnload` 保持 NULL）。
  门控版 SHA256
  `8B147F299CACFFD653C15A255F33CD950EC884531CCDE0BC189F03FEC6F36109`
  （归档 `D:\aaaaaavm\yuanguard_hv_unload_guard_20260813.sys`）实机验证：
  - 加载 RUNNING，`state` 功能正常（active=1 page_count=2），无蓝屏；
  - `sc stop` → 1052，服务变为 `NOT_STOPPABLE`；
  - `unload_driver.ps1`（`NtUnloadDriver`）→ `0xC0000010`
    （STATUS_INVALID_DEVICE_REQUEST），驱动保持加载。
- 解除方式：重启（服务 `DEMAND_START` 不自动加载），重启后覆盖
  `C:\yuanguard_hv.sys` 为稳定默认版即可恢复卸载能力。当前 C 盘仍为
  门控版且驱动 RUNNING 不可卸载，待用户重启后恢复 `70888311...`。
- 待办：loader_stealth 的 kd `!driver` 复核需内核调试会话；门控默认
  关闭，产品化时再决定并入默认构建与卸载逃生通道。

### 9.121 2026-08-13 重启后恢复稳定基线

- 用户重启完成：`yuanguard` 服务 STOPPED（`DEMAND_START` 未自动加载），
  `C:\yuanguard_hv.sys` 已从 2C-3 门控版 `8B147F29...` 恢复为稳定默认版
  `70888311B38EF8D386252271CAF8EC8ADE8D96FAA472C7260E30692B95F92E3B`。
- 2C-3 防卸载门控实验闭环：门控版唯一卸载通道为重启，重启后恢复正常
  加载/卸载能力；该门控保持默认关闭，产品化时再设计逃生通道。
- 当前机器安全基线恢复，工作区干净，HEAD `1cecd2f`。

### 9.122 2026-08-13 综合回归收尾（2B-1/2B-2/2C 全套 PASS）

- 最新默认构建（无 `YGHV_LOADER_STEALTH`/`YGHV_UNLOAD_GUARD` 门控）
  SHA256 `8A9734CA2E0A8B9C7DD94854F2F8660747413F570C7A06AD0256C41E6F0059F5`，
  归档 `D:\aaaaaavm\yuanguard_hv_default_full_20260813.sys`。
- 实机回归清单全部 PASS：`state/target/list-pages/list-hooks/config`
  读取；`config` stop 后 `auto-disarm 0/1` 与 `deny-status` 设置/恢复；
  `selftest`（含 list-pages 校验）；`exit-test`（target 退出自动 disarm）；
  后台进程 `install-hook ZwOpenProcess` → `list-hooks` 可见 →
  `remove-hook` → 清理；`clear`；`set-auto-start/unset-auto-start`
  （Start 2↔3）；服务重启后默认持久保护恢复 2 页；Java
  `config/target/list-pages/list-hooks/install-hook` 全 PASS；全程无冻结。
- 已知限制不变：`clear` 后 `hook_count=1` 残留（remove 全核心 pause 锁序
  问题，待 2B-3-b/换平台处理）。
- 收尾：`sc stop yuanguard`，`C:\yuanguard_hv.sys` 恢复稳定默认版
  `70888311...`，服务 STOPPED；下一步进入 2B-3-a 多目标状态结构重构
  （保持单目标行为，先回归再开放多目标）。

### 9.123 2026-08-13 2B-3-a 多目标状态结构重构（保持单目标行为，PASS）

- `protect.h`：新增 `YGHV_PROTECT_MAX_TARGETS 4` 与
  `yghv_protect_target_t`（pid/flags/cr3/page_count/pages/process），
  `yghv_protect_state_t` 改为 `target_count + targets[4] + config + active`。
- `protect.c`/`main.c`：所有 `g_protect.pid/cr3/process/flags/page_count/
  pages` 机械改写为 `g_protect.targets[0].*`（单目标槽 0），hook stub
  地址同步为 `&g_protect.targets[0].cr3`；`init` 置 `target_count=1`，
  config/active 保持 state 顶层。
- 实机回归（新默认版 SHA256
  `C9638324855A298F225A1A22DC9900892D2366E9B742DDF70EBADCB6DE4668C3`，
  归档 `D:\aaaaaavm\yuanguard_hv_targets_refactor_20260813.sys`）：启动
  自测（hook resident allow/deny、boundary、多核 heartbeat、all
  stopped）PASS；`state/target/list-hooks/selftest/exit-test` PASS；
  无冻结。
- 收尾：C 盘恢复稳定默认版 `70888311...`，服务 STOPPED；下一步
  2B-3-b 开放多目标（按 cr3 绑定槽、stub 遍历、list-targets）。

### 9.124 2026-08-13 2B-3-b 完整多目标（b1+b2 合并，PASS）

- 目标槽注册：`yghv_protect_set_target` 按 pid 查找/分配槽（槽 0 保留给
  System 基架，客户端从槽 1 起），`target_count` 上限 4。
- 控制面按 CR3 绑定：新增 `add_page_for/remove_page_for`，ADD/REMOVE_PAGE
  IOCTL 用调用者 CR3 选槽；旧 API 包装槽 0 供自测/持久保护。
- NPF/决策跨槽：`is_target_cr3`/`find_page` 遍历全部槽，start/stop 遍历
  全部槽，`on_target_exit` 按 pid 清对应槽（hooks 全局，不再随目标退出
  自动移除），`clear` 清空全部槽并把 `target_count` 复位为 1。
- hook stub 多目标：改为 unrolled 遍历 `targets[0..3].cr3`（按
  `sizeof(yghv_protect_target_t)` 步长 disp32 读取，动态生效），orig slot
  移到 0x90；不再依赖需刷新的 cr3_list。
- 新增 `GET_TARGETS 0x80E` + PowerShell/Java `list-targets`；`selftest`
  与 `exit-test` 改为多目标语义（自己槽 page_count、list-pages 包含
  addr、child 槽 auto cleared）。
- 实机验证（SHA256
  `14253486481881910CC19EE8CD64AE70E7BB1CADDCBE810462AB91F8359C3F9E`，
  归档 `D:\aaaaaavm\yuanguard_hv_multi_target_20260813.sys`）：启动自测
  hook resident deny=0xC0000022 PASS；`selftest`/`exit-test` PASS；
  双后台进程各注册槽 + install-hook 共存（list-targets 显示 System+客户端
  槽、list-hooks 双 hook）；后台退出自动清槽；`clear` 清空全部槽与 hooks；
  服务重启恢复默认保护；Java `list-targets/state` PASS；无冻结。
- 已知限制（记录待办）：`g_hook_patch_active` 补丁互斥可能因后台进程在
  install 期间异常退出而残留，后续 install 返回 `ERROR_BUSY`，需重启驱动
  恢复（建议后续加超时/owner 清理）；hooks 为全局表，目标退出不自动
  移除；install_hook 的 hook 函数页计入槽 0。
- 收尾：`sc stop yuanguard`，C 盘恢复稳定默认版 `70888311...`，服务
  STOPPED；提交 6 个文件（control_ioctl.h、protect.h、control_device.c、
  protect.c、YghvCtl.java、yghv_ctl.ps1）+ 交接文档。

### 9.125 2026-08-13 BUSY 诊断与 clear 硬冻结复现（本机停止 hook 路径实验）

- 澄清：9.124 记录的 `g_hook_patch_active` 补丁互斥其实不存在——9.118
  锁序实验回退时 `patch_begin/patch_end` 一并回退，当前 install/remove
  仍是旧锁序（持 `g_protect_lock` 内全核心 pause）。`ERROR_BUSY (0x5AA)`
  的真实来源未定位，代码中无 `STATUS_DEVICE_BUSY` 返回点。
- 诊断加装：`protect.c` 新增 `yghv_hook_diag`，install/remove 各失败
  stage（map/pause/split/perm/addpage/missing/arm/removepage/fail）落盘
  `C:\Windows\yghv_hook.log`。诊断版 SHA256
  `48824837BF11E205A92EAEF56B3D6364A88D496B8936C29DCA5AC07DDD645836`
  （归档 `D:\aaaaaavm\yuanguard_hv_hook_diag_20260813.sys`）。
- 重现尝试：并发 install（后台+前台）未复现 BUSY（双 hook 安装成功）；
  随后 `clear`（含 remove_hook 的持锁全核心 pause）触发整机硬冻结
  （21:05:39 Event 6008，无 minidump），复现 9.118 已记录的 pause/锁序
  死锁风险。诊断日志未留下失败 stage（冻结发生在 pause 等待期间）。
- 结论：本机不再进行 install/remove/clear 类 hook 路径实机验证，该族
  问题留待换平台或硬件调试器；`ERROR_BUSY` 保持低频残余风险记录。
- 收尾：`C:\yuanguard_hv.sys` 恢复稳定默认版 `70888311...`，服务
  STOPPED；提交 protect.c 诊断代码与交接文档。

### 9.126 2026-08-13 非 hook 路径综合回归（最新多目标+诊断版 PASS）

- 回归版本：`48824837BF11E205A92EAEF56B3D6364A88D496B8936C29DCA5AC07DDD645836`
  （即多目标 + hook 诊断默认构建，归档
  `D:\aaaaaavm\yuanguard_hv_hook_diag_20260813.sys`）。
- 实机清单全部 PASS：`state/list-targets/list-pages/config` 读取；
  `config` stop 后 `auto-disarm 0/1` 与 `deny-status` 设置/恢复；
  `selftest`（多目标语义，含 list-pages 校验）；`exit-test`（child 槽
  auto cleared）；`set-auto-start/unset-auto-start`（Start 2↔3）；服务
  RUNNING 稳定，无冻结。
- 该轮刻意避开 hook 安装/移除与 `clear`（本机 pause 死锁已知风险，
  见 9.118/9.125），确认查询/配置/页保护/服务命令在最新多目标代码上
  无回归。
- 收尾：`sc stop yuanguard`，`C:\yuanguard_hv.sys` 恢复稳定默认版
  `70888311...`，服务 STOPPED；工作区干净，HEAD `772de94`。

### 9.127 2026-08-13 进度保存 + 仓库清理 + 新窗口交接

- 仓库清理（用户授权）：删除未跟踪/忽略的构建产物与临时文件
  （YuanGuardHV/bin、logs_archive、kd_cmd/kd_ctl_*.txt/log、
  svm_trampoline.asm、.superpowers/、YghvCtl.class、Sleepy.class、
  native/yghv_ctl_jni.dll/exp/lib）；`.gitignore` 覆盖项不变；
  `reference/` 等被跟踪参考代码保留；归档目录 `D:\aaaaaavm` 不受影响。
- 文档核实：`docs/YUANMOD_HANDOFF_CURRENT.md` 主线记录到 9.126，
  9.110-9.118 存在驱动与 Codex 桌面恢复记录编号重复（历史遗留，
  内容有效，不重排）；工作区干净，HEAD `f833ab1`。
- 新增 `docs/YUANMOD_NEXT_WINDOW_PROMPT.md`：完整新窗口提示词，包含
  铁律、当前状态、关键结论、本机雷区、归档清单、下一步建议、常用命令
  与文档编号说明。
- 本机状态：服务 STOPPED，`C:\yuanguard_hv.sys` = 稳定默认版
  `70888311...`；hook 路径实验已停（9.125），非 hook 路径回归已 PASS
  （9.126/9.128）。

### 9.128 2026-08-13 最新默认版非 hook 路径回归（PASS）

- 基线：HEAD `02131a1`（9.127），工作区干净；`C:\yuanguard_hv.sys`
  稳定默认版 `70888311...`，服务 STOPPED。
- 构建最新默认版（无门控，SHA256
  `7B19EF022B8944BB3055AF91D2C14FBA21D25145D4A2441F5862F386BEDDB5A0`），
  复制到 C 盘，`sc start` RUNNING。
- 只读查询 PASS：`state active=1 pid=4 page_count=2`；`target`（旧 API）
  显示全局 hook_count=1；`list-targets` 每槽 hooks=0（hooks 全局表，
  该差异为当前设计语义，非回归）；`list-pages` 2 页 armed=1；
  `list-hooks` id=0 installed=1 patch_len=12；`config` 默认
  `auto_disarm=1 deny_status=0xC0000022`。
- config 回归 PASS：`auto-disarm 0` 在保护 active 时按设计返回
  Access Denied（strict deny + active 拒设）；先 `stop` 后
  `auto-disarm 0→1`、`deny-status 0xC0000005→0xC0000022` 设置/恢复，
  最终恢复默认；`start` 后 2 页重新 armed。
- `selftest` PASS：pid=2632，baseline page_count=2，set-target/add-page/
  list-pages/start/user write-read/remove 全部通过；`exit-test` PASS：
  child pid=6988，退出后槽自动清理。
- 服务脚本 PASS：`set-auto-start`/`unset-auto-start` 注册表 Start 2↔3；
  `sc stop/start` 后 `state active=1 page_count=2`，默认保护恢复。
- 收尾：`sc stop yuanguard`，`C:\yuanguard_hv.sys` 恢复稳定默认版
  `70888311...`，服务 STOPPED；本轮未执行 install-hook/remove-hook/
  clear，无冻结。
- 下一步不变：hook 路径（锁序/ERROR_BUSY/真实多目标语义）换平台或
  硬件调试器；loader_stealth 的 kd `!driver` 复核待内核调试会话；
  R1/NPT 权限收紧待裸机/KVM。

### 9.129 2026-08-13 hook 静态分析与新窗口提示同步（只读）

- 用户确认执行：同步 `docs/YUANMOD_NEXT_WINDOW_PROMPT.md` 到最新状态；
  对 hook 锁序与 0x5AA 做只读静态分析，输出不改机修复方案。
- 0x5AA 更正：实测 `RtlNtStatusToDosError(0xC000009A)` 返回 0x5AA，
  Win32 文本为 `ERROR_NO_SYSTEM_RESOURCES`（1450），不是
  `ERROR_BUSY`（170）；全仓库无 `STATUS_DEVICE_BUSY`/直接 0x5AA
  返回点。
- 候选来源：`protect.c:249` 目标表满（install 自动 set_target 时最
  常见）、`protect.c:1071` hook stub 池分配失败、`npt_core.c:91`
  split 页表分配失败、`control_device.c:82` CreateFile 上下文分配
  失败、`protect.c:314` 目标页表满。
- 死锁：install/remove 持 `g_protect_lock` 内 pause；NPF 写路径取同一
  锁，AB-BA 窗口与 5 秒超时/硬冻结一致；clear 持锁逐个 remove_hook，
  pause 失败不清 `h->installed`，导致 hook_count 残留 1。
- 修复方案（仅文档，不改驱动）：patch 互斥 -> 资源准备 -> pause ->
  取锁修改 -> resume；clear 单次 pause 后无条件清 hook 槽；预分配
  hook stub、明确目标表满语义、扩展 yghv_hook_diag 阶段。
- 新增
  `docs/YGHV_HOOK_LOCK_AND_0x5AA_REDESIGN_20260813.md`；同步
  `docs/YUANMOD_NEXT_WINDOW_PROMPT.md`（HEAD 0268a81、9.128 状态、
  9.129 更新版）。
- 收尾：未加载驱动、未跑 hook 路径、无代码改动；提交文档。

### 9.130 2026-08-13 Java/JNI 非 hook 产品化：unprotect/scan（PASS）

- 用户确认执行：给 Java 客户端补 `unprotect` 与 `scan`，全程不碰
  install/remove-hook/clear，不改驱动与 JNI 逻辑。
- 代码改动：`YghvCtl.java` 新增 `unprotect`（stop + 当前 JVM
  set-target 清页，不触碰 hooks）与 `scan <pid> [maxPages]`（复用现有
  `enumeratePages`，只打印已提交页候选）；README 同步用法。
- 构建：`cmd /c build.bat` SUCCESS；JNI DLL 与 .class 为忽略产物，
  本轮保留未清理。
- 实机回归（最新默认版 `7B19EF022B8944BB3055AF91D2C14FBA21D25145D4A2441F5862F386BEDDB5A0`）：
  Java `state` active=1 pid=4；`list-targets` returned=1（System 槽）；
  `list-pages` returned=2；`list-hooks` returned=1（id=0 installed）；
  `config` 默认 `auto_disarm=1 deny_status=0xC0000022`。
- `scan` PASS：pid=10480，cap=8，enumerated=64，仅打印不保护；
  `unprotect` PASS：set-target pid=2588 OK、state active=0、unprotect
  OK；Java 进程退出后自身槽自动清为 pid=0/cr3=0，hooks 未变化。
- 收尾：`sc stop yuanguard`，`C:\yuanguard_hv.sys` 恢复稳定默认版
  `70888311...`，服务 STOPPED；本轮无 hook 路径、无冻结。
- 下一步不变：hook 路径换平台/kd；R1 与 loader_stealth 复核分别待
  裸机/KVM 与内核调试会话。

### 9.131 2026-08-13 收尾整理：提示词/TASKS 同步 + 产物清理

- 用户确认执行：同步 `docs/YUANMOD_NEXT_WINDOW_PROMPT.md` 到 HEAD
  `a5f354e`/9.130；更新 `docs/TASKS.md` 中过期的“Java 层未开始”
  条目；清理本轮生成的忽略产物。
- 清理：`YuanGuardHV/bin` 与 Java/JNI 构建产物（`.class`、
  DLL/lib/exp）已删除，`git status` 保持干净。
- 文档：NEXT_WINDOW_PROMPT 改为 9.131 更新版；TASKS 将真实目标接入与
  Java/JNI 客户端标记完成，并把剩余项收窄为 R1、MSR/IO/整机级隐形、
  真实系统 hook。
- 收尾：服务 STOPPED、C 盘稳定默认版 `70888311...` 不变；提交文档。

### 9.132 2026-08-13 文档清理与任务清单勾选（用户批准）

- 用户批准后执行文档清理：删除 `PLAN.md`、`YuanGuardHV/HANDOFF.md`、
  `YuanGuardHV/TECHNICAL_REVIEW.md`、`docs/NEXT_WINDOW_PROMPT.md`、
  `docs/SESSION_20260811.md`、`docs/reference/reference_design.md`、
  `docs/reference/reference_tasks.md` 与 4 个 OS-as-guest 计划文档。
- 保留：`docs/YUANMOD_HANDOFF_CURRENT.md`、
  `docs/YUANMOD_NEXT_WINDOW_PROMPT.md`、`docs/TASKS.md`、
  `docs/YGHV_HOOK_LOCK_AND_0x5AA_REDESIGN_20260813.md`、Phase 3
  计划/规范与 `reference/` 参考代码。
- `TASKS.md` 更新：YGHV-007/008 标完成；YGHV-006/009/010 保持原状态；
  Phase 2d 标完成；代码快照更新为 2026-08-13；火绒冲突与串口管道假设
  按已有记录勾选；v33-v42 拆为“已完成/剩余”。
- 交接文档顶部快照与 `YUANMOD_NEXT_WINDOW_PROMPT.md` 同步到清理后的
  文档结构。
- 收尾：`git rm` 删除并提交；工作区保持干净。

### 9.133 2026-08-13 tests/ 静态校验脚本（PASS）

- 用户确认执行：建 `YuanGuardHV/tests/` 目录，做接口编号、命令清单与
  安全红线静态校验，不加载驱动、不碰 hook 实机路径。
- 新增：`tests/README.md`、`ioctl_parity.ps1`、`command_parity.ps1`、
  `safety_checks.ps1`、`run_static_checks.ps1`。
- 同步 `tools/yghv_client/README.md` 为完整命令清单（Java 18 个、
  PowerShell 20 个）。
- 运行结果全部 PASS：IOCTL parity C=15/PowerShell=15/Java=15；Command
  parity PowerShell=20/Java=18；Safety checks PASS（pause-under-lock
  限定已知两函数、无 `STATUS_DEVICE_BUSY`、0x5AA 命名正确、README
  包含 unprotect/scan）。
- 收尾：未加载驱动、未跑 hook 路径；提交。

### 9.134 2026-08-13 build.bat 接入静态校验（PASS）

- 用户确认执行：`YuanGuardHV/build.bat` 编译前调用
  `tests\run_static_checks.ps1`，校验失败即中止构建。
- 验证：`cmd /c build.bat` 输出先为静态校验 PASS，再编译/链接/签名
  SUCCESS；新构建 SHA256
  `50FC9809D69528F9135D7F0B5653A24CD9AB98CBC0CE9DFEA4D9C390FDB80555`。
- 同步 `docs/YUANMOD_NEXT_WINDOW_PROMPT.md` 到 HEAD `d4079bc`/9.133。
- 收尾：未加载驱动、未跑 hook 路径；`YuanGuardHV/bin` 为构建验证
  产物（忽略文件，未清理）；提交。

### 9.135 2026-08-13 全链路隐藏审计与痕迹自查（只读）

- 目标确认：以“抗人工取证”优先，ACE 类反作弊尽力而为但不保证；
  合规说明已写入审计文档。
- 新增 `docs/YGHV_STEALTH_AUDIT_20260813.md`：六层隐藏矩阵（加载/
  运行/处理器/交互/反卸载/自查）、当前可见痕迹清单与阶段路线。
- 新增 `YuanGuardHV/tools/yghv_stealth_check.ps1`：只读检查服务、
  内核驱动、注册表、文件、日志与控制设备，不加载驱动、不写日志。
- 本机实测：可见 5 项（kernel driver、注册表服务键、
  `C:\yuanguard_hv.sys`、`yghv_progress.log`、`yghv_ioctl.log`）；
  服务停止时设备对象与 `yghv_hook.log` 不可见。
- 收尾：未加载驱动、未改任何隐藏门控；提交文档与自查脚本。

### 9.136 2026-08-14 全面代码审查 + 任务修复清单（只读）

- 背景：接管后首次完整全面审查。只读，未改任何代码，未实机加载驱动
  （本机硬冻结史，见 9.118/9.125）。
- 方法：本人通读全部正式构建源码（main.c 4090 行 / protect.c 1298 行 /
  svm_core.c / svm_trampoline.S / vmexit.c / npt_core.c / vmmcall.c /
  control_device.c / multi_core.c / loader_stealth.c + 全部 common 头文件）；
  5 个模块子代理（SVM 核心 / NPT+vmexit / 控制面 / 客户端+测试 / protect 层）
  并行深审，报告逐条对照源码核实；`cmd /c build.bat` 构建验证 SUCCESS
  （SHA256 `F411C929C1114773BEBB6B7C541B82607AA3CB0A5FB051D7C733200C3A0CC106`）。
- 完整报告：`docs/YGHV_FULL_REVIEW_20260814.md`（验证正确清单 + 问题清单 +
  任务修复清单，问题 ID `YGHV-REV-001..043`）。
- 结论：默认构建路径无 CRITICAL；关键机制（VMCB 布局、trampoline、NPT 数学、
  IOCTL 面、认证分层、结构体 ABI）验证正确。
- 发现汇总：**9 MAJOR / 21 MINOR / 13 INFO-NIT**。MAJOR：
  - REV-001 持 `g_protect_lock` 跨全核 pause 死锁（已知，9.118/9.125，换平台）；
  - REV-002 `npt_set_page_perm_range` 2MB 步进语义错误；
  - REV-003 `stop_all_residents` 不唤醒暂停 resident（卸载死锁，一行修复）；
  - REV-004 `get_pages_info` `returned` 超上限 → 客户端越界读；
  - REV-005 `yghv_trace_u64` 栈缓冲区溢出（潜在，当前不可达）；
  - REV-006 `check_target_exited` EPROCESS 裸指针 UAF 竞态（多核 + 目标退出窗口）；
  - REV-036 `yghv_hook_diag` 在 FAST_MUTEX 锁内（APC_LEVEL）调 `ZwCreateFile` IRQL 违规；
  - REV-037 x86-64 指令解码器误解码（B8-BF imm64、0F 38/3A 差一、F6/F7 立即数）→ 错误补丁边界；
  - REV-038 `remove_hook` pause 超时后不恢复原始字节 → hook 永久残留 + stub 泄漏。
- 独立复核修正 2 处子代理误报：exit-test 不可用（漏看 main.c:4075
  `g_persistent_mode=TRUE`，默认构建实际 PASS）；INTR 拦截"惰性"（与 step11
  实测 15 次 INTR 退出矛盾，留 REV-035 待 kd）。
- 新发现文档/工具间隙：交接文档本文件 line 1886 仍残留 `` `ERROR_BUSY (0x5AA)` ``
  过时标注，且 `tests/safety_checks.ps1` 检查清单未含本文件（漏检）；
  C 盘稳定默认版 `70888311...`（9.112 构建）与 HEAD 源码构建 `F411C929...` 不同源。
- 下一步：任务清单见审查报告第 4 节（P0 低风险项可本机验证，P3 锁序/hook 路径
  需换平台/kd）；本机继续停止 hook 路径实验。

### 9.137 2026-08-14 P0 低风险修复 4 项（REV-003/004/005/006，已确认实施）

- 用户确认按审查报告 P0 方案实施，仅改 5 个文件，未动 hook 路径：
  - **REV-003** `hv/svm_core.c` `svm_core_stop_all_residents`：对 `pause_requested`
    vcpu 补 `KeSetEvent(resume_event)`，避免卸载时 `wait_all_stopped` 死锁。
  - **REV-004** `hv/protect.c` `get_pages_info`：`returned` 改为实际写入条数 `n`
    （原跨目标累加可到 256 超 64 项缓冲，客户端越界读）；`tools/yghv_ctl.ps1`
    `Read-YghvPages` 与 `tools/yghv_client/YghvCtl.java` `listPages` 循环上限
    收敛 `min(returned,64)`（防御旧驱动）。
  - **REV-005** `hv/main.c` `yghv_trace_u64`：label 拷贝上限收紧为
    `sizeof(buf)-20`（固定后缀 `=`+`0x`+16hex+NUL=20B），杜绝栈溢出（潜在）。
  - **REV-006** `hv/protect.c` `check_target_exited`：锁内对捕获 EPROCESS
    `ObReferenceObject`，等待后 `ObDereferenceObject`；进程已退出且调
    `on_target_exit` 前锁内复核槽的 pid+process 仍匹配（防 pid 复用误清新目标）。
- 构建：`cmd /c build.bat` → 静态校验 PASS + 编译/链接/签名 SUCCESS，
  新 SHA256 `57fed86a03aa5af1c48be5b72449382dcf5dc893880afd70681ead3ef2ce531d`
  （归档 `D:\aaaaaavm\yuanguard_hv_p0fix_20260814.sys` 待复制）。
- 验证：尚未实机加载（本机加载需用户许可）；建议按 9.128 非 hook 回归清单
  （sc start → state/list-pages/selftest/exit-test → sc stop）。
- 提交：本轮代码（svm_core.c、protect.c、main.c、yghv_ctl.ps1、YghvCtl.java）
  + 审查文档勾选 + 本记录。

### 9.138 2026-08-14 P0 修复版实机回归（用户授权，全部 PASS）

- 部署：备份 C 盘稳定默认版（`70888311...`）到
  `D:\aaaaaavm\yuanguard_hv_c_drive_backup_20260814.sys`，覆盖为 P0 修复版
  `57FED86A...`，`sc start yuanguard` → RUNNING。
- 驱动自测（`yghv_progress.log`）：NPT/protect 测试、hook test PASS、
  hook resident allow/deny（deny=0xC0000022）、hook boundary PASS、
  双核心跳、all stopped，全部正常。
- 只读查询：`state`（active=1 pid=4 page_count=2）、`target`、
  `list-targets`（returned=1）、`list-pages`（returned=2 均 armed=1，
  同时验证 REV-004 修复）、`list-hooks`（id=0 installed patch_len=12）、
  `config`（auto_disarm=1 deny_status=0xC0000022）全 PASS。
- 功能回归：`selftest` PASS；`exit-test` PASS（子进程槽自动清理，覆盖
  REV-006 心跳判活路径）；config 回归 PASS（active 下 `auto-disarm 0`
  返回 Access Denied 0x5 不冻结、stop 后设置/恢复、deny-status 设置/恢复、
  `start` 重武装 2 页）。
- **REV-003 验证**：`sc stop` 干净回 STOPPED（WIN32_EXIT_CODE 0），
  卸载路径 stop_all_residents→wait_all_stopped 无死锁。
- 全程无冻结/蓝屏，机器响应正常；未执行 hook 路径（install/remove/clear）。
- 收尾：C 盘已恢复稳定默认版 `70888311...`，服务 STOPPED。
- 结论：P0 修复 4 项（REV-003/004/005/006）实机回归全部 PASS，无回归。
  提交（可选）：本轮回归记录。

### 9.139 2026-08-14 P1 语义修复 11 项 + REV-039（用户授权直接修复）

- 用户指示"完成 P1 直接修复，不用给代码"。共改 4 个驱动文件，构建 + 实机非
  hook 回归全部 PASS。
- **REV-002** `npt_core.c` `npt_set_page_perm_range` 改 4K 迭代（每页先 split 再
  set_perm），修正 2MB 步进对拆分区域漏改/大页过度应用。
- **REV-010** `npt_core.c` `npt_init` pml4 分配失败 `g_cache=NULL`；`main.c`
  npt_init/map_ram 失败路径补 `npt_cleanup(&g_npt)`（修泄漏/悬垂）。
- **REV-011** `npt_core.c` `npt_translate` 先查 present 再查 large_page。
- **REV-013** `vmmcall.c` auth CR3 比较统一 `~0xFFF` 掩码（与 stub 匹配一致）。
- **REV-015** `vmmcall.c` ADD_PAGE/REMOVE_PAGE 改按调用者 CR3 解析目标槽
  （`add_page_for`/`remove_page_for`），与 IOCTL 一致。
- **REV-036** `protect.c` `yghv_hook_diag` IRQL 违规：锁内改 `yghv_hook_diag_mark`
  （内存+LOG_ERROR），公共包装（install/remove/clear/cleanup）放锁后
  `yghv_hook_diag_flush` 落盘。
- **REV-037** `protect.c` 指令解码器 3 类修正：rex 存字节值（W 位）、B8-BF 按
  REX.W 分 imm32/imm64、0F 38/3A modrm 从 op3 后解码、F6/F7 仅 /0 有立即数；
  `main.c` `hook_boundary_test` 增补 5 组解码器用例（B8 imm32 / movabs / 0F38 /
  F6 / F7）。
- **REV-040** `protect.c` install 失败路径 `npt_set_page_perm` 后补全核
  `npt_flush_pending`。
- **REV-041** `protect.c` install 失败路径 re-pause 后再恢复字节 + 释放 stub
  （消除撕裂指令与 stub UAF 窗口）。
- **REV-044** `protect.c` `set_target` disarm 失败时 snapshot+rollback 恢复槽原
  armed 状态。
- **REV-045** `protect.c` `on_npf_write` 决策前检查 `g_protect.active`（非 active
  不处理写，避免失败 stop 后一次写永久 disarm）。
- **REV-039**（P0 清单补）`protect.c` install/remove `st` 声明即初始化
  `STATUS_UNSUCCESSFUL`，修未初始化传 diag。
- 构建：首轮因注释 `Zw*/Nt*` 含 `*/` 提前闭合导致编译错误，修正注释后 SUCCESS；
  另修 `buf[n++]=stage[n]` 未定序警告。最终
  SHA256 `fb1522651546870ab46740a965cf6abfa6a50a980a90b7ca70f12cb35be1dab4`
  （含 REV-039；归档 `D:\aaaaaavm\yuanguard_hv_p1fix_20260814.sys` 为前一轮
  `85b763e...`，本轮最终件见 bin/）。
- 实机回归（85b763e，REV-039 仅 hook 错误路径变量初始化、不影响非 hook 结果）：
  部署 → RUNNING；progress log 自测全过（**hook boundary PASS 含新解码器用例**，
  验证 REV-037）；state/target/list-targets/list-pages/list-hooks/config 全 PASS；
  selftest PASS；exit-test PASS；`sc stop` 干净 STOPPED；C 盘恢复稳定默认版
  `70888311...`。（list-pages 首页 armed=0 为常驻 guest 每轮写 workload 页的
  auto-disarm/rearm 时序快照，非回归；未执行 hook 路径。）
- 提交：4 驱动文件（npt_core.c / main.c / vmmcall.c / protect.c）+ 审查文档 P0
  补勾 REV-039 + P1 全部勾选 + 本记录。
