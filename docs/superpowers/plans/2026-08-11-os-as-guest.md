# OS-as-Guest（整机纳入 guest）Phase A：单核无缝有界试点

## 目标

让一个真实内核线程以“OS-as-guest”方式运行：VMRUN 后该线程继续执行 Windows 内核代码
（CPUID/RDTSC/共享计数），guest 不再返回 host 语义而是由 hypervisor 接管；有界跑完后
由 host 侧收尾并终止测试线程。验证无缝进入、VMEXIT 分发、host 栈切换三条核心路径。

## 架构要点

- guest 上下文 = 进入线程的当前上下文（GPR/RSP/RFLAGS/CR3/段/IDT/GDT/MSR 镜像）。
- guest 栈 = 测试线程自己的内核栈；host 栈 = `vcpu->host_stack`（专用 4 页）。
- 首入：`svm_trampoline_os_enter` 保存当前 GPR 到 `vcpu->regs`，VMCB
  RIP=guest main、RSP=线程栈、RFLAGS 清 IF（有界测试期间 guest 不收中断）。
- VMEXIT 后 host 循环在 trampoline 内自持：`svm_dispatch_exit` → 未停止则重入 guest。
- 停止：guest main 结束前置 `g_os_guest_stop=1` 并执行一次 CPUID；host 在
  `svm_dispatch_exit` 顶部看到旗标即停止，不再恢复 guest，`ret` 到
  `yghv_os_guest_host_done`（host 栈上植入的返回地址），由它发事件并终止测试线程。

## 安全边界

- 单核（core 1）有界测试，5000 次 CPUID/RDTSC + 共享计数。
- guest IF=0，避免 guest 内 Windows ISR 路径。
- 只拦截 CPUID/SHUTDOWN/VMMCALL，不拦截 INTR/NMI/MSR/CR（和合成 resident 的裸机配置不同）。
- 60 秒超时等待完成事件；超时按失败返回。
- 残余风险：若 VMRUN 不退或 guest 上下文错误，实机可能硬冻结（无 dump），需重启。

## 验证判据

- 日志出现 `bm os guest start` → `os guest thread enter` → `os guest counter>0` →
  `bm os guest done`，step 12 返回成功，机器全程响应。
- 之后进入 Phase B：全核 OS guest + OS profile 拦截矩阵；Phase C：真实 hook/NPF；
  Phase D：整机隐形与 R1。
