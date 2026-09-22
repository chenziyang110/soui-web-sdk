# 交付与发布

当前版本只标记 preview，机器可读结果保持 `releaseReady: false`。

## 交付组成

- 完整源码包：SDK、模板、Demo、工具、文档、测试、第三方源码与固定运行依赖。
- SDK 安装包：通过 `tools/sdk/soui-sdk.cjs pack` 生成，包含逐文件身份清单。
- 可运行程序：分别暂存、验证和压缩的最小模板与 Workbench；诊断日志、PDB 和未知用户文件不进入运行包。
- 验收记录：工具链、配置、测试结果、产物散列和未完成项。

`verification/sbom-*.cdx.json` 是从三个 npm 锁文件生成的 CycloneDX 依赖清单；原生依赖身份见 `third_party/manifests/dependencies.json`，底层源代码来源见各源码目录的 `SOURCE-MANIFEST.json`。这些清单不等同于漏洞已清零或授权审核完成。

源码维护者先将审查过的源码加入 Git 暂存区，再执行 `node tools/internal/source-archive.cjs create` 和 `node tools/internal/source-archive.cjs verify`。除清单自身外存在未暂存修改时生成器会失败。v2 清单从 Git index blob 的原始字节生成；验证先比较原字节，再允许文本 checkout 的 CRLF 转换，保留 Git 中原有的特殊混合换行。二进制不转换；上游导入清单中的历史缓存不决定发布内容。

使用 `powershell.exe -File tools/Package.ps1` 生成源码归档及 SHA-256。它先校验当前清单，将固定内容复制到新的 `out/package/source-*`，再次校验再压缩；不会隐式刷新已失效清单。解压后无须 Git 也可执行 `node tools/internal/source-archive.cjs verify`。源码改变后回到维护者 checkout 更新清单，不能在解压目录中无依据重建发布集合。编译工具和 npm 包需要另行准备。

通过 Release 启动测试后，运行 `powershell.exe -File tools/Package-Applications.ps1 -Architecture x64` 分别生成 `SouiStarter-<版本>-windows-x64.zip` 和 `SouiWorkbench-<版本>-windows-x64.zip`；x86 同理。它检查源码、构建 ID、完整运行载荷与所有启动场景，再从校验后的独立暂存目录打包。每个包伴随 SHA-256 和 `.zip.json` 大小报告，包含逐文件散列、原生程序/内核/其他运行库/Web 分类大小和压缩参数。不执行代码签名。

应用输出目录中的 `resources/` 为部署工具所有，重复构建会整体替换。产品数据必须放在独立用户数据目录；不要放入 `out/.../resources`。其他应用文件不会被资源替换操作删除；未识别文件会阻止运行包生成。

交付前验证 SDK 独立构建、模板/Demo 四组配置、安装升级、生成项目独立构建、外部 CMake 引用、源码包完整性、底层依赖源码构建。正式发布还需完成 known-issues.md 中适用门槛。

签名证书、产品品牌、安装包、更新服务器与回退策略由实际产品配置。包内没有签名私钥、伪造签名或成品安装器。删除 preview 标签不代表发布验收完成。
