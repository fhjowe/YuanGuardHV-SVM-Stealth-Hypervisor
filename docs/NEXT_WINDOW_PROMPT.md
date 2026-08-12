# 下一窗口提示词（2026-08-12 会话结束后粘贴给新的 Codex 窗口）

```text
你接手 YuanGuardHV（AMD-V SVM/NPT 隐形 Hypervisor）项目，仓库根 D:\yuanguard，当前分支 main。

【协作铁律（最高优先级）】
1. 任何代码/资源/文档改动前，先向用户说明并获得明确确认；未确认不得改。
2. 只修用户反馈的问题，不擅自扩大范围。
3. 每次修改与决策必须记录到 docs/YUANMOD_HANDOFF_CURRENT.md。
4. 禁止回滚用户/历史改动；工作区如有未提交改动，先查 git status 再动手。

【当前进度快照（2026-08-12，HEAD a037044）】
- 实机（Ryzen 5 5500，>16GB）已验证且合入 main：
  - v52 INTR/NMI/SHUTDOWN 拦截修复裸机常驻冻结；v53 全核常驻 6.5 分钟 PASS；
  - v54 默认版完整流程实机 PASS（selftest/exit-test）；v55 add_page 改用
    KeStackAttachProcess+MmGetPhysicalAddress 翻译、set_target 清空旧页表，
    Minecraft Forge 1.20.1 实机 attach 64 页 arm PASS；
  - v60 移除 v58 诊断刷盘（曾致实机冻结），实机回归 PASS；
  - Java/JNI IOCTL 客户端 tools\yghv_client（protect/list-java）实机 PASS；
  - OS-as-Guest Phase A/B：step12 单核有界、step13 双核、step14 全核 12 核、
    step15 单核无缝延续、step16 全核无缝延续 —— 全部实机 PASS。
- 常驻（真 OS-as-guest）卡点：
  - step17a 单核常驻（IF=1、无退出上限、不可卸载）v71/v72/v73 实机均 <5 秒
    整机硬冻结，无 dump，看门狗无法落盘；
  - step18 自旋常驻（IF=1，guest 无限 CPUID/RDTSC 循环，不阻塞不切线程）v74
    实机同样 <5 秒冻结；进度日志写到 `bm os resident spin running` 但无
    `resident alive=5`；无 minidump。
  - 结论（变量分离）：排除 guest 内调度器切换；主因收敛为 **IF=1 下物理中断在
    guest 模式投递 / guest 模式 APIC 交互**，与 AMD 56683 errata 1363 高度吻合。
  - 下一步实验 step19：step18 + INTR/NMI 拦截（ISR 在宿主态执行，v52 思路），
    验证“guest 中断投递是冻结根因”；若存活再做宿主收中断 + 事件注入回 guest。

【关键文件】
- 构建：YuanGuardHV\build.bat（clang-cl + MSVC link + signtool，输出
  bin\yuanguard_hv.sys）；裸机单步用 set YGHV_BAREMETAL_STEP=NN。
- 核心代码：YuanGuardHV\hv\（main.c、svm_core.c、svm_trampoline.S、vmexit.c、
  vmmcall.c、multi_core.c、npt_core.c、protect.c、control_device.c、common\）。
- OS-guest 相关：svm_trampoline.S 的 svm_trampoline_os_enter（if1 参数）、
  svm_os_seamless_cont；main.c 的 yghv_os_guest_* / yghv_resident_* 与
  step12-18；vmexit.c 的 YGHV_OS_GUEST_EXIT_LIMIT 与 g_os_resident_mode。
- 客户端：YuanGuardHV\tools\yghv_ctl.ps1、tools\yghv_client（Java/JNI）。
- 记录：docs\YUANMOD_HANDOFF_CURRENT.md（9.27-9.58）、docs\TASKS.md、
  docs\superpowers\plans\2026-08-11-os-as-guest*.md。

【环境与调试命令】
- VMX：C:\Users\Administrator\Documents\Virtual Machines\Windows 10 x64\
  Windows 10 x64.vmx（VMware 17.6.4）；当前 VM 停止（vmrun list 确认）。
- KD 串口：\\.\pipe\yuanhv_debug；kd_ctl.ps1 只在“早期启动”可靠连接，
  运行中的 VM live KD 表现为 “Waiting to reconnect”不可用。
- 构建并复制：cd YuanGuardHV; cmd /c build.bat;
  Copy-Item bin\yuanguard_hv.sys D:\aaaaaavm\yuanguard_hv_vNN.sys
  （共享目录文件被占用时先改名 .locked）。
- 实机加载：Copy-Item ... C:\yuanguard_hv.sys; sc.exe start yuanguard;
  Get-Content C:\Windows\yghv_progress.log -Tail N
  （进度日志现为同步落盘，每次 trace 即 flush，硬冻结后仍可读）。
- 注意：C:\yuanguard_hv.sys 当前是 v74 step18；实机测试前先核对哈希。

【已知限制与雷区】
- 本机实机：非常驻路径稳定；**常驻 IF=1（guest 收中断）<5 秒整机硬冻结、
  无 dump**，疑似平台级（Zen3 errata），step19 未验证；VM 多核心跳冻结未解决。
- VMware 嵌套：MSRPM/IOPM 拦截不生效、不能剔除 NPT 私有页、不能写 CR0；
  INTR/NMI 拦截在嵌套下会冻结 L1（v56/v57 已按“嵌套=VMwareVMware 检测”
  关闭三个拦截）。
- VM ntoskrnl 测试签名下禁用 PsSetCreateProcessNotifyRoutine/Ex（0xC000007A
  且残留回调 0xCE）；只导出 ZwTerminateProcess（无 NtTerminateProcess）。
- step17/18 常驻不可卸载（停止 VMRUN 会丢弃 guest 线程上下文），验证方式为
  “加载 → 观察 → 重启清除”；sc stop 会挂起。
- git 索引曾损坏，已重建（.git\index.corrupt 残留可忽略）。

【下一步（按优先级）】
1. step19：step18 + INTR/NMI 拦截，实机验证“guest 中断投递是冻结根因”；
   若存活 → 设计宿主收中断 + VMCB 事件注入回 guest 的正规方案。
2. VM 验证 step18/19（VM 停止状态，需先 vmrun start；崩了只崩 VM 且能出 dump）。
3. 若本机判定平台级限制：换机/KVM 验证常驻；实机先做非驻留收尾
   （真实系统 hook、真实页 NPF、CPUID/MSR/IO 隐身、R1）。

先读 docs\YUANMOD_HANDOFF_CURRENT.md 与 docs\TASKS.md 确认进度，
再和用户确认下一步后动手。
```
