# YuanGuardHV 任务清单（2026-08-09 初始化）

> 状态: `[ ]` 未开始 / `[x]` 完成 / 阻塞会单独标注。
> 原则: 先打通验证通道，再按 P0 修复；每次代码改动先经用户确认并记入 `YUANMOD_HANDOFF_CURRENT.md`。

## 0. 调试路径（先打通验证通道）

- [x] 修复 `build.bat`（for 块改子程序、路径加引号）
- [x] 重新构建并签名，记录 sys 哈希
- [x] 删除 Win11 VM 并清理 VMware 清单（文件暂存 `_win11_trash`，待手动删除）
- [x] 禁用宿主机 WiFi 适配器 `WLAN`
- [ ] 启动 VM + KD 连接验证（阻塞升级为“VMware 17.6.4 本身在 AMD 宿主触发硬卡死”：火绒/USB/VM USB/`vhv.enable` 均排除，仍复现 5 次；待选：重装/回退 VMware、硬件诊断、或转裸机/KVM）
- [ ] `min_drv.sys` 加载链验证（sc create/start/stop/delete）
- [ ] VM 内 CPUID SVM bit + CLGI 实测
  - SVM 暴露：尝试 VM 内 VMRUN 冒烟
  - 未暴露：准备裸机测试（testsigning、签名、usbwifi 处理）
- [ ] 可运行环境最小冒烟：VMMCALL 心跳

## 1. P0 复核（TECHNICAL_REVIEW.md 2026-07-30，需对照 7/31 后代码）

| ID | 标题 | 复核结果 |
|---|---|---|
| YGHV-001 | VMCB 布局与 AMD APM 不一致 | 待复核 |
| YGHV-002 | svm_core_init 真实路径（原 #if 0） | 待复核（文件已变） |
| YGHV-003 | trampoline 寄存器保存/恢复 | 待复核 |
| YGHV-004 | VMEXIT 不推进 RIP / 返回值写错 | 待复核 |
| YGHV-005 | cleanup 分配释放不匹配 / 无条件清 SVME | 待复核 |
| YGHV-006 | NPT 全物理 RWX | 待复核 |
| YGHV-007 | VMMCALL 无认证 | 待复核 |
| YGHV-008 | NPT 权限 API 假成功 | 待复核 |
| YGHV-009 | NPF event injection VALID 位 | 待复核 |
| YGHV-010 | ASID/TLB/PAT 未初始化 | 待复核 |

## 2. 后续 Phase（未开始）

- [ ] Phase 3 隐形、保护功能、Java 层
- [ ] `tests/`、`mod/`、`vm/` 目录补齐
- [ ] 仓库卫生清理（`hv/common/*.bak`、`reference_*` 迁移、历史日志归档）
- [ ] 宿主稳定性处理：拔除/禁用 USB WiFi 设备或重装其驱动，确认 VMware 可稳定运行
- [ ] 处理火绒安全驱动冲突：程序化禁用被火绒自我保护拦截（`Access denied`/`1052`），需用户在托盘“退出火绒”或关闭自我保护后重试；备选：关闭 `vhv.enable` / 升级 VMware
- [ ] 宿主稳定性根因处理：重装/回退 VMware（17.5.2）、Windows 内存诊断、BIOS/AMD 芯片组更新
