# 交付与发布

## 0.6.0 本次授权与执行范围

本轮目标为普通 GitHub Release `v0.6.0`，纳入本地受控 DevTools 与已合入主线的能力。当前源码、依赖、构建及交付材料正在收口，尚未发布。用户于 2026-10-28 明确选择普通 Release 并后置低收益长测；该决定替代此前候选阶段要求立即执行全部长测的顺序，不修改各验收项或旧失败的事实。见 [ADR-0136](adr/0136-0.6.0-release-scope.md)和[0.6.0 发布说明](releases/0.6.0.md)。

根版本使用 `0.6.0`，GitHub 分类使用 `prerelease=false`。生产资格继续 `releaseReady=false`、`productionQualificationComplete=false`；没有运行完整 Collector 时，根验收记录保持 `status=unverified`。0.5.0 的实际 GitHub 分类是 prerelease，不能重写为普通 Release 先例。

按实际依赖顺序执行最小充分集合：

1. 审查并冻结新 worktree 中的源、版本、peer、许可与对应源码范围；保护原 dirty 工作区和历史 raw。
2. 从最终源重建受影响的 MiniBlink 两架构依赖，绑定真实编译输入、导出和产物；其它未变化依赖仅在来源与原件身份明确一致时复用。源码能力不能靠旧 stock DLL 的存在变成已交付运行能力。
3. 使用所选受支持工具链完成适用的两架构 SDK/应用配置；共同 Node/工具测试只执行一次，Release 每架构执行必要原生回归。同一实际输入已有充分覆盖时，不换入口重复整套用例。
4. 核对新 SDK 的安装、生成项目、公开 CMake 消费者及实际运行/退出。DevTools 的默认拒绝、可信原生授权、配对资源和目标退休仍须按实际新载荷检查；Modern/Auto 的 Runtime 及双启动路径按交付选择执行。
5. 对最终应用包解压，验证资源/许可闭集、业务往返和自然收尾；发布前保留原始命令、退出码、跳过、失败和 SHA-256。
6. 写本轮真实范围报告，更新受影响 SBOM、依赖/配对记录和当前未验收状态；Root 最后暂存、生成/验证源码清单并打包，固定 tag 对应的最终 commit 和资产集合。

D13 千次、D14 八小时、完整性能对比、完整目标系统/输入环境资格和低收益重复升级/搬移演练保留待验。D01–D12 的缺项不得仅以优先级删除。完整 `Verify-Release.ps1` 六阶段若没有执行，不能用代表短验冒称它已通过；当前安装/生成、消费者、资源与权限门以及真实退出不因长测后置而省略。总期限、默认拒绝、失败判定和资源 fence 保持不变。

DevTools 材料选择使用 `Build.ps1 -WithDevToolsPackage` 及文档列出的六个显式路径/pin 参数；它不发 grant，不能与 `-SdkOnly` 混用。源码诊断另用 `-WebProfile source-diagnostic`。MiniBlink Inspector、业务 source map 与原生策略是独立身份；prepared 与公开部署的 delivery 元数据不同，编译输入必须绑定实际消费者读取的那一种，不能复用看似相近的散列。

建议保持既有完整源码 ZIP、source SDK ZIP 和两架构 Starter/ModernStarter/Workbench ZIP，并附逐包 SHA-256、应用 `.zip.json`、Release Manifest/SUMS。额外 Auto ZIP、Bundle、7z 或供应材料以最终实际清单为准；选择后满足相应的启动、恢复或配对门。Modern 不包含 WebView2 Runtime，Auto 不下载 Runtime；打包工具不执行代码签名，不捆绑 Fixed Runtime 或 Windows 字体文件。签名状态按新原件核对后披露。

本轮第三方再分发及对应源码说明见[许可材料](licenses/v0.6.0-redistribution.md)，完整源码资产见发布页。技术合成工具不产生外部批准，MIT 首方许可也不替代第三方条件。

完整收集未执行时，对真实、当前版本的范围报告使用：

```powershell
node tools/Collect-Verification.cjs --record-unverified `
  --evidence-reference verification/production-validation/<本轮目录>/<真实范围报告>.json
