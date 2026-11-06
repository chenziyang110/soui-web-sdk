# 固定源诊断快照：未采用，未完成 64 输入验收

这是供开发审查、保存和后续显式集成的源码快照，不是新 SDK、安装包、默认基准目标或可自动运行的验收包。复制没有改变任何源码字节；原失败、日志、冻结请求和二进制仍留在原证据目录。本目录不含 EXE/DLL、构建输出、旧启动器或依赖本机绝对路径的操作脚本。

目录分工：

- `consumer/`：R35 single-bootstrap R2 的项目 CMake 与 Host 编译所需源/内部头，只构建消费者。没有把外部实验 CMake 覆盖仓库原 benchmark 默认，也没有补回或执行旧 ProfileContracts。
- `runtime-page/`：R37 v4 固定页面、原 trace/reader 源与 `dom-capability.test.cjs`，共六项。页面的 `burst-config.js` 由消费者用受校验的 run/kind 生成，不在快照中伪造运行数据。
- `coordinate-tool/`：R38 根 CMake 与七个工具源/头，目标 `BurstInputCoordinateEvidence` 只含输入器与原 observer 两个 TU。原 normalizer、DPI setter 时序、坐标网格、125 Hz/64、NOCOALESCE、原 ACK/关闭期限和硬门均未修宽；新增内容只是本轮已经冻结的冷坐标环境及固定 POD 旁证。
- `matched-private/engine_admission.h`：6064 字节的指定私有头，SHA-256 `683f851d3785d02dd27f2732df3b0137771d2175bcc09096f1ad21abd6606cb2`。它只随本次已安装 SDK `0.7.0-preview.4`、源码身份 `e1d…`、build `18e47fa2-69df-4530-b1f9-365f6b2aec28` 的匹配库使用，不是新的公共 ABI，也未认证其他 SDK 版本。

`source-inputs.json` 以本目录为目标根列相对路径、字节数、SHA-256 与归档来源，并记录每个复制文件与冻结原件逐字节相等。私有头来自被冻结收据描述的仓库私有文件：其 `originArchiveRelativePath` 指向归档中的 pin 记录，另有 JSON 指针和仓库相对来源，不能误读成归档里另有一份原始头文件。清单不包含自身散列以避免循环；README 是本次新写说明，不冒充历史原件。

历史二进制来源仅作为 provenance：R2 Host 为 `c4320c14cc641b904e920bd6a1eff918cb002a560e430837b4a0fde9a9339aaa`，R38 工具为 `850566483a1c211ee42b5c365f27f62a226c8bf24ad72b9c3f29ef0d65058777`。两者不随快照复制，也不证明本目录重建后的二进制通过；源码中的旧历史注释同样不是新地点的运行指令。

事实边界保持：

- R37 页面 CPU 证据是原首轮五项通过、一项失败，修正 mock 后仅补跑该一项通过；不是一次最终整组 6/6。
- R35 早期双 bootstrap、前台交接及后续运行失败全部保留。本轮冷握手/模块/局部构建成功不能替代正输入资格。
- R38 实际仍为 64 次发送但子窗口仅 56 个 tagged 消息，且部分收到的原生坐标已有 1 px 偏差；完整 64 的 T1→DOM ID→首槽→软件 capture 因果验收未通过。不得将该快照、部分链或 CPU 结果写成输入延迟、物理显示、帧率、Electron 对齐或 SDK 性能收益通过。
- 不把缺失的 `isTrusted` 改成 true；不按近时钟补 native ID；tag0、多 DOM、错误、loss、CRC/nonce/前台等原硬门保持。未来 cell/坐标修复尚未实施，不包含在此快照中。

后续若决定重新构建，应在独立输出中使用仓库现有标准 MSVC v143/Windows SDK 19041 流程与 `tools/internal/Msvc-CompilerLifecycle.ps1` 的隔离、准入和自然收尾规则，并显式指定匹配的已安装 SDK 与私有头位置。不得复制旧成功状态、绕过原 guard、重编或覆盖冻结 SDK；新二进制、页面、工具与请求必须重新绑定实际身份。此说明不触发构建、测试或运行。
