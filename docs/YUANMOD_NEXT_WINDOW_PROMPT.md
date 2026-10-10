# YuanGuardHV 新窗口提示词（2026-10-11 更新，对应 9.290 / C6 冻结）

你接手 YuanGuardHV（AMD-V SVM/NPT 隐形 Hypervisor）项目，仓库根
D:\yuanguard，分支 main，HEAD 含 9.290（**本地领先 origin/main 约 20+ 提交，
未推送——用户说推才推**）。工作区干净。

【协作铁律（最高优先级）】
1. 任何影响机器状态的命令（驱动加载/sc/压测/重启）**先向用户说明并取得明确
   确认**——重启也必须确认；只读操作（文件/事件日志/进程状态）可直接做。
2. 只修用户反馈的问题，不擅自扩大范围。
3. 每次修改与决策必须记录到 docs/YUANMOD_HANDOFF_CURRENT.md（判读用
   9.2xx 编号顺延）。
4. 构建与蓝屏取证可直接做；构建前 marker 验证（206 构建含 's206b coexist'
   不含 'hook test start'）。
5. **不要用 python -c 编辑 .h/.c 文件**（转义事故清空过 protect.h，git 恢复）——
   用 Edit 工具；跨工具链编辑 .h 前先备份。
6. 不主动推送 GitHub。

【当前状态（2026-10-11，9.290）】
- 机器：Windows 10 Pro for Workstations 19045，Ryzen 5 5500（6C12T Zen3），
  16GB，**驱动已卸载（裸金属）**。
- **当前稳定镜像 = 206c7e**（md5 356c5b6b，归档
  `D:\aaaaaavm\yuanguard_hv_step206c7e_20261011.sys`）= C4/C5/C6(v4 语义)
  全 PASS 线。206c9（假写+INVLPGA 诊断版）归档保留。
- 里程碑全 PASS：206-A/B（coexist 进入/在线卸载）、C1（控制面）、C2（页写
  保护端到端）、C3（看门狗/lasthit）、**C4（跨进程保护 protect-page fn
  0x810/0x811）**、**C5（实进程试点 + WPM 旁路定性）**、**C6 v4（影子假写：
  外来内核写→影子，真页无损，假成功幻觉完备）**。
- **C6 冻结**：NCr3=alt 切换在 Zen3 上延迟生效（1.05M 次重故障自旋 92s，自然
  TLB 逐出才生效）——INVLPGA/VmcbClean=0/TlbControl=1 均无法立即冲刷 NPT
  （GPA→HPA）翻译。**继续定位需要 KD 单步 NPF 路径的 TLB 行为（需第二台机器
  或 KDNET 主机）**。假写默认关（config-fake 0x812），关闭时零影响。
- **Zen3 硬件事实（本项目实测，与 APM 文档语义不符，务必记住）**：
  1. TlbControl=1 不冲刷 NPT（GPA→HPA）翻译——位翻转类修改侥幸生效（walk
     cache 未命中时），PA 变更类修改不可见；
  2. VMCB clean bits 缓存 nCr3 字段写——写 NCr3 必须 VmcbClean=0 才可能生效；
  3. 即便如此，nCr3 切换的翻译可见性仍延迟至自然 TLB 逐出；
  4. NPF（VMEXIT）不写 CR2——注入 #PF 必须手动设 SSA.Cr2（否则陈旧 CR2 →
     spurious-retry 自旋，c12 风暴同源）。
- WPM 旁路 = 已知限制（README 已记录）：MmCopyVirtualMemory 双形态
  （MiDoPoolCopy 调用方 CR3 / KeStackAttachProcess 目标 CR3+cpl0），假写可吞
  但有上述延迟问题；C6 重启需 KD 环境。

【关键文件】
- `docs/YUANMOD_HANDOFF_CURRENT.md`：全程判读史（**最近 9.244–9.290 是
  206/C4-C5/C6 线**，每轮含蓝屏判读）。
- `YuanGuardHV/tools/yghv_ctl.ps1`：控制面客户端（30 命令，含跨进程
  protect-page/mmf-*/scan-pid/wpm-write/config-fake/TryRead 等）。
- `D:\aaaaaavm\run_c16_step206c6.ps1`：C4 回归脚本（按镜像名部署，哈希门）。
- `D:\aaaaaavm\kd_c6f.bat`：kd 转储取证模板（`kd_last.bat <dump>` 通用）。
- 归档镜像：`D:\aaaaaavm\yuanguard_hv_step206*.sys`（206c7e = 稳定，
  206c9 = 假写诊断版）。
- 蓝屏取证流程：minidump → kd !analyze -v（符号经代理）→ 栈/模块定位 →
  判读入库。**切勿直接改代码猜原因**。

【本机雷区（全部实测）】
- python -c 转义事故清空过 protect.h（已 git 恢复）——.h/.c 编辑用 Edit 工具。
- default 构建会覆盖 bin\yuanguard_hv.sys——**归档前 marker 验证**（206 构建含
  's206b coexist'）。
- run_c* 脚本自带残留 VMM guard + 部署哈希门（防旧镜像静默运行）。
- 目标进程 90s 窗口会被长自旋耗尽（gate3 的 err 87 教训）。
- 跨工具链 .h 编辑前先备份（protect.h 清空事故）。

【下一步候选（用户定）】
1. 真实产品目标流程化（Minecraft/Forge 目标选择+页选取——控制面已就绪）。
2. C6 重启（需 KD 环境：第二台机器或 KDNET 主机）。
3. 推送 GitHub（本地领先 20+ 提交）。