```

报告须有 `version: "0.6.0"`、`releaseReady: false`、`productionQualificationComplete: false`。上述路径是操作说明中的占位符，必须改为本轮已存在的实际报告。入口只记录未验收状态并保留旧 acceptance 原字节，不运行测试，不重验外部原件，不输出完整验收通过。它与其他 Collector 串行执行，之后再封存最终源码清单。

以下 0.5.0 与更早流程保留历史身份或用于完整资格验证；本轮事实以新的 0.6.0 范围报告为准。

## 0.5.0 历史发布范围

按维护者 2026-10-16 的要求，发布前采用最小充分验证集合：最终两架构四配置构建、每架构 Release 一次关键回归、SDK 外部消费者、交付包解压启动、现代后端本机 100 Hz 实测和交付清单检查。单次完整原生回归已经覆盖的用例，不再经另一个入口重复全量执行；断言、超时与失败判定保持不变。底层二进制导入仍须核对架构、来源、导出和清单。

8 小时长会话、千轮生命周期、完整性能对照与重复搬移全编译留作后续验证。发布记录必须列出已完成项、实际样本、未完成项和已知失败，未执行项不得标为通过；短时 100 Hz 测量仅支持该设备和样本范围的结论。本轮通过的是自动触发 S05 动效的两分钟真实页面调度诊断：窗口绑定当前 100 Hz 显示路径，requestAnimationFrame 中位间隔 10 毫秒。严格桌面截图采样因累计帧数大于一而失败，实际输入采样也未完成有效场景；这些失败保留，本轮不声称合成画面、实际输入响应或物理帧率认证通过。此范围调整不把 `releaseReady` 或完整生产验收状态手工改为通过。

本轮独立接入使用公开 `tests/package-consumer` 对当前已安装 `Soui::Sdk` 分别编译和运行，现代高刷采集另以外部宿主引用当前 `Soui::WebView2`。完整 `Verify-Release.ps1` 的多阶段升级/回退与搬移演练不重复执行，既有记录保留原载荷身份。因此完整 Collector 所需的全部发布证明仍为待完成；使用范围明确的本轮结果汇总，不生成或复制一份虚假的完整验收通过报告。下面的完整流程继续供后续认证使用。

完整收集未执行时，使用下面的显式入口同步根文件的当前版本与“未验收”状态：

```powershell
node tools/Collect-Verification.cjs --record-unverified --evidence-reference verification/production-validation/0.5-source30-release-2026-10-16/scoped-results/RELEASE-FACTS.json
```

该命令不运行构建或测试，退出 0 只表示状态记录成功。它核对引用的版本及非生产资格标志，保存相对路径、字节数和 SHA-256；不复验外部原件或继承通过结果。根文件保持 `scope=full`、`status=unverified`、`collectionExecuted=false`、`sourceDeliveryVerified=false`、`releaseReady=false`。旧根文件按原字节存入 `verification/history/` 并由新记录引用。与其他 Collector 串行执行，随后按下文刷新源码清单；已发布的固定包保持原样。设计与失败边界见 [ADR-0117](adr/0117-current-unverified-acceptance-status.md)。

## 交付组成

未发布候选新增[可选运行时去重 Bundle](runtime-packaging.md)：原单应用 ZIP
保持默认，新格式需要先恢复为完整独立应用目录，不能直接当作既有安装器使用。

应用及 Bundle 可显式选择[压缩档位](compression-profiles.md)。省略参数时 ZIP
格式、原参数和文件名不变；7z 档位的接收方需要相应解压工具。

显式 `-Profile auto` 包含现代与兼容页面和本地 MiniBlink；WebView2 Runtime
仍是外部依赖。打包要求同一构建的两条真实启动路径通过，详见[自动后端](auto-backend.md)。

- 完整源码包：SDK、模板、Demo、工具、文档、测试、第三方源码与固定运行依赖。
- SDK 安装包：通过 `tools/sdk/soui-sdk.cjs pack` 生成，包含逐文件身份清单。
- 可运行程序：分别暂存、验证和压缩的最小模板与 Workbench；诊断日志、PDB 和未知用户文件不进入运行包。
- 验收记录：工具链、配置、测试结果、产物散列和未完成项。

除下面的原生清单外，`verification/sbom-*.cdx.json` 来自十个当前 npm 锁文件：Web SDK、React/Vue GUI 绑定、兼容和 Modern 模板、Workbench、兼容和 Modern 参考工作台，以及验收工具和 Electron 对照工具。后两项属于验证工具依赖，不属于 SDK 运行载荷；应用包版本独立于根 SDK 版本。生成使用 `npm sbom --package-lock-only --sbom-format=cyclonedx --offline --ignore-scripts --install-links`，版本冻结后需重新生成受影响清单。React/Vue 清单中本地 Web SDK 的 distribution URL 经核对对应 package 名称和版本后，转换为相对清单目录的 `../sdk/web`；保留原始生成记录，不把开发机位置写进交付清单。[CycloneDX 1.5](https://github.com/CycloneDX/specification/blob/1.5/schema/bom-1.5.schema.json) 的 externalReference URL 使用 `iri-reference`。

`verification/sbom-native.cdx.json` 独立列出实际复算散列的 136 个原生交付文件及两架构共 22 个内核静态链接输入。链接输入仅表示该 archive 参与真实链接，不表示每个成员都进入最终 DLL；这些构建输入也不全是运行时分发文件。来源分别绑定 `third_party/manifests/dependencies.json`、`kernel-build-050.json` 和当前实际构建报告。清单不含首方编译产物和完整传递源码组成，composition 明确为 `incomplete`，未知叶依赖没有伪装成空集合。`sbom-native.validation.json` 保存实际官方 1.5 schema 校验结果及 schema 散列。版本或载荷变动后须重新生成、复算文件散列并校验。所有清单都不等同于漏洞已清零或授权审核完成；完整源码另由 `source-manifest.json` 及各来源记录约束。

源码维护者先将审查过的源码加入 Git 暂存区，再执行 `node tools/internal/source-archive.cjs create` 和 `node tools/internal/source-archive.cjs verify`。除清单自身外存在未暂存修改时生成器会失败。v2 清单从 Git index blob 的原始字节生成；验证先比较原字节，再允许文本 checkout 的 CRLF 转换，保留 Git 中原有的特殊混合换行。二进制不转换；上游导入清单中的历史缓存不决定发布内容。

历史验收目录 `verification/**/raw/old-generator/` 中的 `package.json` 和 `package-lock.json` 是当次输入原件，由同批 `raw-manifest.json` 固定字节数与 SHA-256。它们仅用于取证和复现历史记录，不在这些目录启动 devserver，也不升级其历史依赖或改写 raw manifest。活动模板的依赖升级在 `templates/` 中独立维护。执行 `node --test tests/sdk/archived-generator-inputs.test.cjs` 可核对已归档生成器的两份依赖输入与原始固定身份。

使用 `powershell.exe -File tools/Package.ps1` 生成源码归档及 SHA-256。它先校验当前清单，将固定内容复制到新的 `out/package/source-*`，再次校验再压缩；不会隐式刷新已失效清单。解压后无须 Git 也可执行 `node tools/internal/source-archive.cjs verify`。源码改变后回到维护者 checkout 更新清单，不能在解压目录中无依据重建发布集合。编译工具和 npm 包需要另行准备。

共享工作区或需要组合独立源码根时，另有显式入口，无须改变真实Git暂存区：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/Package.ps1 `
  -SourceRootsBinding C:/candidate/source-roots-binding.json `
  -PackageWorkspace C:/candidate/fresh-full-source-package
```

