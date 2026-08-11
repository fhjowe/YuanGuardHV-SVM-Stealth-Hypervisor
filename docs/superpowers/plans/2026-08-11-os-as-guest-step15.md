# OS-as-Guest Step15：无缝延续 + 全核常驻路线

## 目标

在 Phase A/B（独立 guest 函数）基础上验证“无缝延续”：trampoline 进入 guest 后，
guest 从 `svm_os_seamless_cont` 直接 `ret` 回调用者，调用者后续代码在 guest mode
中继续执行。这对应真实 OS-as-guest 的“OS 线程从 VMRUN 后下一条指令无缝继续”语义。

## step15（本次）：单核有界无缝

- 专用系统线程（core1）调用 `svm_trampoline_os_enter`，VMCB RIP =
  `svm_os_seamless_cont`；进入 guest 后 trampoline 的调用者（该线程函数）
  在 guest 中继续，执行 5000 次 CPUID/RDTSC + 计数。
- 停止仍由 host 按每核退出次数限停（10000 次），host_done 发事件并终止测试线程
  （测试线程可抛弃，不涉及真实系统线程）。
- 判据：`os seamless enter=1` → 计数器>0 → `bm os seamless done`，机器响应，
  `sc stop` 干净。

## step16+：全核无缝常驻（真正 OS-as-guest）

- 每核把真实运行上下文（RIP=post-VMRUN、RSP=当前线程栈、CR3/段/IDT/MSR 镜像）
  作为 guest 无缝进入，guest 不再返回（VMRUN 后继续 OS）。
- 中断：guest IF=1（OS 自己收中断）；NMI 处理；SHUTDOWN 拦截保留。
- 控制面：NPF/VMMCALL/CPUID 拦截开启；MSR/CR 选择性拦截。
- 卸载/devirtualize：需要“安全退出点 + 逐核离开 guest”机制，未实现，风险最高。
- 测试策略：建议先在 VM（可崩溃回收）验证，再实机有界化。
