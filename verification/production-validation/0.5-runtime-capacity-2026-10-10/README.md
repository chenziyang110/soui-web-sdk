# 0.5 Runtime Capacity 有限集成验证

2026-10-10 的 x64 / Win32 × Debug / Release 四配置验证均完成。每配置的配置、编译、CTest 三阶段均自然退出为 0/0，整组进程已关闭；每配置运行相同 29 个测试，合计 **12 个自然收尾阶段、116 项测试**，JUnit 无失败、错误或跳过。

| 配置 | 阶段 | 测试 |
| --- | ---: | ---: |
| x64 Debug | 3 | 29 |
| Win32 Debug | 3 | 29 |
| x64 Release | 3 | 29 |
| Win32 Release | 3 | 29 |

此目录保存有限集成证据。它不建立 mounted Host、资源泄漏或长期性能、Windows / PE 兼容性、完整工具链来源或发布资格；原始收据中的相关资格字段仍为 false。29 项的范围为 Runtime Metrics 11、Engine Capacity 8、Scope Capacity 10；可选 mounted Host 在本次计划中保持 OFF。

## 原始失败和修复范围

[最初 Debug 失败收据](evidence/root-initial-debug-failure-review.json)保留原结果：两架构配置成功，编译均自然退出 1/1，出现 CRT MDd/MD 与 iterator level 2/0 的 LNK2038；当时没有运行 CTest。两组失败 summary、四阶段闭合 Owned 原始记录、标准输出和错误输出均在 historical-failure/ 下，失败没有改写为成功。

修复只涉及三个 CMake 测试目录的八处字节变更：四个目标（含可选 Host）与 SDK 现有约定统一使用 MultiThreadedDLL（/MD）和 /U_DEBUG。没有新增 NDEBUG、修改测试断言、SDK C++ 运行算法或公开 ABI。Debug 仍是 Debug 配置，其运行库按现有 SDK 约定固定为 /MD。before / after 复本在 sources/；[DELTA](controls/DELTA.json)保留完整前后变更描述。

## 核验依据

[四配置 Root 实际收据](evidence/root-actual-review.json)保留原始 47,153 字节和绝对引用，SHA256 为 da9a930be679aa73f69a22d174825ba966c059b337d0bfed17f2702dac478ec4。[Root 静态收据](evidence/root-crt-static-review.json)与原计划、guard、4,640 字节 finite Owned driver、编译器选择文件一并保留在 evidence/ 和 controls/。

每配置的 summary、JUnit、LastTest.log、三个阶段的 ready、context、finished、原始 Owned、stdout、stderr 均在 actual/ 下。已核对实际 PID / 十进制字符串 FILETIME、命令参数、原 120 / 900 / 180 秒预算及 2 秒 drain、根进程与 supervisor 自然 0/0、完整 Job 关闭、没有强制清理、停止请求或回退。配置、编译和测试 Job 可包含编译子进程和 conhost；本次没有把它们套用单进程 Job 限制。

actual/ 中还收录十二组实际 CL / LINK command、LINK.read 与 CL.read 文本。实际测试编译命令使用 /MD、/U_DEBUG，配置名称正确；LINK.read 将各测试对象绑定到同配置正常 SDK archive。四个 SDK archive 与十二个 EXE 的真实长度 / SHA256 保存在 [binary-hashes.json](binary-hashes.json)，本目录不复制二进制、第三方大库或旧 MAP 巨型记录。该绑定不等于完整工具链来源证明。

## 历史输入与相对附件

四份历史计划各含 183 个输入 pin。独立文件读取在 **2026-10-10T08:55:18.6347317Z** 核对了 732 个引用、183 个实际文件，全部匹配；该时点在获准更新 README / ADR 文案之前。随后文档更新可能使这些历史 live 文档 pin 与当前工作区不同。本目录不把历史计划重新标为更新后的当前输入，也不要求执行所附 guard / driver 来“重证”既有结果。三个 CMake after 是本次修复的封存复本，不能代表整个工作区的后续状态。

原收据、summary、context 和命令中的绝对路径保持原文。[attachment-index.json](attachment-index.json)把原 pin 映射到本目录的相对文本附件，便于在另一路径审查；二进制引用只有哈希记录。额外收录的 CL.read 是已闭合构建跟踪文本，其清单 pin 记录复制时的真实字节。

[manifest.json](manifest.json)按 Ordinal 顺序列出每个相对附件的长度和 SHA256，覆盖原始复本及本目录生成的说明、附件映射和二进制哈希表；清单自身不自列。原始 Root bindings 未改写为相对路径。此包只记录已经闭合的有限验证，不产生新的通过资格。

本目录的局部 .gitattributes 禁用 Git 换行转换，提交时保留附件原字节及其 SHA256。
