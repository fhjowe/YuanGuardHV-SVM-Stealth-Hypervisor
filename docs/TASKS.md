# YuanGuardHV 任务清单（2026-08-09 初始化）

> 状态: `[ ]` 未开始 / `[x]` 完成 / 阻塞会单独标注。
> 原则: 先打通验证通道，再按 P0 修复；每次代码改动先经用户确认并记入 `YUANMOD_HANDOFF_CURRENT.md`。

> 2026-08-13 阶段更新：OS-as-guest 常驻线（step12-102）已完成变量分离并停线。
> 唯一 PASS 形态为 step20 自旋 + INTR/NMI 拦截 + 宿主 ISR（guest 永不阻塞）；
> 阻塞/调度器上下文切换在 Ryzen 5 5500 上整机硬停（0x139 两次可诊断、
> v102 全异常 + HLT 拦截阴性），判定平台级限制。非驻留保护路线保持稳定。
> 详见 `YUANMOD_HANDOFF_CURRENT.md` 9.84-9.102。

> 2026-08-13 客户端回归：CLI/Java 客户端已显式启用 SeDebugPrivilege，
> 实机 `state/selftest/exit-test/list-java/protect` 全部 PASS（详见
> `YUANMOD_HANDOFF_CURRENT.md` 9.114）。

> 2026-08-13 9.128 最新默认版非 hook 路径回归 PASS：查询/配置/
> selftest/exit-test/服务脚本全部通过；C 盘恢复稳定默认版
> `70888311...`，服务 STOPPED（详见 `YUANMOD_HANDOFF_CURRENT.md`
> 9.128）。

> 2026-08-13 9.130 Java/JNI 非 hook 产品化 PASS：新增 `unprotect` 与
> `scan`，实机回归通过；C 盘恢复稳定默认版 `70888311...`，服务
> STOPPED（详见 `YUANMOD_HANDOFF_CURRENT.md` 9.130）。

> 2026-08-13 9.131 收尾整理：构建与 Java/JNI 忽略产物已清理，
> NEXT_WINDOW_PROMPT/TASKS 同步到 HEAD `a5f354e`/9.130。

## 0. 调试路径（先打通验证通道）

- [x] 修复 `build.bat`（for 块改子程序、路径加引号）
- [x] 重新构建并签名，记录 sys 哈希
- [x] 删除 Win11 VM 并清理 VMware 清单（文件暂存 `_win11_trash`，待手动删除）
- [x] 禁用宿主机 WiFi 适配器 `WLAN`
- [x] 启动 VM + KD 连接验证（2026-08-10 用官方镜像重装的新 VM `Windows 10 x64`，串口命名管道连接成功，KD 13:19 握手；旧“不忘初心”Guest 才是 COM1 缺失根因）
- [x] `min_drv.sys` 加载链验证（sc create/start/delete，驱动可 RUNNING）
- [x] VM 内 CPUID SVM bit + CLGI 实测
  - SVM 暴露：VMRUN 冒烟已通过（VMLOAD/VMSAVE/CLGI/VMRUN 通路正常）
  - 未暴露：不需要走裸机备选
- [x] 可运行环境最小冒烟：VMMCALL 心跳（10000 轮稳定）
- [x] 不重启反复测试：`DriverUnload` + `unload_driver.ps1`（NtUnloadDriver），启动/卸载/再启动验证

## 1. P0 复核（TECHNICAL_REVIEW.md 2026-07-30，需对照 7/31 后代码）

| ID | 标题 | 复核结果 |
|---|---|---|
| YGHV-001 | VMCB 布局与 AMD APM 不一致 | 已修复：字段/段属性对照 APM，VMRUN 通过 |
| YGHV-002 | svm_core_init 真实路径（原 #if 0） | 已复核：真实路径跑通 |
| YGHV-003 | trampoline 寄存器保存/恢复 | 已复核：栈偏移修复，心跳循环稳定 |
| YGHV-004 | VMEXIT 不推进 RIP / 返回值写错 | 已复核：VMMCALL/STOP_INTERNAL 正常返回 |
| YGHV-005 | cleanup 分配释放不匹配 / 无条件清 SVME | 部分完成：NPT 泄漏与 DriverUnload 已修；EFER.SVME 裸机恢复待复核 |
| YGHV-006 | NPT 全物理 RWX | 待处理：16GB identity 仍是 RWX，未做权限收紧 |
| YGHV-007 | VMMCALL 无认证 | 待处理：尚无调用方认证 |
| YGHV-008 | NPT 权限 API 假成功 | 部分完成：2MB large-page perm 已实现；`range/translate` 仍 stub |
| YGHV-009 | NPF event injection VALID 位 | 部分完成：NPF 恢复映射路径已验证；向 Guest 注入 #PF 未验证 |
| YGHV-010 | ASID/TLB/PAT 未初始化 | 部分完成：`g_pat`/ASID/TLB 已配置；多 ASID 管理未做 |

### 1.5 实际代码核对（2026-08-10 晚）

