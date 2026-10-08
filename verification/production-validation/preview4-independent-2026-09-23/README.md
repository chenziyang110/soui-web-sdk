# preview.4 仓库外项目接入

日期：2026-09-23。以 `0.3.0-preview.4` 源码创建 402 文件 SDK 包，执行包校验及源码匹配校验；由 `tools/New-Project.ps1` 在 SDK 仓库外的新目录生成项目，并验证项目锁。包清单、锁文件及核验状态在[机器报告](result.json)，对应原始文件为 [sdk-manifest.json](sdk-manifest.json) 和 [sdk-lock.json](sdk-lock.json)。

生成项目在 x64/x86 各自独立完成 Release 编译，[构建记录](x64-build-Release.json)与[32 位构建记录](x86-build-Release.json)绑定同一项目源码指纹。真实启动监督得到两个 exit 0，日志分别记录 `application-ready` 与 `host-close-complete`：[x64](x64-smoke.log)、[x86](x86-smoke.log)。两端运行载荷中的 `node.dll` 与 preview.4 固定内核散列一致。归档报告把机器绝对路径改成相邻证据文件名，并记录原始监督报告 SHA-256；未改写通过/失败字段。

此结果只关闭当前预览包的“生成项目、独立构建与短启动”切片。未验证真实业务小样、升级/回退后的再次编译运行、Debug、目标系统、长稳与授权；`releaseReady` 保持 false。
