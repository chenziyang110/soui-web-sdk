# 固定 Miniblink DLL 的链接参数核对

对构建时保留的 x86/x64 `build.binlog` 使用 MSBuild 17.14.40.60911 回放，从各自唯一的 `link.exe` 调用提取库参数。日志、链接命令和输出 DLL 的 SHA-256 以及原顺序库参数见 [observation.json](observation.json)。输出 DLL 的散列与[二进制授权待审阅清单](../../../third_party/manifests/license-review.json)及[直接动态导入记录](../kernel-imports-2026-09-25/README.md)一致。MSBuild 官方支持将 `.binlog` 作为输入回放；回放不会重新编译。[命令行参考](https://learn.microsoft.com/en-us/visualstudio/msbuild/msbuild-command-line-reference?view=vs-2022)

两个架构的链接命令各列 36 个 `.lib` 参数，均显式列出 libcurl、zlib、OTS、libxml、Harfbuzz、V8、Skia、JPEG 和 PNG 等库。x86 命令还显式列出 `openssl.lib`，x64 命令没有这一**显式参数**。这并不能证明 x64 二进制未使用 OpenSSL：默认库指令、其他库和运行时加载仍需核对；同样，命令行列出某库不证明其所有目标文件都进入最终 DLL。

原始 `build.binlog` 留在本机构建目录，可能含绝对路径，因此不随公开源码提交；其散列可供本地重放核验。要关闭许可与维护门槛，还须记录实际链接成员/符号、完整运行载荷、随包许可声明、适用再分发条件和维护责任。此记录不作法律或安全结论，`releaseReady=false`。
