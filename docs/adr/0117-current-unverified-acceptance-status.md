# ADR-0117：限定范围交付后的当前未验收状态

状态：proposed（已实现，待维护者评审）。关联 Issue #96。

## 背景

0.5.0 按维护者确定的限定范围交付，没有执行完整 Collector；根验收文件
仍指向 0.4.0-preview.3，并称没有新发布。发布事实与完整生产验收是不同
结论，不能为了更新版本而重跑已明确推迟的流程，或把限定范围结果当作完整通过。

## 决定

`Collect-Verification.cjs --record-unverified --evidence-reference <仓库相对路径>`
只记录当前 `VERSION` 的完整验收尚未完成。入口不接受其他收集选项，
不运行构建、测试或桌面进程；`scope=full`、`status=unverified`、
`collectionExecuted=false`、`sourceDeliveryVerified=false`、`releaseReady=false`。

引用必须位于 `verification/production-validation/`，是普通 JSON 文件，
版本匹配，且显式声明 `releaseReady=false`、`productionQualificationComplete=false`。
输出仅绑定其相对路径、字节数与 SHA-256，并标记 `referenceOnly=true`；
不复制其通过计数，不验证其外部原件，不赋予运行、完整交付或生产资格。

替换前将原验收文件按原字节保存到 `verification/history/acceptance-<SHA256>.json`，
已有不同字节、链接、非法引用或无效 JSON 均拒绝。旧报告归档成功后才原子替换
根文件；重复执行继续保留历史链。文件目录写入要求与其他收集操作串行，
本入口不提供与另一个并发 Collector 协调的协议。
历史目录使用 Git 的 `-text -eol` 属性，避免暂存或检出时改变已绑定的 BOM/换行字节。

完整和 foundation 收集的参数、证明要求与执行语义保持不变。工具及报告
更新后依照原流程刷新 Git 索引源码清单；已经发布的包和历史记录不替换。
新帮助模块放在 `tools/`，不进入 SDK Core 的托管工具目录，避免改变固定 SDK 内容。

## 备选、兼容与回退

直接手改根 JSON 容易再次遗留旧版本或误写通过；运行完整 Collector 又与
本轮限定范围不符。新入口使“明确未验收”成为可重复执行且保留来源的操作。
它不改变 SDK、应用、ABI 或依赖。回退时还原工具与根状态文件；历史报告保留，
但不得将旧版本的成功记录解释成当前通过。

## 验证

通过真实 CLI 的临时仓库检查版本更新、成功字段不继承、原字节归档、
重复历史链、错误版本/资格声明、缺失/损坏输入、非法选项、目录越界和链接。
原 Collector 档位及发布读者回归继续执行。该测试范围不授予原生或发布验收通过。
