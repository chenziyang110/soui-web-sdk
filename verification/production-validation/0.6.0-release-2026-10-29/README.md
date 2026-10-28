# 0.6.0 限定发布证据

[RELEASE-FACTS.json](RELEASE-FACTS.json)记录实际范围，[独立审查](FinalReleaseFactsIndependentReview.json)核对原件、范围、资格标志与公开数据。最终 tag/tree、完整对应源码 ZIP 与上传身份由发布附件 Release-Manifest-0.6.0.json 记录，避免源码归档自引用。

- 当前默认应用两架构共八次启动、Bridge 与自然关闭，公开校验器通过。
- 四个生成诊断消费者、五个当前诊断阶段均自然退出；原生授权、Console 与 MiniBlink x86 TSX 断点/单步观察见 runtime/gui-observations 原图。C64 Console 来自原超时尝试，后续自然结束观察独立记录。
- 两次 C64 操作超时、初始工具加载失败、编译辅助 Job 失败及准备错误原件保留，不被后续通过改写。
- WebView2 SDK attached/paused/visible 保持 unknown；不声称暂停中关闭、完整面板或全部系统资格。

initial-scoped 为早期快照；final-scoped/Inputs.index.json、closing/Files.index.json 与 runtime 各组 Files.index.json 映射逐 raw SHA 原件。原件内部绝对路径保留为历史采集元数据，不代表读者机器路径。

完整生产、D07/D13/D14 与环境资格仍未完成，见[长测交接](../../../docs/testing/v0.6.0-long-run-handoff.md)。releaseReady、productionQualificationComplete、fullDevtoolsGoalComplete 均为 false。
