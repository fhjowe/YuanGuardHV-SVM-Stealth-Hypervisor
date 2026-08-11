# 下一窗口提示词（2026-08-11 会话结束后粘贴给新的 Codex 窗口）

```text
你接手 YuanGuardHV（AMD-V SVM/NPT 隐形 Hypervisor）项目，仓库根 D:\yuanguard，当前分支 main。

【协作铁律（最高优先级）】
1. 任何代码/资源/文档改动前，先向用户说明并获得明确确认；未确认不得改。
2. 只修用户反馈的问题，不擅自扩大范围。
3. 每次修改与决策必须记录到 docs/YUANMOD_HANDOFF_CURRENT.md。
4. 禁止回滚用户/历史改动；工作区如有未提交改动，先查 git status 再动手。

【当前进度快照（2026-08-11）】
- Phase 2a/2b/2c 完成：SVM init + VMRUN、16GB NPT identity map + NPF、多核心跳。
- R1 安全地基部分完成：NPT translate/perm-range/split 单测（v25）、NPF 注入（v26）、TLB 刷新与 rearm 全页重锁（v31）。
- Phase 3 保护一版完成并已合并 main（快进到 bf67ebc）：内存页写保护（v27）、终止保护（v28）、句柄保护（v29）、全核常驻模式（v30/v31）。
- v31 已 VM 验证：real write #2 PASS（TLB 刷新生效）、hook 0/1 PASS、常驻心跳 621 万次、unload 干净。
- 重要定档：当前常驻模式是“空转测试基架”（0 个受保护页、无真实 hook）；真实保护能力尚未接入。

【关键文件】
- 构建：YuanGuardHV\build.bat（clang-cl + MSVC link + signtool，输出 bin\yuanguard_hv.sys）
- 核心代码：YuanGuardHV\hv\（main.c、protect.c、vmexit.c、vmmcall.c、svm_core.c、multi_core.c、npt_core.c、svm_trampoline.S、common\）
- 设计/计划/记录：docs\superpowers\specs\2026-08-11-phase3-protection-design.md、docs\superpowers\plans\2026-08-11-phase3-protection.md、docs\YUANMOD_HANDOFF_CURRENT.md、docs\TASKS.md、docs\SESSION_20260811.md
- 本文件：docs\NEXT_WINDOW_PROMPT.md

【环境与调试命令】
- VMX：C:\Users\Administrator\Documents\Virtual Machines\Windows 10 x64\Windows 10 x64.vmx（VMware 17.6.4，官方 Win10 19045.2965）
- KD 串口管道：\\.\pipe\yuanhv_debug；控制端 YuanGuardHV\kd_ctl.ps1（读 kd_cmd.txt，日志 kd_ctl.log）；向 kd_cmd.txt 写命令，连接早期启动，`g` 放行
- 构建并复制：cd YuanGuardHV; cmd /c build.bat; Copy-Item bin\yuanguard_hv.sys D:\aaaaaavm\yuanguard_hv_vNN.sys
- VM 内加载：copy "\\vmware-host\Shared Folders\aaaaaavm\yuanguard_hv_vNN.sys" C:\yuanguard_hv.sys; sc.exe start yuanguard
- VM 内卸载：powershell -ExecutionPolicy Bypass -File "\\vmware-host\Shared Folders\aaaaaavm\unload_driver.ps1"
- VM 卡死恢复：vmrun stop <vmx> hard; vmrun start <vmx> gui; 早期启动 kd_ctl.ps1；再写 g

【已知限制与雷区】
- VMware 嵌套 SVM：不能剔除 NPT 私有页/自页；不能写 CR0（#UD）；这些留给裸机/KVM。
- 非 resident 核上不要给真实系统函数装 hook（stub vmmcall 会 #UD），测试用驱动内 dummy。
- 该版本 ntoskrnl 只导出 ZwTerminateProcess（无 NtTerminateProcess 导出）；NtOpenProcess 正常导出。
- KD 控制端 kd_cmd.txt 有文件占用竞态风险；写命令后等待，不要并发写。

【下一步（按优先级）】
1. 真实目标进程接入：IOCTL/设备对象配置通道（或直接 Java 层），SET_TARGET/ADD_PAGE/START_PROTECT 从外部下发。
2. 常驻模式接入真实受保护页与真实 hook，并让 stub 在 resident guest 中实际执行（allow/deny 路径）。
3. NPT 共享状态加锁（g_protect/g_protect_hooks 跨 VCPU 保护）。
4. 目标进程生命周期：EPROCESS 校验、退出自动 disarm、PID 复用防护。
5. hook 加固：页边界/指令边界校验、跨核 rendezvous。
6. 控制面安全：管理命令限制调用方 CPL/CR3。
7. R1 剩余：私有页剔除、NPT 自剔除、默认 NX（需裸机/KVM）。

先读 docs\YUANMOD_HANDOFF_CURRENT.md 与 docs\TASKS.md 确认进度，再和用户确认下一步后动手。
```
