# preview.4 源码包搬移与编译验证

日期：2026-09-23。针对提交 `b9584b1c` 生成的完整源码清单包含 29,585 项，打包后的 ZIP 已通过 `7z t`；[机器结果](result.json)记录当次 ZIP 与源码清单散列。将 ZIP 解压到 SDK 仓库外的新目录后，在**无 Git 元数据**环境执行 `node tools/internal/source-archive.cjs verify`，逐文件校验通过。

从该解压目录分别执行 `tools/Build.ps1 -Architecture x64 -Configuration Release` 与 `-Architecture x86 -Configuration Release`，并运行 `tools/Test.ps1 -NativeOnly`。两架构均完成 SDK、模板、Workbench 编译及三入口实际启动/关闭：[x64 构建](x64-build-Release.json)和[smoke](x64-smoke-Release.json)，[x86 构建](x86-build-Release.json)和[smoke](x86-smoke-Release.json)。两个构建的源码指纹均为 `f4cb6eba6ff8ed6119d702b81df62c2db8279829afeeb8e035bd28e425cf22c9`。

本归档绑定的是上述当次源码快照。后续新增验证文档与证据会使最终源码清单和 ZIP 散列改变，发布前必须重新生成并核验最终 ZIP；不能沿用这里的散列作为最终发布校验和。本次从搬移后的包编译了 SDK/应用，但**没有在搬移目录重编译 Miniblink/SOUI 底层依赖**；Debug、业务升级/回退、目标设备与长稳仍未验收，`sourceDeliveryVerified`、`releaseReady` 保持 false。
