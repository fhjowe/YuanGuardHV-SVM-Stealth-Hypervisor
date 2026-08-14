# YuanGuardHV 新窗口无缝衔接提示词（2026-08-14 会话交接）

> 将此文档内容作为新窗口的首条提示词（或保存为文件供新会话读取）。

你正在接管 **YuanGuardHV** 项目——一个 **AMD-V SVM/NPT 隐形 Hypervisor 内核驱动**
（保护 Minecraft/Forge 进程），仓库 **D:\yuanguard**（Windows，分支 main）。

## 当前状态
- **HEAD = `554d07a`**（驱动二进制 SHA `2E79F4C4` = 独立 guest CR3 + 完整 workload
  功能），工作区干净。
- **本机（Ryzen 5 5500）安全**：`C:\yuanguard_hv.sys` = 稳定默认版 `70888311...`，
  服务 `yuanguard` STOPPED，电源正常（IDLEDISABLE=0）。
- **本机定位**：可作构建/静态验证平台；**真实 hypervisor 运行测试风险高**（本机
  在持续 SVM/NPT 负载下会 CPU 级锁死，已 4 次看门狗捕获证实）——需换平台。

## 本会话已完成的关键工作（先读 `docs/YUANMOD_HANDOFF_CURRENT.md` 9.141-9.156）
1. **冻结根因+修复**：guest 原本共享宿主内核 CR3 → guest TLB 与宿主 TLB 冲突 →
   本机 CPU 全核锁死。修复 = **独立 guest CR3**（main.c `yghv_build_guest_cr3`
   构建专用页表，persistent 各核 `cr3=guest_cr3`）。
2. **hook 调用三重重置根因+修复**：`svm_alloc_vcpu` 的 `host_stack` 是 4 页
   （SVM_HOST_STACK_PAGES=4），guest RSP 在第 4 页，但 guest CR3 只映射了
   第 1 页 → call 压栈 NPF → 三重重置。修复 = **映射全部 4 页栈**（gv[] 加到 64）。
3. **完整功能恢复**：persistent c0 跑完整 workload guest（写保护 NPF/rearm +
   hook 调用），**12 核全 ACTIVE，selftest PASS，60s+ 稳定，干净卸载，无蓝屏**。
4. **诊断增强**：看门狗（`C:\Windows\yghv_watchdog.log`，每 5s 每核
   `e=/s=/x=/r=/p=/g=`）含 exit 计数/状态/last_exitcode/rip/rsp/cr3；修复了
   join 蓝屏 0xA（线程句柄当对象指针 → `ObReferenceObjectByHandle`）与空日志
   （缓冲 1600 + `yghv_wd_hex` 边界 `off+18 > bufsz-1`）。

## 关键文档索引
- `docs/YUANMOD_HANDOFF_CURRENT.md`（9.141-9.156 = 本会话完整历史；
  9.152-9.155 = 冻结+功能修复）
- `docs/YGHV_FULL_REVIEW_20260814.md`（全面审查 + 任务清单 REV-001..048）
- `docs/TASKS.md`（任务清单 + 部署基线）
- `docs/YGHV_HOOK_LOCK_AND_0x5AA_REDESIGN_20260813.md`（REV-001 锁序重构方案）
- `docs/YGHV_STEALTH_AUDIT_20260813.md`（隐藏审计）
- `docs/YUANMOD_NEXT_WINDOW_PROMPT.md`（协作铁律 4 条 + 常用命令）

## OS-as-guest 结论
本机**不建议直接全核 OS-as-guest**（最重 SVM 负载，冻结风险高）。OS-as-guest
原本用 guest 自己的 CR3（非我们修的共享 CR3 机制），其历史冻结
（9.84-9.102：0x139×2、v102 全异常+HLT 阴性）是**另一个平台级机制**。
**独立 CR3 + 完整栈映射 + 全核看门狗是重开 OS-as-guest 的良好基础，应换平台
继续**；本机维持合成 resident 形态。

## 构建/测试命令
- 构建：`cd D:\yuanguard\YuanGuardHV && cmd /c build.bat`
  （静态校验 + 编译 + 签名，输出 `bin\yuanguard_hv.sys`）
- 客户端：`tools\yghv_ctl.ps1`（state/list-pages/selftest/exit-test 等）
- **实机部署测试流程**（每次有冻结风险，需用户备好重启）：
  备份 `C:\yuanguard_hv.sys` → 覆盖 → `sc start yuanguard` →
  验证（watchdog + state/selftest）→ `sc stop` → 恢复 `70888311...`
- 看门狗：`C:\Windows\yghv_watchdog.log`

## 剩余问题
- **P3（需换平台/kd）**：REV-001 锁序重构（HOOK_LOCK 文档有方案）、
  REV-035 INTR/NMI kd 复核。
- **P2 延后**：REV-029（设备 SD）、REV-043/046（R1 族）。
- **本机冻结**：已诊断（4 次捕获 → 平台级），驱动修复为独立 CR3，
  合成 resident 稳定。

## 协作铁律
1. 任何代码/文档改动前先说明并取得确认。
2. 只修用户反馈的问题。
3. 每次修改记录到 `docs/YUANMOD_HANDOFF_CURRENT.md`。
4. 不擅自回滚；先查 `git status`。

## 下一步候选
- **A**：审查 OS-as-guest 门控代码（os_guest 线程 + os_enter 汇编），写
  "换平台重开 OS-as-guest 准备清单 + 风险分析"（零机器风险）。
- **B**：本机试探性逐步重开 OS-as-guest（有冻结风险，需用户备好重启）。
- **C**：维持合成 resident 稳定形态，做收尾/清理。
