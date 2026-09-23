# 0.3 独立项目接入证据

日期：2026-09-23。源码候选指纹 `b565bd098865886d0382321eb43a5df9177fa99d1a395c094f592746a3f5724f`。这是固定 prebuilt、默认软件呈现的 SDK 包接入验证，不是 GPU 候选或正式发布验证。

1. 从源码执行 `node tools/sdk/soui-sdk.cjs pack --source . --output dist/sdk/0.3.0-preview.3 --version 0.3.0-preview.3`，得到 401 文件的 preview 源码包；[包清单](sdk-manifest.json) SHA-256 为 `eaa18c4a1b9d6655b1f482a320a9fda321997bc298e8680877dcaba83e2805d4`。[包校验](package-verify.json)及[源码匹配校验](source-verify.json)通过。
2. 使用 `tools/New-Project.ps1 -Name SdkConsumerB565 -OutputRoot F:\work\SOUI_WEB\out\SdkConsumerB565 -SdkPackage <上述包绝对路径>` 在 SDK 仓库外生成项目。项目[锁文件](sdk-lock.json) SHA-256 为 `27a315b3c57f972731f924339dfbd1759f3721bcfa3fe801071e40d7d10599eb`；[再次校验](project-verify.json)通过。绝对路径仅说明这次运行的位置，复跑时可换成任意新目录。
3. 在生成的项目根目录分别执行 `tools/Build.ps1 -Architecture x64 -Configuration Release` 和 `-Architecture x86 -Configuration Release`，均成功。[x64 构建身份](x64-build-Release.json)与[x86 构建身份](x86-build-Release.json)显示相同生成项目源码散列 `3ceb874b28f81eb81cff875b9e326f66af3a5150399ffc3d3129966f74ecc6b5`，各自产物散列为 `8ee48cf41c4ec4ab6d6c4a822951a4efa6de58cb025c1d152db2b965c7ec2e71`、`b352c2bc752255baf16c47bc6df95dc03bc861ebc843d9280e2e80bfb0a90a33`。
4. 两架构的 `SdkConsumerB565.exe --smoke-test` 均由监督进程等待退出并得到 exit 0；[x64](x64-smoke.log)、[x86](x86-smoke.log)均记录 `process-start`、`host-started`、`application-ready`、`host-close-complete`。

这关闭当前 preview 包的“仓库外生成、双架构独立构建与真实启动”切片。它仍未验证业务小样、SDK 升级/回退后重编译运行、Debug、目标 Windows 版本、GPU 与长期资源门槛；清单的 `releaseReady` 保持 false。
