# 下一窗口提示词（2026-08-11 会话结束后粘贴给新的 Codex 窗口）

```text
你接手 YuanGuardHV（AMD-V SVM/NPT 隐形 Hypervisor）项目，仓库根 D:\yuanguard，当前分支 main。

【协作铁律（最高优先级）】
1. 任何代码/资源/文档改动前，先向用户说明并获得明确确认；未确认不得改。
2. 只修用户反馈的问题，不擅自扩大范围。
3. 每次修改与决策必须记录到 docs/YUANMOD_HANDOFF_CURRENT.md。
4. 禁止回滚用户/历史改动；工作区如有未提交改动，先查 git status 再动手。

【当前进度快照（2026-08-11，v42）】
- Phase 2a/2b/2c 完成；Phase 3 保护一版 v27-v31 完成并合入 main。
- 后续 v32-v42 已合入 main：
  - v32：IOCTL 控制设备配置通道（SET_TARGET/ADD_PAGE/START/STOP/GET_STATE），外部下发可用。
  - v33：常驻模式接入真实受保护页 + 驱动内 dummy hook，stub 在 resident guest 中 allow/deny 验证。
  - v34：NPT 共享状态 FAST_MUTEX 加锁 + NPF/rearm 定点化。
  - v37：目标进程生命周期（KeWaitForSingleObject 轮询判活 + 退出自动 disarm + PID 复用防护）。
  - v38：hook 加固（页/指令边界校验 + 跨核 rendezvous）。
  - v39：控制面安全（IOCTL 句柄 CR3 绑定 + VMMCALL CPL/CR3）。
  - v40/v41：隐形基础 CPUID 矩阵（0x40000000 段清 0、leaf1 hypervisor bit 清、SVM bit 清、0x8000000A 清 0）。
  - v42：MSR/IO 隐身尝试后回退（VMware 嵌套不放行 L1 MSRPM/IOPM 拦截，留裸机/KVM）；仓库整理完成（参考文档/lib 迁移、logs_archive、.gitignore）。
- 关键定档：常驻模式仍是“合成 resident guest 测试基架”，整个 OS 尚未纳入 guest；真实系统 hook、真实用户页 NPF 拦截、整机级隐形均未接入。

【关键文件】
- 构建：YuanGuardHV\build.bat（clang-cl + MSVC link + signtool，输出 bin\yuanguard_hv.sys）
- 核心代码：YuanGuardHV\hv\（main.c、protect.c、vmexit.c、vmmcall.c、svm_core.c、multi_core.c、npt_core.c、control_device.c、svm_trampoline.S、common\）
- 客户端：YuanGuardHV\tools\yghv_ctl.ps1（state/set-target/add-page/remove-page/start/stop/selftest/exit-test）
- 记录：docs\YUANMOD_HANDOFF_CURRENT.md、docs\TASKS.md、docs\SESSION_20260811.md、本文件

【环境与调试命令】
- VMX：C:\Users\Administrator\Documents\Virtual Machines\Windows 10 x64\Windows 10 x64.vmx（VMware 17.6.4，官方 Win10 19045.2965）
- KD 串口管道：\\.\pipe\yuanhv_debug；控制端 YuanGuardHV\kd_ctl.ps1（读 kd_cmd.txt，日志 kd_ctl.log）；向 kd_cmd.txt 写命令，连接早期启动，`g` 放行
- 构建并复制：cd YuanGuardHV; cmd /c build.bat; Copy-Item bin\yuanguard_hv.sys D:\aaaaaavm\yuanguard_hv_vNN.sys（共享目录文件被 VM 占用时先改名 .locked）
- VM 内加载：copy "\\vmware-host\Shared Folders\aaaaaavm\yuanguard_hv_vNN.sys" C:\yuanguard_hv.sys; sc.exe start yuanguard
- VM 内配置/回归：powershell -ExecutionPolicy Bypass -File "\\vmware-host\Shared Folders\aaaaaavm\yghv_ctl.ps1" selftest / exit-test
- VM 内卸载：powershell -ExecutionPolicy Bypass -File "\\vmware-host\Shared Folders\aaaaaavm\unload_driver.ps1"
- VM 卡死/蓝屏恢复：vmrun stop <vmx> hard; vmrun start <vmx> gui; 重启 kd_ctl.ps1；再写 g

【已知限制与雷区】
- VMware 嵌套 SVM：不能剔除 NPT 私有页/自页；不能写 CR0（#UD）；MSRPM/IOPM 拦截不生效（MSR/IO 隐身留裸机/KVM）。
- 非 resident 核上不要给真实系统函数装 hook（stub vmmcall 会 #UD），测试用驱动内 dummy。
- VM ntoskrnl 19045.2965 只导出 ZwTerminateProcess（无 NtTerminateProcess）；PsSetCreateProcessNotifyRoutine/Ex 在测试签名驱动下返回 0xC000007A 且会残留回调（曾两次 0xCE），不要再用该 API。
- KD 控制端 kd_cmd.txt 有文件占用竞态风险；写命令后等待，不要并发写。

【下一步（按优先级）】
1. 文档同步已完成（本文件/SESSION/PLAN 已更新到 v42）；后续窗口先读交接文档。
2. 实机（裸机/KVM）验证：R1 私有页剔除、NPT 自剔除、默认 NX、MSR/IO 隐身、Phase 2d；需确认 testsigning + 稳定物理机。
3. Java 层客户端（基于 IOCTL）与真实目标（Minecraft/Forge）接入。
4. 整机级隐形立项（OS 纳入 guest）。
5. tests/mod/vm 目录补齐与遗留 test_*.c 归档。

先读 docs\YUANMOD_HANDOFF_CURRENT.md 与 docs\TASKS.md 确认进度，再和用户确认下一步后动手。
```
