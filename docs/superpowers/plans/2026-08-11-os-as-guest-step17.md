# OS-as-Guest Step17：全核无缝常驻（真正 OS-as-guest）

## 目标

把正在运行的 Windows 12 核整体无缝切进 guest mode 并长期保持（常驻），使
NPT/NPF、VMMCALL stub、CPUID/MSR/IO 隐身作用于真实 OS 和真实进程。

## 与 step15/16 的差异

- step15/16 用“可抛弃测试线程”验证无缝延续，guest 是测试线程的代码，
  host 按退出数限停。
- step17 让**每核真实运行上下文**进入 guest：VMRUN 后该核上所有代码
  （含后续被调度的所有 Windows 线程/ISR）都在 guest mode 中执行，且不设退出上限。

## 进入方式（每核）

1. 每核一个常驻系统线程（沿用 `yghv_resident_thread` 模式）调用
   `svm_trampoline_os_enter`，VMCB RIP = `svm_os_seamless_cont`，
   RSP = 线程栈，RFLAGS.IF = 1（OS 必须自己收中断）。
2. guest 从 `svm_os_seamless_cont` ret 回常驻线程，常驻线程随后
   **在 guest 内永久阻塞**（等一个永不被触发的事件或 pause 循环），
   把该核让给调度器；其它 Windows 线程随之在 guest 中运行。
3. trampoline 的 host 循环自持：VMEXIT → `svm_dispatch_exit` → 未停止则重入。

## 常驻拦截矩阵（OS profile）

- 开：NPF、VMMCALL、CPUID、SHUTDOWN。
- 关：INTR、NMI（OS 自己处理）、RDTSC/RDTSCP（避免退出风暴）、MSR、CR。
- `svm_dispatch_exit` 未知 exit：日志一次并**继续**（不能像测试那样 return 1 停机）。
- 保护接入：`yghv_protect` 的 NPF 决策、v55 的 `add_page` 翻译、真实 hook stub
  （vmmcall）此时才对真实 OS 生效。

## 卸载 / devirtualize（关键限制）

- 当前没有安全离开 guest 的机制：停止 VMRUN 且不恢复 = 正在 guest 中执行的
  Windows 线程上下文被丢弃 → 系统必然损坏。
- 因此 step17 阶段**不支持 `sc stop` 卸载**；验证方式为“加载 → 观察机器响应 →
  强制重启清除”。干净卸载作为后续独立设计项（需“每核安全退出点 + 逐核恢复 +
  VMCB 上下文回写”）。
- DriverUnload 若被触发会因常驻线程无法停止而挂起，需接受。

## 测试顺序与判据

- Step17a（单核常驻，core1）：guest IF=1，无退出上限；加载后机器保持响应
  （时钟/调度/IPI 正常）30-60 秒；不卸载，直接重启。
- Step17b（全核常驻）：12 核全部进入；观察响应与稳定性；重启清除。
- 风险：guest IF/中断/调度任一错误 → 硬冻结或 0x101 看门狗，无 dump，
  只能重启；建议每步只改一个变量。
