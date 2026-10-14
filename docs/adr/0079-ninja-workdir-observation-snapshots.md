# ADR-0079：Ninja 工作目录变量与编译观测快照

- 状态：proposed
- 日期：2026-10-14

## 背景

一次普通 SDK Release 构建中的原生编译和整个 Job 已自然结束，但公开完整构建入口的观测阶段拒绝了 CMake 生成的九条 CUSTOM_COMMAND。原因是两个 Ninja 读取器只支持美元符号、空格和冒号转义，未支持 implicit output 路径中 CMake 声明的 cmake_ninja_workdir 变量。该次公开构建失败保持失败；原编译结束不等于资格通过，unit 尚未执行。

[Ninja 官方手册](https://ninja-build.org/manual.html)说明，路径变量展开得到静态路径内容，include 使用当前 scope，subninja 创建新 scope。变量展开后的冒号、空格和美元符号应保留为路径字面内容，不能再做一次词法拆分。

## 决定

PowerShell 普通 SDK 观测与 JavaScript measurement 观测使用同一有限契约。读取实际 impl、common、rules 三个原始文件，只接受 impl 对 CMakeFiles/common.ninja 的唯一顶层 include、common 对 CMakeFiles/rules.ninja 的唯一顶层 include，以及不含进一步 include/subninja 的 rules。common 在规则 include 之后必须有唯一顶层 cmake_ninja_workdir 声明；其他文件的声明、缩进 shadow、重复声明、foreign include/subninja 与声明前使用均拒绝。

声明右侧只按原有 Ninja 字面转义解码，结果必须是当前受绑定 buildRoot 的绝对本地路径并保留末尾分隔符。读取器接口同时接收 bound buildRoot 与已声明的 known value，仅接受该已知变量的花括号或直接引用。其他变量继续拒绝。展开内容直接追加为一个路径 token；原 raw/rawLogical 和 explicit/implicit/order-only/validation 分区保持。CUSTOM_COMMAND 仍完整解析，不能跳过来绕过错误。

这同时改变 compiler proof 与 measurement snapshot 的必需证据。measurement 写端必须写出 records.common、records.rules 和 ninjaWorkdirBinding；普通 SDK 写端必须写出 ninjaScopePins、ninjaWorkdirBinding，并把 common 与 impl/rules 一起纳入 evidencePins。三个 scope 文件在读取前后及封存前复核原始 bytes/SHA。消费端要求这些必需 pins、绑定目录、原始声明、include scope 一致，重新检查原始文件并复核读取前后 pins。当前标准消费与 prior reuse 均遵循新约束；缺少 common 的旧快照不会因新读取器而获得接受。

measurement-validation 的现有 seal 保存完整 observation，prepared 路径调用其 verifier；普通 SDK 的现有公开路径仍写入并消费真实观测。无需引入新的执行入口。原有 NativeNatural、tool identity、Job closure、真实 command/source/object/link/dependency 匹配和资格边界不变。

## 验证与后续

现有 PowerShell 与 JavaScript 纯 Reader 回归共享实际失败九行的 fixture，覆盖字面展开、未知变量、缺失/重复/缩进 shadow、foreign scope、声明顺序，以及 common 缺失或 raw pin 篡改。纯文件数据检查不执行编译器、SDK、Native、OwnedRunner、VM 或 GUI。原失败不会追溯提升；修复候选仍须由 Root 在 fresh SDK root 通过既有公开入口正常重跑 stock、普通 full ALL 和 unit。实际编译验证、Native 资格、兼容性和完整资源预算在此 Source 阶段仍未评估，未来实际 receipts 保持 null。