- 正式构建只编译链接 `main.c svm_core.c npt_core.c vmexit.c vmmcall.c svm_trampoline.S`；`multi_core.c`、`loader_stealth.c`、`pool/*`、`test_*`、`min_drv.c` 均不进入 `yuanguard_hv.sys`。
- `main.c` 当前只初始化 CPU0 + 单 VCPU + NPT/NPF 测试；`multi_core.c` 和 `svm_core_ipi_*` 存在但未被调用，多核未接线。
- `svm_prepare_vcpu` 只开启 `INTERCEPT_VMRUN | INTERCEPT_VMMCALL`；`vmexit.c` 的 CPUID/MSR/CR handler 存在但当前不可达（拦截未开启）。
- `vmmcall.c` 只实现 `HEARTBEAT/STOP_INTERNAL/VERSION/STATS`；`PROTECT_HANDLE/UNPROTECT/SCAN_PROCESS/READ_MEMORY/GET_CONFIG/SET_CONFIG/SHUTDOWN` 仅枚举，未实现。
- NPT 单页 2MB 权限已实现并验证；`npt_set_page_perm_range` 仍假成功，`npt_translate` 返回 0，无 `npt_split_2mb_to_4kb`。
- `loader_stealth.c` 未编译未调用；`stealth.c` 不存在；CPUID 隐身 handler 是死代码。
- （2026-08-10 晚快照）`tests/`、`mod/` 目录不存在；Java/JNI 客户端
  已在后续 9.41/9.114/9.130 完成。

## 2. 后续 Phase（未开始）

- [x] Phase 3 第一版进程保护（v27-v31，已完成并合并 main）
- [x] Phase 3 真实目标接入与 Java/JNI 客户端（9.41/9.114/9.130）
- [ ] Phase 3 隐形（MSR/IO/整机级）
- [ ] `tests/`、`mod/`、`vm/` 目录补齐
- [ ] 仓库卫生清理（`hv/common/*.bak`、`reference_*` 迁移、历史日志归档）
- [ ] 宿主稳定性处理：拔除/禁用 USB WiFi 设备或重装其驱动，确认 VMware 可稳定运行
- [ ] 处理火绒安全驱动冲突：程序化禁用被火绒自我保护拦截（`Access denied`/`1052`），需用户在托盘“退出火绒”或关闭自我保护后重试；备选：关闭 `vhv.enable` / 升级 VMware
- [ ] 宿主稳定性根因处理：重装/回退 VMware（17.5.2）、Windows 内存诊断、BIOS/AMD 芯片组更新
- [x] 本地完全重装 VMware 17.6.4（默认路径，跳过 Networking）——安装成功但无法解决 VM 启动崩溃
- [ ] 后续调试通道：换机/KVM，或裸机验证（testsigning、min_drv 加载链、崩溃转储分析）
- [ ] 验证串口管道假设：`Windows 10` VM 稳定运行中（`vhv.enable=TRUE`/USB 开/无调试管道），对照旧 VM 差异（`yuanhv_debug` 管道），决定下一步是否重建调试 VM

## 3. 当前阶段结论（2026-08-11）

- [x] Phase 2a：SVM init + VMRUN 单核（10000 轮 VMMCALL 心跳）
- [x] Phase 2b：NPT identity-map + NPF（16GB 映射 + 权限缺页注入）
- [x] Phase 2c：多核 DPC（每核系统线程，双核 10000 轮心跳验证通过）
- [ ] Phase 2d：物理机验证（未做）
- [x] Phase 3 第一版保护：内存页写保护（v27）、终止保护（v28）、句柄保护（v29）、常驻模式（v30/v31）
- [x] Phase 3 真实目标接入与 Java/JNI 客户端（9.41/9.114/9.130）
- [ ] Phase 3 隐形（MSR/IO/整机级）
- [ ] R1 安全地基（代码已部分实现，VMware 嵌套环境阻塞验证）
- [x] v22 稳定基线恢复（v7 路径 + 认证注入，双核心跳与卸载重载通过）
- [x] R1 第一步：NPT translate/perm-range/split 安全单测（v25 验证通过）
- [x] R1 第二步（VM 安全版）：NPT 权限注入/NPF 测试（v26 验证通过）
- [ ] R1 第三步：小范围私有页剔除（VMCB/hsave，需裸机/KVM）
- [ ] R1 第四步：NPT 自剔除 + 默认 NX（需裸机/KVM）
- [x] Phase 3 第一版内存页写保护（v27，NPF+单步重放 VM 验证通过）
- [x] Phase 3 第一版终止保护（v28，stub+VMMCALL 决策 VM 验证通过）
- [x] Phase 3 第一版句柄保护（v29，双 hook 测试 VM 验证通过）
- [x] Phase 3 常驻保护模式（v30，全核线程化 + 干净卸载 VM 验证通过）
- [x] Phase 3 最终修复（v31，TLB 刷新 + rearm 全页重锁 + 二次写测试 VM 验证通过）
- [x] Phase 3 一版合并回 main（快进到 `bf67ebc`，分支已删）
- [x] 真实目标进程接入与 Java/JNI 客户端（IOCTL 配置通道 v32、
  9.41/9.114/9.130）
- [ ] 下一阶段：常驻模式接入真实受保护页/真实 hook + NPT 共享状态加锁 + 目标进程生命周期 + hook 加固 + 控制面 CPL/CR3 + 隐形基础 CPUID + 仓库整理（v33-v42 已实现并 VM 验证通过）；裸机逐步逼近 v51 Step1-7 全 PASS，常驻（非停止 VMRUN）在宿主 2 核即冻结（判定平台兼容问题，需换机/KVM/VM）；剩余 R1、MSR/IO 隐身、整机级隐形、真实系统 hook
