## 1. Phase 1: Hypervisor 核心 + 基础保护

- [ ] 1.1 创建 YuanGuardHV 项目骨架（Java 层结构 + hv/ 目录）
- [ ] 1.2 实现 AMD-V SVM 初始化（CPUID 检测/EFER.SVME/VMCB 配置/VMRUN）
- [ ] 1.3 实现 identity NPT 页表管理（GPA→HPA 1:1 映射 + 动态拆分）
- [ ] 1.4 实现 VM Exit 基础分发器（NPF/VMMCALL/CPUID 最小集）
- [ ] 1.5 实现自卸载 Loader 驱动（SCM→VMRUN→脱链表→自删→释放）
- [ ] 1.6 实现 VMMCALL 通信协议（心跳/状态/基础命令）
- [ ] 1.7 实现 NPT 句柄保护（ObpCreateHandle Execute Trap + 访问掩码检查）
- [ ] 1.8 实现进程终止保护（TerminateProcess 路径拦截）
- [ ] 1.9 实现 JVM DLL 完整性扫描（NPT 直读 + 哈希对比）
- [ ] 1.10 实现句柄枚举（PspCidTable NPT 遍历 → VMMCALL 返回）
- [ ] 1.11 实现退出清理（VMXOFF + NonPagedPool 覆写 + 文件删除）
- [ ] 1.12 Java 层适配（HVMod 入口 / HvComm 通信 / HVLoader / HVCommand）
- [ ] 1.13 实现隐形基础包（CPUID 伪造/ETW 抑制/随机 PoolTag）
- [ ] 1.14 实现 VBS 检测 + 用户态降级模式

## 2. Phase 2: 进程保护 + MC 专属 + 内核监控 + 无痕内存

- [ ] 2.1 实现调试器检测（DRx 写监控/INT3 页面异常/硬件断点影子）
- [ ] 2.2 实现 DLL 注入全向量检测（反射式/Threadless/PROPagate/Atom/EarlyBird/Ctrl/ServiceDLL/Fiber/Hollowing）
- [ ] 2.3 实现代码段完整性监控（.text 写 Trap + 哈希校验 + 自修复）
- [ ] 2.4 实现速度作弊对抗（TSC/HPET 虚拟化 + 时间扭曲检测）
- [ ] 2.5 实现线程劫持检测（SetThreadContext/APC）
- [ ] 2.6 实现句柄表完整性（定期扫描 + 异常条目告警）
- [ ] 2.7 实现堆栈 pivot 检测（RSP 跳变 + 返回地址链验证）
- [ ] 2.8 实现 NX/页面权限强制执行
- [ ] 2.9 实现渲染管线完整性（LWJGL/OpenGL draw call NPT Hook）
- [ ] 2.10 实现输入流保护（SendInput/RawInput/按键记录检测）
- [ ] 2.11 实现窗口消息过滤（WM_GETTEXT/WM_KEYDOWN/GWL_WNDPROC）
- [ ] 2.12 实现剪贴板保护
- [ ] 2.13 实现 X-Ray 检测（NPT Trap chunk 加载）
- [ ] 2.14 实现发包完整性校验（Netty ChannelRead/Write NPT Hook）
- [ ] 2.15 实现截屏/录屏检测（GDI BitBlt/DXGI OutputDuplication）
- [ ] 2.16 实现外部叠加层检测（Discord/Steam/GeForce Overlay）
- [ ] 2.17 实现游戏逻辑保护（连点器/Reach/飞行/隐身/暴包）
- [ ] 2.18 实现内核监控（SSDT/IDT/GDT/MSR/驱动加载/回调链/隐藏进程/KD调试器/APC/DPC/签名强制）
- [ ] 2.19 实现无痕扫描（签名/完整性/隐藏代码/字符串/跨进程/内核/时间线差分/全物理）
- [ ] 2.20 实现无痕修改（热补丁/变量操作/代码注入/自修复/NPT 重映射/寄存器替换）
- [ ] 2.21 实现无痕过滤（执行唯读/读影子/写镜像/条件视图/内存蜜罐/幽灵页/跨视图检测/权限陷阱）
- [ ] 2.22 实现无痕 Hook 引擎（Syscall/函数入口出口/SSDT影子/IRP分发/ntdll/Kernel32/COM/SEH/APC/LWJGL/Netty/Entity）
- [ ] 2.23 实现高级 Hook 模式（影子/瞬态/多态/反Hook/条件/深度调用链）
- [ ] 2.24 实现无痕 IO/设备过滤（Port I/O/MMIO/PCIe/MSR）

## 3. Phase 3: 高级攻防 + 自身防御 + 反取证

