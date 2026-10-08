# 固定 Miniblink DLL 的直接动态导入

对 `third_party/manifests/license-review.json` 所指的 x86/x64 精确 DLL，使用 `llvm-readobj` 22.1.8 读取 PE 导入目录；文件大小、SHA-256、工具版本和原始 DLL 名称列表见 [observation.json](observation.json)。两份 DLL 的哈希均与待审阅清单相符。x86 列出 17 个直接导入 DLL 名称，x64 列出 19 个；后者额外直接导入 `WININET.dll` 和 `gdiplus.dll`。

这个静态观察只界定 **直接 PE 导入目录**。它无法列出静态链接源码、延迟导入、运行时加载、组件版权归属、可再分发文件或条款；也不能因为某 DLL 未在 x86 直接导入列表中，就断定运行时不会使用它。精确二进制链接组成和随包声明仍须结合构建配置、链接输入、实际交付包及许可文本审阅。`releaseReady=false`，本记录不构成授权或安全准入通过。
