# HTTPS 身份校验候选验证

日期：2026-09-23。仅针对重新编译的 Miniblink 候选；固定 prebuilt 没有替换。x64 DLL SHA-256 为 `0801aa929336168c851df4a3f245f5984a88bd01af585aea752404fa07774816`，x86 为 `3b9071413f704958fb5708b8bb065785a6c3ed03b7fd0fb04f5952201e5fe4f7`。编译时 `WebURLLoaderManager.cpp` 工作树原始字节 SHA-256 为 `16e95cf1fe4f2e13429bc300e77062965bb1dfab28be243784c905415ea48079`（CRLF），Git 规范化源码 SHA-256 为 `cc873d772f9cf8057bb501b13b2605f43d3ccda7d5ba248cbc3b28c73ac2edcb`（LF）；对应[补丁 0006](../../../third_party/patches/miniblink/0006-verify-tls-peer-and-host.patch)。[构建日志](x64-kernel-build.log)与[32 位构建日志](x86-kernel-build.log)留存。

使用 `tests/diagnostics/https_server.py` 在本机回环地址提供测试页面，证书临时生成于忽略目录 `out/tls/`：自签、SAN 为 `DNS:localhost`，私钥没有归档。[公开证书](cert.pem)及 [12 项原始结果](validation.json)绑定这次运行。`tools/Test-TlsKernel.ps1` 对 x86/x64 各执行六项：未受信链拒绝、显式信任且主机匹配成功、主机不匹配拒绝、宿主回调分别尝试关闭证书链或主机名校验仍拒绝、从匹配主机重定向到不匹配主机仍拒绝。十二项均通过；对应 libcurl 错误分别为 60 和 51。测试不使用外部服务或生产凭据。

复跑时先用 OpenSSL 在 `out/tls/` 生成临时 RSA 测试密钥与证书（`-subj /CN=localhost`、`subjectAltName=DNS:localhost`、`basicConstraints=critical,CA:TRUE`），用 `tools/internal/Build-TlsProbe.cmd` 分别编译 x86/x64 诊断程序，再运行 `python tests/diagnostics/https_server.py out/tls/cert.pem out/tls/key.pem`。服务就绪后将两个诊断程序、候选 DLL 和证书路径传给 `tools/Test-TlsKernel.ps1`；运行结束后关闭本地服务并删除忽略目录中的私钥。回环服务与测试证书只用于验收，不作为 SDK 的生产信任根。

同一修改的[公共 SDK 消费者 x64](bridge-x64/result.json)和[x86](bridge-x86/result.json)各完成 Bridge 3 轮并正常退出，构建身份和逐步日志在相邻文件中。源码/工具指纹为 `0128d6dcb5c60518a22cdefac4c1c0d7663a2719bad7a1da22bed28155b12e44`。

本测试确认指定本地证书和这两个候选 DLL 的校验行为，不证明系统信任根交付、代理策略、远程服务兼容或旧版 TLS/浏览器组件的漏洞维护。固定 prebuilt 仍为原始二进制；必须完成候选打包、完整回归、信任根更新机制和安全维护审查才能考虑生产准入。`releaseReady` 保持 false。

同一回环服务下的固定 prebuilt 对照更明确地说明当前发布包不能获准联网：[x64](prebuilt-x64.log)和[x86](prebuilt-x86.log)均接受了未受信自签证书；[x64 错误主机](prebuilt-wrong-host-x64.log)与[x86 错误主机](prebuilt-wrong-host-x86.log)也被接受。它们分别绑定清单中的 DLL 散列 `8f155e8466e82099f64334bbc56db35368e0f85d44ed38e11a13768edd1af199` 和 `1233a8542de4f6891e09b14e49270ddb0dabea703e387fe256dbacb716dc5978`。对照记录保持原样，不被候选修复覆盖。
