# 交付与发布

0.5.0 按下面限定范围进行预发布交付，实际结果见[发布说明](releases/0.5.0.md)。完整生产验收未关闭，机器可读结果保持 `releaseReady: false`。

## 0.5.0 本次发布范围

按维护者 2026-10-16 的要求，发布前采用最小充分验证集合：最终两架构四配置构建、每架构 Release 一次关键回归、SDK 外部消费者、交付包解压启动、现代后端本机 100 Hz 实测和交付清单检查。单次完整原生回归已经覆盖的用例，不再经另一个入口重复全量执行；断言、超时与失败判定保持不变。底层二进制导入仍须核对架构、来源、导出和清单。

8 小时长会话、千轮生命周期、完整性能对照与重复搬移全编译留作后续验证。发布记录必须列出已完成项、实际样本、未完成项和已知失败，未执行项不得标为通过；短时 100 Hz 测量仅支持该设备和样本范围的结论。本轮通过的是自动触发 S05 动效的两分钟真实页面调度诊断：窗口绑定当前 100 Hz 显示路径，requestAnimationFrame 中位间隔 10 毫秒。严格桌面截图采样因累计帧数大于一而失败，实际输入采样也未完成有效场景；这些失败保留，本轮不声称合成画面、实际输入响应或物理帧率认证通过。此范围调整不把 `releaseReady` 或完整生产验收状态手工改为通过。

本轮独立接入使用公开 `tests/package-consumer` 对当前已安装 `Soui::Sdk` 分别编译和运行，现代高刷采集另以外部宿主引用当前 `Soui::WebView2`。完整 `Verify-Release.ps1` 的多阶段升级/回退与搬移演练不重复执行，既有记录保留原载荷身份。因此完整 Collector 所需的全部发布证明仍为待完成；使用范围明确的本轮结果汇总，不生成或复制一份虚假的完整验收通过报告。下面的完整流程继续供后续认证使用。

## 交付组成

- 完整源码包：SDK、模板、Demo、工具、文档、测试、第三方源码与固定运行依赖。
- SDK 安装包：通过 `tools/sdk/soui-sdk.cjs pack` 生成，包含逐文件身份清单。
- 可运行程序：分别暂存、验证和压缩的最小模板与 Workbench；诊断日志、PDB 和未知用户文件不进入运行包。
- 验收记录：工具链、配置、测试结果、产物散列和未完成项。

