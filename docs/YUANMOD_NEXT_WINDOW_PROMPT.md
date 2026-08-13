# YuanGuardHV 新窗口提示词（2026-08-13 9.131 更新版）

你接手 YuanGuardHV（AMD-V SVM/NPT 隐形 Hypervisor）项目，仓库根
D:\yuanguard，分支 main，HEAD a5f354e（文档已收尾到 9.130，9.131 为本
文件对应的收尾记录）。

【协作铁律（最高优先级）】
1. 任何代码/资源/文档改动前，先向用户说明想法并取得明确确认；未确认
   不得改。
2. 只修用户反馈的问题，不擅自扩大范围。
3. 每次修改与决策必须记录到 docs/YUANMOD_HANDOFF_CURRENT.md。
4. 禁止回滚用户/历史改动；工作区如有未提交改动，先查 git status 再动手。

【当前状态】
- 机器：Windows 10 19045，Ryzen 5 5500，>16GB。服务 yuanguard 已
  STOPPED；C:\yuanguard_hv.sys = 稳定默认版
  （SHA256 70888311B38EF8D386252271CAF8EC8ADE8D96FAA472C7260E30692B95F92E3B，
  归档 D:\aaaaaavm\yuanguard_hv_default_cpcr_20260813.sys）。
- 工作区干净（HEAD a5f354e）；构建产物已清理（YuanGuardHV/bin、
  Java/JNI 的 .class/DLL/lib/exp 等均已删除）；logs_archive/、kd
  运行文件、.superpowers/ 均已清理；.gitignore 已覆盖。
- 最近提交：a5f354e feat: Java client unprotect/scan + docs 9.130；
  1587a4d docs 9.129；0268a81 docs 9.128；02131a1 docs 9.127；
  f833ab1 docs 9.126；772de94 docs 9.125；691e2c5 2B-3-b 多目标；
  6af1d8b 2B-3-a；1cecd2f 2C-3；1591a55 2C；d5631ef 2B-2；be87130
  2B-1；8dac7e9 MDL+真实 hook；b9504ef 9.113 收尾。

【关键结论（务必先读 9.84-9.130）】
1. OS-as-guest 常驻线已停线：guest 内 Windows 调度器上下文切换触发
   平台级整机停机；唯一 PASS 是 step20 自旋+INTR/NMI 拦截+宿主 ISR。
   非驻留保护路线是产品主线。
2. R1 私有页剔除门控关闭（YGHV_R1_EXCLUDE_PRIVATE=0）；R1 deny 机器级
   停机保持门控。
3. P0 已完成：VMMCALL 认证分层（per-vcpu auth_key）、控制面 SeDebug 提权
   校验、VMMCALL SET_TARGET 自设目标、ADD/REMOVE_PAGE 目标进程绑定。
4. 真实 hook：MDL 可写映射修掉 0x50；可变长度 12-16 字节补丁；原生 CR3
   跳板（真实进程不能用 VMMCALL 决策）；控制面 INSTALL/REMOVE_HOOK 可用。
5. 多目标已完成（2B-3）：targets[4] 槽、NPF/决策跨槽、hook stub unrolled
   遍历 4 槽 CR3、GET_TARGETS 0x80E、list-targets。
6. 2C 已完成：loader_stealth 接线（YGHV_LOADER_STEALTH 门控默认 0）、
   服务持久化/防卸载（set/unset-auto-start、harden/unharden-service 用
   sc sdset）、内核侧防卸载（YGHV_UNLOAD_GUARD 门控默认 0）。
7. 9.128 非 hook 路径回归 PASS（查询/配置/selftest/exit-test/服务脚本；
   构建哈希 7B19EF02...）。
8. 9.129 静态分析：0x5AA 实为 ERROR_NO_SYSTEM_RESOURCES，是
   STATUS_INSUFFICIENT_RESOURCES 的 Win32 映射，不是 ERROR_BUSY；
   锁序死锁与 0x5AA 修复方案见
   docs/YGHV_HOOK_LOCK_AND_0x5AA_REDESIGN_20260813.md。
9. 9.130 Java/JNI 非 hook 产品化：`unprotect`/`scan` 已加入并实机
   PASS。
10. 9.131 收尾：构建与 Java/JNI 产物已清理，提示词与 TASKS 已同步。

【本机雷区（重要）】
- install/remove/clear 类 hook 路径实机验证会触发整机硬冻结：remove/
  clear 持 g_protect_lock 内全核心 pause，与 NPF handler 存在死锁
  （9.118/9.125 多次无 dump 硬冻结）。本机已停止该族实验，留待换平台
  或硬件调试器。
