# 旧源码测试的严格 UTF-8 编译投影

`scheduler_continuation` 和 `window_close_lifetime` 明确以 CP936 编译生产片段；clang-cl 22 拒绝 `/source-charset:.936`。在显式 `LLVM22MSVC142` 档位中，两者仍由原 generator 生成原始字节及 `source-binding.json`，再把这些源绑定副本投影到独立 `generated-utf8/` 编译。MSVC 路径保留原编码选项。

投影先核对完整生产源 SHA、实际字节范围与原片段 SHA，再严格 CP936 解码，并要求重新编码逐字节返回原内容。非法/不能往返的混合编码明确失败；输入、输出拒绝链接和额外文件。`projection.json` 保存原始/UTF-8 字节数与散列，原 `generated/`、生产 vendor 源码及 fixtures 不改写。仅表示相同 Unicode 源文本的另一种编译表示，不扩大生产内核的资格。

原 `/W4`、`/WX`、原始 source/HEX 守卫和运行行为断言保留；投影成功不是测试运行通过。独立运行叶项目时仍需要项目显式 LLVM 工具链和固定 PowerShell 文件身份输入；不能将默认的 CP936 MSVC 入口直接交给其他编译器。
