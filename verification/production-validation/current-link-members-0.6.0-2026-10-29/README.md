# 0.6.0 精确内核链接成员工程记录

本目录针对 Issue #39，从已经完成的 0.6.0 两架构链接 MAP 提取全部符号来源及库/对象成员集合，未重新编译、运行 DLL、应用、VM 或长测。原 0.5.0 的成员记录和 `third_party/manifests/license-review.json` 保留原身份。

| 架构 | MAP 符号行 | OpenSSL 符号行 | OpenSSL 对象成员 |
|---|---:|---:|---:|
| x64 | 342174 | 9475 | 486 |
| x86 | 304462 | 9496 | 489 |

完整 MAP 以无损 gzip 保存，解压字节及精确 DLL 与已有 `0.6.0-source2-kernel-2026-10-29/{x64,x86}/summary.json`、`build-result.json` 及当前交付 DLL 的 SHA-256 对应。x64 continuation 的实际 DLL/MAP 位于原 run2 输出目录，记录不将它误写为 run3 的新文件。两份 MAP 的时间戳、首选加载基址与 PE 一致；非零节符号地址逐行与 DLL 节 RVA/偏移核对。零节符号单独计数，不宣称它们落在 DLL 节内。OpenSSL 的全部符号均位于已核对地址，`SSL_new` / `SSL_connect` 另保留前 16 字节机器码。

`<direct>` 与 `<unattributed>` 保留原始区分，不猜测所属嵌套组件。符号数量不等于函数数量、全部库成员数量或运行覆盖；相同代码折叠可使不同符号共享地址。该工程清单不能证明传递依赖/嵌套许可已穷尽。OpenSSL 版本来自同一冻结源码清单中的 1.0.2h 头文件；源码 LICENSE 与随附许可文本逐字节相同，不据此生成法律审核或再分发批准。

`Collection.actual.json` 记录本次实际 Node 映像身份、argv、cwd、收集/复核脚本及输出 pin。`Files.index.json` 核对目录完整文件集，只排除自身以避免自引用散列；原始 MAP、收集器、复核器和说明均纳入。复核器独立使用 token 化解析重新提取成员，不需要原开发机的 MAP 绝对路径：

```powershell
node verification/production-validation/current-link-members-0.6.0-2026-10-29/verify.cjs
```

`collect.cjs` 是该次收集入口，读取原构建摘要中的精确输入路径；存在输出时拒绝覆盖。它不作为无需原始构建产物的重建命令。

`binaryLicenseReviewComplete=false`、`transitiveDependencyInventoryComplete=false`、`releaseReady=false`、`productionQualificationComplete=false`、`fullDevtoolsGoalComplete=false`。完整嵌套组件/版本/条款审核、责任人与维护者审批、目标环境及生产门槛仍待完成，详见[许可交付说明](../../../docs/licenses/v0.6.0-redistribution.md)。
