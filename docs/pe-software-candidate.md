# PE 软件内核候选

`0.4.0-preview.1` 固定 SDK 保留原内核。其运行时默认软件呈现，但构建包含 GPU 支持，仍静态导入 OPENGL32.dll；在已测试的 Setup PE 3.1 x86 中因此无法启动。

本轮从同一 Miniblink 源码和已验证的底层静态库隔离构建无 GPU 候选。x86 内核 SHA-256 为 `156b4de591aada49994181dbc3200c9da8376c1aeeec06d2eb440a3ddcc349b0`，大小 22,225,408 字节，导入表已无 OPENGL32.dll。本机、Win7 SP1 x86 及 Setup PE 3.1 x86 均已执行 Starter 就绪/关闭测试并退出 0；PE 的中文页面及 C++ 问候也已显示。

x64 候选 SHA-256 为 `f618c0a765065fdc6afe77d4c8a33a132cdfdf566dc7f8be976621c2e5748f4f`，大小 35,009,024 字节。其本机短启动和 Setup PE 10 x64（10.0.17134，安装镜像版本 17134.228）中的就绪、关闭、退出 0 已验证；导入表也无 OPENGL32.dll。该结果不代表 Win7 x64 或其他 PE 镜像已经测试。

证据见[当前验证](../verification/production-validation/preview04-2026-10-08/README.md)的 `pe31-software/`。PE 结果采用来宾截图观察，Win7 另有串口完整导出。该候选不自动解决字体、输入法、目录选择器、剪贴板、网络和各类精简 PE 的组件缺失；仍须按产品所用镜像验证。

本机 x86/x64 公共 SDK 独立消费者分别执行了重载、重建、三窗口和 Bridge 各 3 轮，全部正常退出。结果与实际候选 DLL 散列绑定，位于 `pe-public-x86/`、`pe-public-x64/`；这些短测不替代目标 PE 上完整业务及长期测试。

## 使用范围

候选运行包与固定 SDK 包分别提供。候选不能冒充同版本的新固定 SDK，也不应在已锁定项目中手工替换 `third_party/prebuilt`。本轮用于验证 PE 路径；正式纳入 SDK 包管理需要新的固定版本、组件清单及完整交付验收。

解压候选到可写目录，运行 `SouiRuntimeDoctor.exe` 获取环境报告，运行 `SouiStarter.exe` 查看示例。也可从该目录运行 `SouiStarter.exe --smoke-test`；退出 0 且 `smoke.log` 包含 `application-ready` 和 `host-close-complete` 才是本机短启动通过。诊断报告中的 `compatibilityStatus: unverified` 是刻意保留的范围声明。

## 可追溯重建

完整源码包已包含对应源文件及 `tools/internal/Build-KernelMainArch.cmd`。先按现有流程构建指定架构的 Miniblink 静态库，然后在 `.cmd` 脚本中调用该入口，第五个 GPU 库目录参数使用显式空字符串，输出到一个新的隔离目录：

```bat
call tools\internal\Build-KernelMainArch.cmd "%VSDEV%" "%MINIBLINK_SOURCE%" "%ISOLATED_OUTPUT%" "%NATIVE_LIBRARIES%" "" x86
```

这些变量分别指向已检测的 VS 开发环境入口、完整 Miniblink 源码、新输出目录和该架构静态库目录。不能把 x86 和 x64 的库或对象混用。重建后核对退出码、源编码告警、DLL 导入表、输入与输出散列，再进行目标系统验证；`GpuExperiment=false` 不能替代实测。

既有项目的 x86 Release 输出名为 `node.dll`，x64 为 `miniblink.dll`；部署候选时都按公共加载器约定命名为 `node.dll`，必须保留原始输出与部署文件的同一散列。

源码仍有既有上游编译告警（本次包含 libxml C4789），旧内核维护、安全及再分发审核继续按[已知问题](known-issues.md)跟踪。候选保持 `releaseReady=false`。
