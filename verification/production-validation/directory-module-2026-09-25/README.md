# 公共目录选择器：双架构无窗口验证

状态：公共接口、模板与仓库外消费者已编译，**真实对话框尚未运行**；`releaseReady=false`。本记录只覆盖开发树候选，不能替代 Win7 SP1 x86 客体、真实页面和 V03 整组验收。

SDK-only Release 构建记录中的源码指纹均为 `3e15daaf9b23ca695da52c42184454e23e21a2fe38749f7fd7c114169e1e238b`。构建入口为 `tools/Build.ps1 -Architecture x86|x64 -Configuration Release -SdkOnly -Install`；两个架构均验证 Web 产物并安装公开头文件及静态库。首次使用 `-SkipWeb` 的 x86 尝试因 Web 源码/产物身份未匹配而在构建前拒绝，随后使用完整构建入口通过。根源码包的逐文件清单另由 `source-archive.cjs create` 和 `verify` 更新/验证。

| 产物 | SHA-256 |
|---|---|
| 安装的 x86 `soui_sdk.lib` | `3F015997006B1518D67DF111D486D713B0F9516E3014AEE50FE1459C0467DC5C` |
| 安装的 x64 `soui_sdk.lib` | `DE650129DD43AC4483E99C6F122316952150A2BDFED0A7FEA0CB1A44C999FF81` |
| 仓库外 x86 `soui_consumer.exe` | `3B4EE9AD428DDD56C9AA446D9EBCAA5B7DCC5FB9CAF84E7C6B9B6AB845B07EBD` |
| 仓库外 x64 `soui_consumer.exe` | `5512ADE8CCFEE6E14E0D25DEB38548C6183D8EBC08F48A1731854870418F7EAD` |

`tests/package-consumer` 分别以 `out/install/x86` 和 `out/install/x64` 为 CMake 包路径，在新建的 `out/consumer-directory-x86` 与 `out/consumer-directory-x64` 目录配置、编译；两个 EXE 仅通过安装包公开 API 启用目录、剪贴板和窗口模块，并退出 0。x86/x64 模板 Release 也已重新编译，SDK Web 构建、模板 TypeScript 检查和经典页面构建通过。

x86/x64 的六项 CTest 各自全部通过，逐项记录分别见 [x86](ctest-x86.log) 和 [x64](ctest-x64.log)；提交前将 Windows 本地代码页日志转为 UTF-8/LF，记录内容不变。两份提交日志 SHA-256 为 `C040B7E2E4CEA4866B05E4DB30F00FBE43FF02A3E4607353FD0D2045CAD7CD94` 和 `B634B2ABCA3BFF8A76E92F75B840D1E235C00C6852C4EDB076B1B9BA29DE6A94`。目录契约覆盖显式启用/冻结、原生手势、伪造 JSON 手势、无效参数、无效窗口以及在 MTA 拒绝且不打开 UI。

尚需在可见模板页面点击按钮，验证真实目录选择、取消、中文路径和对话框打开期间的 Bridge 响应；在 Win7 SP1 x86 QEMU 与实际目标设备上重复，并检查导航、关窗、网络路径和受限账户。不能由这些无窗口结果得出生产可用结论。
