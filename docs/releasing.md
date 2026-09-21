# 交付与发布

当前版本只标记 preview，机器可读结果保持 `releaseReady: false`。

## 交付组成

- 完整源码包：SDK、模板、Demo、工具、文档、测试、第三方源码与固定运行依赖。
- SDK 安装包：通过 `tools/sdk/soui-sdk.cjs pack` 生成，包含逐文件身份清单。
- 可运行程序：构建后的整个应用输出目录。
- 验收记录：工具链、配置、测试结果、产物散列和未完成项。

`verification/sbom-*.cdx.json` 是从三个 npm 锁文件生成的 CycloneDX 依赖清单；原生依赖身份见 `third_party/manifests/dependencies.json`，底层源代码来源见各源码目录的 `SOURCE-MANIFEST.json`。这些清单不等同于漏洞已清零或授权审核完成。

使用 `powershell.exe -File tools/Package.ps1` 生成源码归档及 SHA-256。归档排除 out、node_modules、缓存和源码树中的编译产物，保留 prebuilt 下必要 DLL / LIB。编译工具和 npm 包需要另行准备。

通过 Release 启动测试后，可运行 `powershell.exe -File tools/Package-Applications.ps1 -Architecture x64` 生成模板与 Demo 的免安装运行包；x86 同理。它校验 EXE 和内核与验收记录一致，不执行代码签名。

交付前验证 SDK 独立构建、模板/Demo 四组配置、安装升级、生成项目独立构建、外部 CMake 引用、源码包完整性、底层依赖源码构建。正式发布还需完成 known-issues.md 中适用门槛。

签名证书、产品品牌、安装包、更新服务器与回退策略由实际产品配置。包内没有签名私钥、伪造签名或成品安装器。删除 preview 标签不代表发布验收完成。
