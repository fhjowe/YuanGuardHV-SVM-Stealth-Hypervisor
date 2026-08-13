# v99 全核 OS-as-guest 常驻实验计划（2026-08-12）

## 1. 背景

单核 OS-as-guest 常驻在所有已测变量下硬冻结，唯一 PASS 是 step20
（自旋 + INTR/NMI 拦截 + 宿主 ISR）。冻结触发点收敛为 guest 模式内
Windows 调度器上下文切换，但尚未排除“单核不对称（一个核 guest、其余核
host）”这个结构性变量。

## 2. v99 目标与变量

目标：12 核全部无缝进入 guest 常驻（真实 Windows 整体作为 guest），验证
对称全核模式是否避开单核不对称冻结。

唯一大变量：所有核都进入 guest。其余配置沿用 step20 稳定基线：

- INTR/NMI 拦截 + 宿主 ISR（v52/step20 思路）
- CPUID/RDTSC/SHUTDOWN/VMMCALL/VMRUN 拦截 + MSR_PROT（v93）
- 每次 VMRUN 清 VMCB clean bits（v94）
- 不开 AVIC、不开 v98 影子 APIC

## 3. 编排设计（step99）

1. 新增 `yghv_os_guest_allcore_thread`：每核一个系统线程，
   `KeSetSystemAffinityThread(1ULL << core)`，`svm_prepare_vcpu(v,
   svm_os_seamless_cont)`，RIP/RSP 同 step16，intercept 同 step20，
   `svm_trampoline_os_enter(v, 1)`，延续体进入 guest 后永久阻塞
   （`KeWaitForSingleObject`，允许调度器在本核切换其它 Windows 线程）。
2. 进入前 barrier：所有核在宿主态就绪（`InterlockedIncrement` + 忙等
   `ready == online`）后同时 VMRUN，压缩“部分核 guest、部分核 host”
   过渡窗口。
3. 每核进入/失败都写同步落盘 trace：`allcore enter core=N` /
   `allcore fail core=N st=0x...`。
4. 保留 `yghv_resident_alive_thread` 与看门狗（guest 调度后自然运行），
   日志继续 `resident alive=N`。

## 4. 验收判据

- PASS：`resident alive=5/10/.../60` 持续出现且 ≥60 秒，机器可响应。
- FAIL：进入后 5 秒内整机硬冻结（日志断在 `allcore enter core=N`）或蓝屏。

## 5. 风险清单

- 不可卸载：全核常驻后无 devirtualize，验证结束必须重启清除。
- 单核进入失败即整机冻结/蓝屏，且无 minidump 时只能靠同步日志定位。
- barrier 忙等期间若某核被抢占会超时失败；失败路径必须回滚已进入核。
- 全核进入瞬间宿主态/guest 态交错，可能与 AMD 平台行为冲突。
- 当前机器已连续多次硬冻结，文档有 git blob + D 盘双备份，丢失风险已控制。

## 6. 分支决策

- v99 PASS → 单核不对称是根因；下一步做全核常驻的卸载/恢复与 APIC 虚拟化。
- v99 FAIL → 全核对称也冻结，判定平台级限制，停线并整理最终交接。
