# YuanGuardHV 静态校验

本目录只做源码/文档一致性检查，不加载驱动、不触碰 hook 实机路径。

- `run_static_checks.ps1`：运行全部检查。
- `ioctl_parity.ps1`：C 头文件与 PowerShell/Java 客户端的接口编号一致。
- `command_parity.ps1`：README 与两个客户端实际命令一致。
- `safety_checks.ps1`：安全红线静态检查。

运行：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File run_static_checks.ps1
```
