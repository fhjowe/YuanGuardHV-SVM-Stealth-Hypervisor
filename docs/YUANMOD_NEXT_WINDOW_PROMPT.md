# YuanGuardHV 新窗口提示词（2026-08-15 更新版，对应 9.198）

你接手 YuanGuardHV（AMD-V SVM/NPT 隐形 Hypervisor）项目，仓库根
D:\yuanguard，分支 main，HEAD `7f19683`（工作区干净；文档收尾到 9.198）。

【协作铁律（最高优先级）】
1. 任何代码/资源/文档改动前，先向用户说明想法并取得明确确认；未确认不得改。
2. 只修用户反馈的问题，不擅自扩大范围。
3. 每次修改与决策必须记录到 docs/YUANMOD_HANDOFF_CURRENT.md。
4. 禁止回滚用户/历史改动；工作区如有未提交改动，先查 git status 再动手。

【当前状态】
- 机器：Windows 10 Pro for Workstations 19045，Ryzen 5 5500（6C12T），>16GB。
  **BIOS 已升级（AGESA 更新），SVM 已重新开启**（VirtualizationFirmwareEnabled=True）。
- 服务 yuanguard：STOPPED；`C:\yuanguard_hv.sys` = 稳定默认版
  `70888311B38EF8D386252271CAF8EC8ADE8D96FAA472C7260E30692B95F92E3B`
  （归档 `D:\aaaaaavm\yuanguard_hv_default_cpcr_20260813.sys`，本会话另备份
  `D:\aaaaaavm\yuanguard_hv_c_drive_backup_20260815.sys`）。
- 仓库 HEAD `7f19683`（B 路线 APIC 虚拟化实验 + OS-as-guest 总结），工作区干净。
- 注意：C 盘 `70888311` 是 9.112 旧稳定版，**不支持多目标客户端命令（GET_TARGETS/
  list-targets）**；当前 `tools/yghv_ctl.ps1`/Java 客户端需配 **HEAD 默认版**（bin 已重建
  `8B59430C...`）。实机回归用 HEAD 默认版（9.128 清单：state/list-targets/selftest/
  exit-test）。

【关键结论（务必先读 9.84-9.198；横向总结见
docs/YGHV_OS_AS_GUEST_SUMMARY_20260815.md）】
1. **OS-as-Guest 本机定论（9.198）**：本机无法运行 OS-as-Guest——guest 态执行真实
   Windows 内核代码（调度器/ISR）在 <10s 内 0x101 或硬冻结，无论独立 CR3/APIC 虚拟化/
   NPT 全权限/ASID-TLB/HLT/host ISR 组合。**平台级限制（AMD errata 1363 类，无修复）**。
   本机可运行形态 = 有界试点（step12/14/16）+ **step20 自旋常驻**（可干净卸载）。
2. MSV（9.180-9.183）与 B 路线（9.184-9.197）均为门控实验
   （`YGHV_BAREMETAL_STEP=200/202`），**默认构建不激活**；换平台时可复用。
3. **非驻留保护路线（产品主线）稳定**：内存页写保护 / 终止保护 / 句柄保护 / 真实目标
   接入 / Java 客户端均 PASS。
4. 已完成里程碑：多目标（2B-3，targets[4]）、loader_stealth 接线（2C，门控默认 0）、
   服务持久化/防卸载（sc sdset + UNLOAD_GUARD 门控）、P0/P1/P2 修复（REV-003/004/005/
   006/002/010/011/013/015/036/037/039/040/041/044/045/008/009/012/014/016/017/018/
   019/047）、静态校验（build.bat 编译前）、隐藏审计、真实 hook（MDL 可写映射）。
5. 9.129 静态分析：0x5AA 实为 ERROR_NO_SYSTEM_RESOURCES（STATUS_INSUFFICIENT_
   RESOURCES），候选根因目标表满/池分配失败；锁序与 0x5AA 方案见
   docs/YGHV_HOOK_LOCK_AND_0x5AA_REDESIGN_20260813.md。

【本机雷区（重要）】
- **本机 hypervisor 负载有平台级冻结/0x101 史**：OS-as-guest 相关构建、R1 门控版、
  YGHV_UNLOAD_GUARD 门控版加载可能硬冻结/无响应；本机不承载持续 VMRUN 常驻负载，
  OS-as-guest 实验**换平台**。YGHV_UNLOAD_GUARD 版只能重启解除。
