# preview.4 固定内核回归

日期：2026-09-23。`0.3.0-preview.4` 已将此前重编译的 Miniblink 候选放入两架构固定交付路径。x64 `node.dll` SHA-256 为 `0801aa929336168c851df4a3f245f5984a88bd01af585aea752404fa07774816`，x86 为 `3b9071413f704958fb5708b8bb065785a6c3ed03b7fd0fb04f5952201e5fe4f7`。依赖散列和授权待审状态见 `third_party/manifests/`；替换决策见 [ADR-0007](../../../docs/adr/0007-rebuilt-miniblink-preview-binary.md)。

固定 DLL 在本机回环 HTTPS 服务上通过[双架构共 12 项正负向测试](tls-validation.json)：未受信链与错误主机拒绝、显式信任后的正确主机成功、宿主回调无法关闭两种校验、重定向至错误主机拒绝。[测试证书](cert.pem)可核对本次报告散列；临时私钥未归档。旧固定二进制的失败对照留在[历史候选归档](../tls-kernel-2026-09-23/README.md)。

x64 的[完整应用构建](x64/app-build.json)和[三个真实入口 smoke](x64/app-smoke.json)，以及[x86 构建](x86/app-build.json)和[smoke](x86/app-smoke.json)均通过。独立公共 SDK 消费者以默认固定 DLL 分别完成 reload、recreate、multiwindow、bridge 各三轮：原始结果在[x64](x64/build.json)和[x86](x86/build.json)相邻场景目录，运行身份和日志散列收录于[索引](../index.json)。两架构完整 SDK/Web/原生契约测试同样通过。本组仅为短回归；每种模式 1,000 次、资源门槛、8 小时、目标设备、实际信任根与代理仍未完成。`releaseReady` 为 false。
