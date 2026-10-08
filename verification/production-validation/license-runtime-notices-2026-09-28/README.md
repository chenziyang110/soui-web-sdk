# preview.15 当前内核与运行包声明材料观察

本记录针对 `codex/sdk-v02-continuous-task` 的 `4ade239018969b1740efa5a3349c0392c4c552bd` 输入，不修改 SDK Core 或任何应用载荷。x86 当前 `node.dll` SHA-256 为 `2ed793ccccb0642df9916128e36aece733efd34ad6acb4e50caa0622a4c25ef3`，x64 为 `fe4160a70587d027504c409b8fd2bc5c87d78392811bff0c969f69c41a44d951`。本目录的 18 份脚本、报告、原始退出码和日志从外部现场 `C:\SOUI-SDK-production-validation\preview15-license-boundary-20260927` 复制，复制前后逐文件 SHA-256 一致；脚本的历史输入散列和运行状态以各报告为准。

其中 `Map-CurrentNoticeCandidates.py` 与 `Verify-CurrentNoticeCandidates.py` 是冻结的历史执行原件，包含当时机器的绝对路径；它们仅用于追溯本次证据，不是可移植的构建或发布入口。下一次新输入须另建记录，不能改写原件或将旧输入检查算作新候选通过。

[当前链接命令提取](current-link-command-extraction-v3.json)记录 x86 36 个、x64 35 个显式库参数；x86 显式包含 `openssl.lib`。[声明候选映射](README-current-notice-candidate-map.md)在显式搜索路径找到 x86 12 个、x64 11 个本地库名候选，并将 13 份唯一候选声明材料与当时的 Git 索引源码清单核对。生成和独立校验的原始记录分别在 `notice-map-attempt1/`、`notice-map-check-attempt1/`，两项真实退出均为 0。这里记录的是**候选路径和文件**，不证明链接器实际采用的归档及成员。

[当前运行包观察](README-current-runtime-notice-observation.md)在四份 Release Starter/Workbench 中复核精确 DLL 身份及每份五个 `resources/legal/` 文件。导入源码中的 OpenSSL 1.0.2h `LICENSE` 含 OpenSSL 与原 SSLeay 文本；四份随包声明集合中均未发现该源码文件或 `Original SSLeay License` 文本。独立校验的 [标准输出](notice-observation-check-attempt1/stdout.txt)、[标准错误](notice-observation-check-attempt1/stderr.txt)和[原始退出码](notice-observation-check-attempt1/exitcode.txt)保留，原始退出为 0。上游版本对应的 [OpenSSL 1.0.2h 许可文件](https://github.com/openssl/openssl/blob/OpenSSL_1_0_2h/LICENSE)与 [Miniblink 导入仓库的顶层许可证](https://github.com/Alberl-Lee/miniblink49-VS2022/blob/master/LICENSE)供维护者进一步审阅；顶层许可证不能代替嵌套组件审查。

这些检查只界定当前材料和发布缺口：显式参数不等于最终链接成员，x64 未显式列出 OpenSSL 也不证明它缺席；其他声明渠道、完整运行包再分发条件、首方许可和发布责任仍未确定。精确二进制授权继续为 `unverified`，`releaseReady=false`。后续按 [Issue #39](https://github.com/chenziyang110/soui-web-sdk/issues/39) 追踪实际链接解析、适用条款、下一版本随包声明及两架构安装/回退复核，不能用同版本覆盖已冻结 SDK Core。
