# 模板滚动条：独立项目生成与双架构预检

本分支只把已在 Workbench 使用的 WebKit 滚动条外观规则补进 `templates/desktop-app/web/src/style.css`，并说明新项目可在该文件调整品牌样式。SDK Core 没有修改。旧模板样式的 1,252 字节作为新文件的原样后缀保留；新 CSS SHA-256 为 `643a962a4a4a62d0ce442bd0911add9508a926bcff505416c25fd907eda7b8f5`。

2026-09-30，在宿主无 QEMU、SOUI、编译器和桌面测试进程时，使用仓库外 `New-Project.ps1 -SdkPackage` 生成 `ScrollStarter`。输入是已冻结的 `0.3.0-preview.15` SDK 包，manifest SHA-256 `44839f856df8d9542231e7bcd38af4ca9a37663aa8d4e6fb755868faacb076da`。生成器原生退出 0、stderr 为空、三次负载门禁为空。独立复核器核对生成后的 CSS、项目元数据和 SDK 锁，并调用公共 `verify-project` 逐一核对 419 个已安装 SDK 文件；原始退出 0。此前三份忙碌或夹具散列错误的失败样本留在仓库外，没有改写或充当生成通过。

生成项目的正式 `Build.ps1` 在现代 Windows 上依次完成 x64 Release 全量 Web 和 x86 Release `-SkipWeb`。两个构建驱动、原生命令和随后 `verify-build` 均退出 0；构建前后门禁未见外部桌面负载。两架构使用同一份 433 文件源码，SHA-256 `edece3019fd29db841425671071367eb13e891d5cefb85a1d70594b9bd3cc227`。x64 buildId 为 `5a5224a2-883d-41da-ac2a-45c7b010413e`，71 文件运行载荷 SHA-256 `1a361461fc0c9837f7fa3a15840ab5a2b0fbaf0deee719c53852e6f84ee73a41`；x86 buildId 为 `24283c4e-d1fa-4f1a-b82e-6065ec1d7853`，71 文件载荷 SHA-256 `5373740f752475f283854413e349db6c9eb3614364aecaffe74b4b9335b9fc30`。打包 CSS 确实包含 WebKit 滚动条规则；两载荷各含五份 `resources/legal` 文件。模板 Web 锁文件的生产依赖审计原始退出 0，报告零项漏洞；这不构成授权结论。

两份载荷分别复制到仓库外运行目录并逐文件复核。x64、x86 `--smoke-test` 均原生退出 0，日志按顺序出现 `process-start`、`host-started`、`application-ready`、`host-close-complete`。运行后 71 个载荷文件仍与正式构建逐字节一致，仅增加应用自己的 `smoke.log` 与 `source-navigation.log`。初版复核器漏列后一个正常日志而退出 1；两份原始失败 stderr 已保留，修正后的复核器还严格核对导航目标 URL，两架构均退出 0。

**可见窗口真实滚动仍未通过。** x64 和 x86 的第一次尝试都启动了应用并取得 PID/HWND/进程创建身份，但窗口在受控启动后都不是自然前台，驱动因此在发送任何滚轮前拒绝继续，记录原始退出 1、`nativeExitCode=null`，只停止自己的应用。没有截图、滚轮、拖动或键盘滚动成功证据，不能把双架构短 smoke 当作交互通过，也不能继承 Workbench 的旧 Win7 截图作为模板验收。后续需要在不干预其他桌面任务的条件下获得前台身份并复测现代 Windows x86/x64 和 Win7 SP1 x86；生产总门保持 `sourceDeliveryVerified=false`、`benchmarkValid=false`、`releaseReady=false`。

`raw/` 保存 63 份原始输入与退出日志、构建身份、复核结果和失败样本。`raw-manifest.json` 的 SHA-256 为 `f2d7499d0e7859680c8923542d4222c1afdc79ddbc2a33012ae952be54951791`。复核归档字节及 Git 暂存区：

```powershell
node verification/production-validation/template-scrollbar-2026-09-30/Verify-Archive.cjs verify --index
```

如仍有本机仓库外原件，可另外传入 `--originals C:\SOUI-SDK-production-validation\template-scrollbar-preparation-20260929` 比对原件。归档只证明上述独立生成、构建和短 smoke 范围，不证明 Win7、真实滚动、升级回退、长稳或 Electron 采用价值。
