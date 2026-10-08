# preview.15 提交前 JavaScript 依赖公告核查

本目录保存 22 份原始记录及逐字节清单。三个当前锁文件分别运行生产依赖和完整依赖的 npm audit；六条原始退出码均为 0，stderr 为空，报告的漏洞总数均为 0。
核查输入为当前工作树中的 SDK Web、Starter Web 与 Workbench Web 锁文件；review.json 固定各锁文件和输出的 SHA-256。
结果只反映执行时 npm 公告服务的答复，不覆盖原生 DLL、安全边界、许可与未来公告；生产准入仍为 false。