- [ ] 3.1 实现自身代码加密（NPT XOR 加密 + 动态解密）
- [ ] 3.2 实现 NPT 页表隐藏
- [ ] 3.3 实现时间反演（RDTSC/RDTSCP 拦截）
- [ ] 3.4 实现性能计数器伪造（RDPMC 拦截）
- [ ] 3.5 实现崩溃 dump 劫持（KeBugCheck2 拦截）
- [ ] 3.6 实现内核栈混淆 + Pool Header 伪造
- [ ] 3.7 实现反虚拟化检测防御（CPUID/MAC/ACPI/SMBIOS/RDTSC/VMExit延迟伪装）
- [ ] 3.8 实现 VMCB 防篡改 + 蓝 Pill 检测
- [ ] 3.9 实现 GPU 防护（VRAM 扫描/着色器检测/驱动完整性/AGS 劫持）
- [ ] 3.10 实现证书/DPAPI 完整性
- [ ] 3.11 实现特权/Token 攻击防护（SeDebug/DuplicateToken/SetToken/Impersonate/CreateToken）
- [ ] 3.12 实现外部攻击面（音频/录屏/ARP/DNS/文件系统/资源包/驱动卸载防护/Self-Defense）
- [ ] 3.13 实现进程内存加密（NPT 页加密：Guest 读密文）
- [ ] 3.14 实现影子页表（不同核心不同 NPT）
- [ ] 3.15 实现系统调用全追踪（可选，性能预算内）
- [ ] 3.16 实现网络包过滤（NDIS 路径 VM Exit 级拦截）
- [ ] 3.17 实现定时器虚拟化（APIC Timer/HPET 劫持）
- [ ] 3.18 实现跨核心攻击检测（IPI 监控）
- [ ] 3.19 实现时序侧信道防御（Flush+Reload/Prime+Probe 检测）
- [ ] 3.20 实现 SMM 监控（SMI 入口陷阱）
- [ ] 3.21 实现嵌套虚拟化支持（被其他 Hypervisor 包裹时检测 + 降级）
- [ ] 3.22 实现 Windows 子系统监控（注册表/WMI/TaskScheduler/AppCompat/DCOM/ALPC/SHWXHook/WindowSubclass/RawInput/NamedPipe/Section/JobObject）
- [ ] 3.23 实现全持久化覆盖（Run键/COM劫持/PATH/BITS/netsh/TimeProvider/PrintMonitor/LSA/Auth/SHELLEX/Winlogon/Office）
- [ ] 3.24 实现 IPC 全通道 + 网络纵深（Mailslot/RPC/Loopback/DDE/TDI/NDIS/WFP/WSK/HTTP代理/TLS中间人）

## 4. Phase 4: 配置 + 运维 + 用户体验

- [ ] 4.1 实现 11 维配置体系（开关/阈值/策略/热加载/加密/签名/预设/预算/条件/时间窗口/同步）
- [ ] 4.2 实现 5 个预设模板（silent/balanced/aggressive/full/custom）
- [ ] 4.3 实现 NPT 加密配置存储
- [ ] 4.4 实现配置热加载（VMMCALL 实时生效）
- [ ] 4.5 实现 `/overlay status` 完整状态面板（NPT/VMExit/Heartbeat/功能状态矩阵）
- [ ] 4.6 实现 `/overlay config` 完整配置管理
- [ ] 4.7 实现 `/overlay events` 事件查看系统
- [ ] 4.8 实现 `/overlay attest` 远程证明生成
- [ ] 4.9 实现 `/overlay perf` 性能开销统计
- [ ] 4.10 实现结构化事件日志（JSONL + 轮转）
- [ ] 4.11 实现攻击证据链生成（可提交服务端）
- [ ] 4.12 实现看门狗（自检心跳 → 超时恢复）
- [ ] 4.13 实现崩溃 dump 保存（异常时自动保存状态）
- [ ] 4.14 实现热补丁（运行时替换 Hypervisor 代码段）
- [ ] 4.15 实现服务器远程证明
- [ ] 4.16 实现启动链验证
- [ ] 4.17 实现 Java Agent 检测
- [ ] 4.18 实现 Session 守护
- [ ] 4.19 实现开发者 API（供 Yuan Mod 调用）
- [ ] 4.20 实现攻击溯源地图（`/overlay map` 可视化）
- [ ] 4.21 实现玩家行为信誉（风险评分自适应）
- [ ] 4.22 实现白名单自学习（正常软件基线）
- [ ] 4.23 实现社区黑名单同步
- [ ] 4.24 实现遥测（匿名可选）
- [ ] 4.25 实现自定义规则引擎（用户脚本）
- [ ] 4.26 实现回放引擎（攻击记录 → 离线 re-run）
- [ ] 4.27 实现 PEB 反调试标记监控（BeingDebugged/NtGlobalFlag/HeapFlags）
- [ ] 4.28 实现父进程 PID 校验（伪造父子进程检测）
- [ ] 4.29 实现 VMCALL 洪泛防御（>1000 VM Exit/s 限速）
- [ ] 4.30 实现 NPT 物理绕过防御（监控直接物理页写入）
- [ ] 4.31 实现安全模式绕过检测
- [ ] 4.32 实现自身哈希校验（定期 CRC/哈希）
- [ ] 4.33 实现 VM Exit 死循环看护
- [ ] 4.34 实现模块兼容列表（已知冲突 mod 检测）
- [ ] 4.35 实现开发者模式（debug 日志 + dry-run）
- [ ] 4.36 实现信任链建立（Hypervisor ↔ Yuan Mod 双向验证）
