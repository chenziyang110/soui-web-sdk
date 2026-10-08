# 当前 preview.15 内核的直接 DLL 依赖

本次用 MSVC `dumpbin /dependents` 读取当前 x86 `node.dll` 与 x64 `miniblink.dll`。输入文件 SHA-256 分别为 `2ed793ccccb0642df9916128e36aece733efd34ad6acb4e50caa0622a4c25ef3`、`fe4160a70587d027504c409b8fd2bc5c87d78392811bff0c969f69c41a44d951`，与[待审二进制清单](../../../third_party/manifests/license-review.json)一致。两次 `dumpbin` 原始退出码均为 0，原始标准输出、错误、工具/输入散列和收集脚本保存在 `raw/`；`raw-manifest.json` 列出逐文件散列。

x86 报告 17 个、x64 报告 19 个直接 DLL 依赖，均为 Windows 系统 DLL 名称。这个 PE 导入视角**不包含**静态归档中实际进入 DLL 的目标文件、运行时动态加载或适用的再分发条件，也不能用来排除嵌入式第三方代码。此前[链接命令记录](../kernel-link-inputs-2026-09-25/README.md)对应更早的二进制 SHA，不能当作这两个当前文件的精确链接成员证据。依赖授权维持 `unverified`，`releaseReady=false`。