除下面的原生清单外，`verification/sbom-*.cdx.json` 来自十个当前 npm 锁文件：Web SDK、React/Vue GUI 绑定、兼容和 Modern 模板、Workbench、兼容和 Modern 参考工作台，以及验收工具和 Electron 对照工具。后两项属于验证工具依赖，不属于 SDK 运行载荷；应用包版本独立于根 SDK 版本。生成使用 `npm sbom --package-lock-only --sbom-format=cyclonedx --offline --ignore-scripts --install-links`，版本冻结后需重新生成受影响清单。React/Vue 清单中本地 Web SDK 的 distribution URL 经核对对应 package 名称和版本后，转换为相对清单目录的 `../sdk/web`；保留原始生成记录，不把开发机位置写进交付清单。[CycloneDX 1.5](https://github.com/CycloneDX/specification/blob/1.5/schema/bom-1.5.schema.json) 的 externalReference URL 使用 `iri-reference`。

`verification/sbom-native.cdx.json` 独立列出实际复算散列的 136 个原生交付文件及两架构共 22 个内核静态链接输入。链接输入仅表示该 archive 参与真实链接，不表示每个成员都进入最终 DLL；这些构建输入也不全是运行时分发文件。来源分别绑定 `third_party/manifests/dependencies.json`、`kernel-build-050.json` 和当前实际构建报告。清单不含首方编译产物和完整传递源码组成，composition 明确为 `incomplete`，未知叶依赖没有伪装成空集合。`sbom-native.validation.json` 保存实际官方 1.5 schema 校验结果及 schema 散列。版本或载荷变动后须重新生成、复算文件散列并校验。所有清单都不等同于漏洞已清零或授权审核完成；完整源码另由 `source-manifest.json` 及各来源记录约束。

源码维护者先将审查过的源码加入 Git 暂存区，再执行 `node tools/internal/source-archive.cjs create` 和 `node tools/internal/source-archive.cjs verify`。除清单自身外存在未暂存修改时生成器会失败。v2 清单从 Git index blob 的原始字节生成；验证先比较原字节，再允许文本 checkout 的 CRLF 转换，保留 Git 中原有的特殊混合换行。二进制不转换；上游导入清单中的历史缓存不决定发布内容。

使用 `powershell.exe -File tools/Package.ps1` 生成源码归档及 SHA-256。它先校验当前清单，将固定内容复制到新的 `out/package/source-*`，再次校验再压缩；不会隐式刷新已失效清单。解压后无须 Git 也可执行 `node tools/internal/source-archive.cjs verify`。源码改变后回到维护者 checkout 更新清单，不能在解压目录中无依据重建发布集合。编译工具和 npm 包需要另行准备。

通过 Release 启动测试后，运行 `powershell.exe -File tools/Package-Applications.ps1 -Architecture x64` 分别生成 `SouiStarter-<版本>-windows-x64.zip` 和 `SouiWorkbench-<版本>-windows-x64.zip`；x86 同理。它检查源码、构建 ID、完整运行载荷与所有启动场景，再从校验后的独立暂存目录打包。每个包伴随 SHA-256 和 `.zip.json` 大小报告，包含逐文件散列、原生程序/内核/其他运行库/Web 分类大小和压缩参数。不执行代码签名。

现代模板使用 `powershell.exe -File tools/Package-Applications.ps1 -Architecture x64 -Profile modern`。该档位生成 `SouiModernStarter-<版本>-windows-x64.zip`，校验当前 WebView2 载荷与浏览器自然退出记录；x86 同理。共享 Runtime 另行安装，不计入应用 ZIP，也不能将此包称为固定 Runtime 离线包。控制台机器 JSON 保留 Unicode 路径，不依赖 PowerShell 当前代码页。

应用输出目录中的 `resources/` 为部署工具所有，重复构建会整体替换。产品数据必须放在独立用户数据目录；不要放入 `out/.../resources`。其他应用文件不会被资源替换操作删除；未识别文件会阻止运行包生成。

Windows 发布已复制的资源目录时，部署工具与 SDK 安装器采用同一有界策略：仅在源目录仍存在、目标目录尚不存在且返回 EPERM/EACCES 时最多重试五次，累计等待 450 毫秒。持续拒绝继续返回原错误并走原有回滚流程，其他错误立即失败；这不绕过权限检查或允许覆盖意外出现的目标。

交付前验证 SDK 独立构建、模板/Demo 四组配置、安装升级、生成项目独立构建、外部 CMake 引用、源码包完整性、底层依赖源码构建。正式发布还需完成 known-issues.md 中适用门槛。

签名证书、产品品牌、安装包、更新服务器与回退策略由实际产品配置。包内没有签名私钥、伪造签名或成品安装器。删除 preview 标签不代表发布验收完成。

LLVM 档位的 `Verify-Release.ps1` 为本轮已安装 SDK 自动准备 Host64 观察器，并将原始回执及 SHA-256 传入实际构建。x86 验证也使用 x64 宿主工具，因此会在所选独立 LLVM 工具目录中准备 x64 工具；任一准备失败立即停止。双架构发布验证应各用一个独立 LLVM 工具目录，避免后一次 fresh 构建改变前一次回执固定的工具原件。SDK 的基础安装消费者与可选 UIA/WebView2 安装消费者分别保留证明；重建安装目录前先完整保存已使用的旧目录，避免混合安装。

先完成依赖当前完整源码清单的固定任务，再运行写入 `verification/` 的发布验证与汇总，最后暂存新报告并重新生成完整源码清单和源码 ZIP。原生构建保存的源码清单输入副本继续作为当时的原始记录，不能改写旧回执匹配新清单。系统升级或工具文件字节发生变化后，重新登记工具并执行受影响验证。