- ERROR_NO_SYSTEM_RESOURCES (0x5AA) 偶发：实为 STATUS_INSUFFICIENT_
  RESOURCES 的 Win32 映射；候选根因是目标表满/池分配失败，详见 9.129
  设计文档。yghv_hook_diag 落盘 C:\Windows\yghv_hook.log。
- clear 后 hook_count 可能残留 1；hooks 为全局表，目标退出不自动移除。
- 常驻/OS-as-guest 相关构建、R1 门控版、YGHV_UNLOAD_GUARD 门控版加载后
  可能硬冻结或无响应；YGHV_UNLOAD_GUARD 版只能重启解除。
- VMware 嵌套：MSRPM/IOPM 拦截不生效、不能剔除 NPT 私有页、不能写 CR0；
  INTR/NMI 拦截冻结 L1。
- 控制面要求调用者启用 SeDebugPrivilege（管理员客户端）。
- loader_stealth 的 kd !driver 复核需要内核调试会话，本机暂无。

【归档（D:\aaaaaavm）】
- yuanguard_hv_default_cpcr_20260813.sys = 70888311...（当前 C 盘稳定版）
- yuanguard_hv_default_auth2_20260813.sys = 1BD333E2...
- yuanguard_hv_realhook_gated_20260813.sys（多次覆盖，以 dump 为准）
- yghv_bsod_0x50_realhook_20260813_1728.dmp（0x50 dump）
- yuanguard_hv_realhook_mdl_native_20260813.sys = 88D7D986...
- yuanguard_hv_default_cfg_20260813.sys = 3F9B85B4...
- yuanguard_hv_default_hookctl_20260813.sys = FB410209...
- yuanguard_hv_loader_stealth_20260813.sys = 7B9CEECB...
- yuanguard_hv_unload_guard_20260813.sys = 8B147F29...
- yghv_service_sddl_backup.txt（harden-service SDDL 备份）
- yuanguard_hv_targets_refactor_20260813.sys = C9638324...
- yuanguard_hv_multi_target_20260813.sys = 14253486...
- yuanguard_hv_hook_diag_20260813.sys = 48824837...（多目标+诊断最新默认）
- yuanguard_hv_default_full_20260813.sys = 8A9734CA...

【下一步建议】
1. 本机非 hook 路径回归已在 9.128 完成；新窗口如需重复按 9.126/9.128
   清单执行。
2. hook 路径（install/remove/clear、真实 hook 多目标语义、锁序优化、
   ERROR_NO_SYSTEM_RESOURCES 根因）按 9.129 设计文档换平台或接硬件
   调试器后再继续。
3. loader_stealth 并入默认前先做 kd !driver 复核。

【常用命令】
- 构建默认版：cd D:\yuanguard\YuanGuardHV; cmd /c build.bat
- 构建门控版：$env:YGHV_XXX='1'; cmd /c build.bat
  （XXX ∈ BAREMETAL_STEP / R1_EXCLUDE_PRIVATE / REAL_HOOK_TEST /
  LOADER_STEALTH / UNLOAD_GUARD）
- 复制：Copy-Item bin\yuanguard_hv.sys C:\yuanguard_hv.sys -Force
- 加载/停止：sc.exe start yuanguard / sc.exe stop yuanguard
- 客户端：powershell -ExecutionPolicy Bypass -File
  D:\yuanguard\YuanGuardHV\tools\yghv_ctl.ps1 <命令>
  （state/target/list-targets/list-pages/list-hooks/config/selftest/
  exit-test/set-auto-start/unset-auto-start/harden-service/
  unharden-service/install-hook/remove-hook/clear）
- Java：cd D:\yuanguard\YuanGuardHV\tools\yghv_client; cmd /c run.bat <cmd>
- 日志：Get-Content C:\Windows\yghv_progress.log -Tail N；
  C:\Windows\yghv_ioctl.log（IOCTL 入口/出口）；
  C:\Windows\yghv_hook.log（hook 失败 stage）
- 蓝屏转储：C:\Windows\Minidump\；cdb -z <dmp> -c '!analyze -v; q'

【文档核实说明】
docs/YUANMOD_HANDOFF_CURRENT.md 中 9.110-9.118 存在驱动记录与 Codex
桌面恢复记录的编号重复（历史遗留，内容均有效）；当前主线记录为
9.115-9.130，尾部 9.130 为最新；9.131 为本文件对应的收尾记录。新窗口
以本文件为准。
