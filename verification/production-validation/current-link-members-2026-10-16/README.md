# 当前内核链接成员与首方许可

本轮针对 Issue #39 复核 0.5.0 精确两架构内核。它们继续用于 0.5.1-preview.1，DLL 没有替换。原构建记录已固定 DLL 和链接 MAP 的 SHA-256；本轮重新读取原件并压缩保存完整 MAP，解压原字节一致。

| 架构 | MAP 符号行 | OpenSSL 符号行 | OpenSSL 对象成员 |
| --- | ---: | ---: | ---: |
| x64 | 339754 | 9468 | 486 |
| x86 | 302305 | 9488 | 489 |

逐架构 JSON 保存全部库/成员集合、PE 节、时间戳及来源。两份 MAP 的时间戳和加载基址与精确 DLL 一致；OpenSSL 符号的节偏移均映射到 DLL 地址，`SSL_new`、`SSL_connect` 另保存前 16 字节代码。符号数量不能解释为函数数量或运行覆盖率；相同代码折叠允许多符号共享地址。未标注来源的导入符号单独列为 `<unattributed>`，没有猜测归属。

导入源码版本头为 OpenSSL 1.0.2h；源码 LICENSE 与保留声明逐字节相同。此前六个 0.5.0 发布 ZIP 的声明检查见 [Issue 专项证据](../issue-maintenance-2026-10-16/README.md)，包含 OpenSSL 和原 SSLeay 条款。根据 [1.0.2h 上游原文](https://github.com/openssl/openssl/blob/OpenSSL_1_0_2h/LICENSE)，审阅需覆盖源码版权保留、二进制随附条款/免责声明、有关宣传和署名要求及名称限制；不能用新版 Apache 许可替换旧版本条款。

维护者在本轮明确选择首方 MIT，相关交付变更见 [ADR-0118](../../../docs/adr/0118-first-party-mit-delivery.md)。这完成首方许可选择，不表示所有第三方嵌套组件的条款和发布审批完成。本记录仅证明链接贡献和现有材料；`binaryLicenseReviewComplete=false`、`releaseReady=false`。

复核：从仓库根执行 `node verification/production-validation/current-link-members-2026-10-16/verify.cjs`。校验器直接解压 MAP、重新统计成员，并将示例地址绑定到仓库当前 DLL；不需要原开发机路径。历史缺件和失败记录保留。