- install/remove/clear 类 hook 路径实机验证可能整机硬冻结（持 g_protect_lock 内全核心
  pause 死锁，9.118/9.125）——本机已停该族实验。
- 控制面要求调用者启用 SeDebugPrivilege（管理员客户端，脚本已自动启用）。
- VMware 嵌套：MSRPM/IOPM 拦截不生效、不能剔除 NPT 私有页、不能写 CR0；且 VMware
  17.6.4 在这台 AMD 主机上启动任何 VM 会整机硬卡死（9.85-9.88）——VM 调试不可用。
- 深拷贝整棵页表树（yghv_clone_host_cr3_deep）慢（600ms+）且带竞态（D1c 0x50；D4 已用
  克隆期暂停 diag 缓解）——仅门控实验使用。

【归档（D:\aaaaaavm）】
- 稳定版：`yuanguard_hv_default_cpcr_20260813.sys`（70888311...）
- 2026-08-15 B/D 系列：`yuanguard_hv_{msv,msv2,b0,b1min,b1min2,b1full,d1_npfdiag,
  d1b_fastdiag,d1c_10ms,d2_shallow,d3_hostisr_inject,d3base_noinject,d4base_deep}_
  20260815.sys`（哈希见总结文档 6.1）
- 转储：`D:\aaaaaavm\yghv_bsod_*.dmp`（0x101 ×9、0xE2、0x50；清单见总结文档 6.2）
- 总结文档：`docs/YGHV_OS_AS_GUEST_SUMMARY_20260815.md`

【下一步建议】
1. **非驻留保护路线继续（产品主线）**：真实 hook 多目标语义与锁序（REV-001，换平台/
   kd）、目标生命周期细节、控制面完善、Java 客户端补 config/unprotect/scan 之外命令、
   loader_stealth 并入默认前 kd `!driver` 复核、R1 私有页剔除换平台验证。
2. **OS-as-Guest 换平台重启条件**：见总结文档第 7 节（顺序：有界试点 → ASID/TLB 卫生
   负结果记录 → 独立 CR3 + APIC 虚拟化 → 阻塞常驻）。
3. 本机仅作构建/静态验证；真实 hypervisor 运行/回归测试换平台。

【常用命令】
- 构建默认版：`cd D:\yuanguard\YuanGuardHV; cmd /c build.bat`
  （静态校验 + 编译 + 签名，输出 bin\yuanguard_hv.sys）
- 构建门控版：`$env:YGHV_XXX='1'; cmd /c build.bat`
  （XXX ∈ BAREMETAL_STEP / R1_EXCLUDE_PRIVATE / REAL_HOOK_TEST / LOADER_STEALTH /
  UNLOAD_GUARD）
- 复制：`Copy-Item bin\yuanguard_hv.sys C:\yuanguard_hv.sys -Force`
- 加载/停止：`sc.exe start yuanguard` / `sc.exe stop yuanguard`
- 客户端：`powershell -ExecutionPolicy Bypass -File D:\yuanguard\YuanGuardHV\tools\
  yghv_ctl.ps1 <命令>`
  （state/target/list-targets/list-pages/list-hooks/config/selftest/exit-test/
  set-auto-start/unset-auto-start/harden-service/unharden-service/install-hook/
  remove-hook/clear）
- Java：`cd D:\yuanguard\YuanGuardHV\tools\yghv_client; cmd /c run.bat <cmd>`
- 日志：`C:\Windows\yghv_progress.log`（进度）、`yghv_ioctl.log`（IOCTL）、
  `yghv_hook.log`（hook 失败 stage）、`yghv_watchdog.log`（常驻诊断）
- 蓝屏转储：`C:\Windows\Minidump\`；`cdb -z <dmp> -c '!analyze -v; q'`
- 常用：`cd D:\yuanguard; git status / git log --oneline -5`

【文档核实说明】
- `docs/YUANMOD_HANDOFF_CURRENT.md` 中 9.110-9.118 存在驱动记录与 Codex 桌面恢复记录
  的编号重复（历史遗留，内容均有效）。
- 主线记录最新为 9.198；OS-as-guest 横向总结见 `docs/YGHV_OS_AS_GUEST_SUMMARY_20260815.md`；
  有效文档：HANDOFF_CURRENT / TASKS / YGHV_FULL_REVIEW_20260814 / HOOK_LOCK 设计 /
  STEALTH_AUDIT / OS_AS_GUEST_RESEARCH_20260814 / 本文件。