`PackageWorkspace`必须是全新的外部绝对路径；可选`OutputFile`只能位于其`dist/`中。绑定JSON使用`soui.full-source-roots/v1`，恰有`sdk`、`miniblink`、`publicSource`三项，每项为`{root, manifest:{path, bytes, sha256}}`，路径为本地绝对路径。SDK与MiniBlink的原始根可以使用`soui.raw-source/v1`清单：`sourceOnly:true`、实际`root`及完整逐文件`files:[{path,bytes,sha256}]`。原始根不能含私有缓存或额外文件，SDK根不能重复嵌入完整SOUI/MiniBlink；清单pin应放在原始根外。

`publicSource`选择同版本`soui.full-source/v2`清单中的四个固定子树：`third_party/sources/soui`、`.github`、`experiments/webview2-consumer`和`third_party/patches/miniblink`。其清单版本须与SDK raw根的`VERSION`完全相同；旧0.5.0清单不能直接绑定新0.6.0候选。工具按这些明确输入合成新v2清单，复制前后核对外部pin、闭集和逐文件内容，再压缩并执行归档完整性检查；不会刷新旧清单或覆盖既有包。

显式归档的`sourceComposition.sourceOnly=true`、`currentBinaryProvenance=null`及`historicalPrebuiltBinaries=true`保持原义。归档包含完整指定MiniBlink源码，但本身不认证历史预编译二进制与当前源码对应，也不等于当前DevTools内核构建或运行通过；对应关系须另由固定源码输入、真实编译记录和产物散列证明。该路径的数据验证入口为`tests/sdk/full-source-roots.test.cjs`、`delivery-identity.test.cjs`及独立`source-package.test.ps1`；不能把它们称为标准Test已自动全部执行。

