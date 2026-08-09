# YuanMod / YuanGuardHV 当前交接与决策记录

> 接管人: Codex (/root) | 日期: 2026-08-09 | 仓库根: D:\yuanguard
> 本文件按协作铁律第 3 条维护，是项目所有修改与决策的记录，优先级高于普通文档。

## 1. 项目一句话

AMD-V SVM/NPT 隐形 Hypervisor（YuanGuardHV），替代原 YuanGuard 内核驱动，把 Minecraft/Forge 进程保护逻辑放到虚拟化层。

## 2. 当前进度快照（2026-08-09 实测）

| 里程碑 | 状态 |
|---|---|
| Phase 1 骨架 + 核心头文件 | 完成 |
| Phase 2a SVM init + VMRUN 单核 | 代码完成，未在可运行 SVM 的环境验证 |
| Phase 2b NPT identity-map + NPF | 代码完成，未验证 |
| Phase 2c 多核 DPC | 代码存在，默认未启用 |
| Phase 2d 物理机验证 | 未完成 |
| Phase 3+ 隐形/保护/Java 层 | 未开始 |
| git 版本控制 | 本次初始化完成 |
| 构建基线 | 成功，见第 5 节 |

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
| VMX | `D:\vmware\Windows 11 x64.vmx` |
| VM 配置 | 4 vCPU / 8GB / EFI / `vhv.enable=TRUE` |
| 调试管道 | `\\.\pipe\yuanhv_debug`（serial0, server 端） |
| KD | `C:\Program Files (x86)\Windows Kits\10\Debuggers\x64\kd.exe` |
| 连接脚本 | `YuanGuardHV\run_kd.bat` / `start_kd.ps1` |
| VM 状态 | 勘查时未运行 |

### 3.3 已知环境限制

- VMware `vhv.enable=TRUE` 的项目记录是：L1 Guest 中执行 `clgi` 触发 `#UD`，即 VMware 不把 AMD SVM 指令暴露给 Guest。需在 VM 内用 CPUID/CLGI 实测再确认；若属实，VMRUN 冒烟只能走裸机或 KVM 嵌套。
- 宿主 `bcdedit` 在当前非管理员 shell 不可用；testsigning / nointegritychecks 状态需在裸机或 VM 内确认。
- 宿主 `C:\Windows\Minidump` 有 7/30 的 4 个 dump，其中 `usbwifi.sys` 蓝屏与项目无关。

## 4. 初始化动作

- [x] 2026-08-09 只读勘查仓库与环境（工具链 / VM / KD / 代码状态）
- [x] 2026-08-09 用户确认初始化方案（docs 记录 + git + 构建基线 + 任务清单）
- [x] `git init` + `.gitignore` + 首次提交
- [x] 修复 build.bat（用户确认后完成）
- [x] `build.bat` 构建基线：成功，见第 5 节
- [ ] VM 加载链验证（min_drv）
- [ ] VM 内 CPUID/CLGI SVM 暴露实测
- [ ] 重新核查 TECHNICAL_REVIEW P0 是否仍适用
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

## 7. 变更日志

| 日期 | 文件 | 变更 | 验证 |
|---|---|---|---|
| 2026-08-09 | `.gitignore` | 忽略 bin/obj/log/dump 等 | git 状态检查 |
| 2026-08-09 | `docs/YUANMOD_HANDOFF_CURRENT.md` | 创建本记录 | - |
| 2026-08-09 | `docs/TASKS.md` | 创建任务清单 | - |
| 2026-08-09 | 仓库 | git init + 首次提交 | git log |
| 2026-08-09 | `YuanGuardHV/build.bat` | for 块改 `call :compile` 子程序；INCLUDES/LIBPATH 路径加引号 | 构建成功并签名 |
| 2026-08-09 | `YuanGuardHV/svm_trampoline.asm` | 移除 clang 生成的中间汇编并加入 .gitignore | git 状态干净 |
