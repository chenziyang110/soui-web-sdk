# preview.5 双架构非桌面回归

2026-09-25 在源码提交 `22c0c376d5d38054ad8e84a5e3456d71051a1650` 的工作树分别执行 `tools/Test.ps1 -Architecture x64 -Configuration Release -UnitOnly` 和 `-Architecture x86 -Configuration Release -UnitOnly`，两次均退出 0。测试使用工作树现有 `out/x64`、`out/x86` 的原生二进制；本轮没有重新编译这些二进制，也没有启动桌面窗口。

| 架构 | 包与交付身份测试 | Web SDK、验收及 benchmark 协议测试 | 原生 CTest | 原始日志 SHA-256 |
|---|---:|---:|---:|---|
| x64 | 9 通过 | 59 项：58 通过、1 跳过、0 失败 | 6/6 通过 | `C16B1C2A9BFD8D6130B8D0F37C975239B2B6555400FBBD88611EC07993039F13` |
| x86 | 9 通过 | 59 项：58 通过、1 跳过、0 失败 | 6/6 通过 | `FBE51C751009C3C0DBED079D14A949C1819AFEA3D60362A986583DD9ED51D588` |

原始日志分别保留在 `D:\SOUI-SDK-QEMU-validation\unit-regression-x64-20260925.log` 和 `D:\SOUI-SDK-QEMU-validation\unit-regression-x86-20260925.log`。两架构各跳过一项需要 x86 调试器的测试。这是公共 API、交付身份、页面协议和已有原生契约的回归证据；不能证明 V02 新页面在 WebView 中的实际交互、Win7 SP1 x86 客体兼容性、长稳资源趋势或双端性能。执行时宿主八小时测试和外部 QEMU 仍在运行，因此不作隔离性能结论。`releaseReady=false`。