通过 Release 启动测试后，运行 `powershell.exe -File tools/Package-Applications.ps1 -Architecture x64` 分别生成 `SouiStarter-<版本>-windows-x64.zip` 和 `SouiWorkbench-<版本>-windows-x64.zip`；x86 同理。它检查源码、构建 ID、完整运行载荷与所有启动场景，再从校验后的独立暂存目录打包。每个包伴随 SHA-256 和 `.zip.json` 大小报告，包含逐文件散列、原生程序/内核/其他运行库/Web 分类大小和压缩参数。不执行代码签名。

现代模板使用 `powershell.exe -File tools/Package-Applications.ps1 -Architecture x64 -Profile modern`。该档位生成 `SouiModernStarter-<版本>-windows-x64.zip`，校验当前 WebView2 载荷与浏览器自然退出记录；x86 同理。共享 Runtime 另行安装，不计入应用 ZIP，也不能将此包称为固定 Runtime 离线包。控制台机器 JSON 保留 Unicode 路径，不依赖 PowerShell 当前代码页。

应用输出目录中的 `resources/` 为部署工具所有，重复构建会整体替换。产品数据必须放在独立用户数据目录；不要放入 `out/.../resources`。其他应用文件不会被资源替换操作删除；未识别文件会阻止运行包生成。

Windows 发布已复制的资源目录时，部署工具与 SDK 安装器采用同一有界策略：仅在源目录仍存在、目标目录尚不存在且返回 EPERM/EACCES 时最多重试五次，累计等待 450 毫秒。持续拒绝继续返回原错误并走原有回滚流程，其他错误立即失败；这不绕过权限检查或允许覆盖意外出现的目标。

交付前验证 SDK 独立构建、模板/Demo 四组配置、安装升级、生成项目独立构建、外部 CMake 引用、源码包完整性、底层依赖源码构建。完整生产资格还需完成 known-issues.md 中适用门槛；本次普通 Release 依据上文 0.6.0 授权范围如实记录，不将未完成项标为通过。

签名证书、产品品牌、安装包、更新服务器与回退策略由实际产品配置。包内没有签名私钥、伪造签名或成品安装器。删除 preview 标签不代表发布验收完成。

LLVM 档位的 `Verify-Release.ps1` 为本轮已安装 SDK 自动准备 Host64 观察器，并将原始回执及 SHA-256 传入实际构建。x86 验证也使用 x64 宿主工具，因此会在所选独立 LLVM 工具目录中准备 x64 工具；任一准备失败立即停止。双架构发布验证应各用一个独立 LLVM 工具目录，避免后一次 fresh 构建改变前一次回执固定的工具原件。SDK 的基础安装消费者与可选 UIA/WebView2 安装消费者分别保留证明；重建安装目录前先完整保存已使用的旧目录，避免混合安装。

先完成依赖当前完整源码清单的固定任务，再运行写入 `verification/` 的发布验证与汇总，最后暂存新报告并重新生成完整源码清单和源码 ZIP。原生构建保存的源码清单输入副本继续作为当时的原始记录，不能改写旧回执匹配新清单。系统升级或工具文件字节发生变化后，重新登记工具并执行受影响验证。
