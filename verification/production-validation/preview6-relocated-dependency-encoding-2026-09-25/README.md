# preview.6 搬移源码重建缺陷与 preview.7 定点复测

此记录沿用[已校验的无 Git 源码搬移](../preview6-source-relocation-2026-09-25/README.md)：`D:\SOUI-SDK-QEMU-validation\preview6-source-stage-20260925-a8b6114a` 在首次编译前包含 30,190 个文件，源清单 SHA-256 为 `DE312AA7051B5C9031DEC22AE5B4849766F314300BE8D1E18B216FAA515772B1`。以下依赖重编和诊断均在该仓库外目录执行；修改编码标记后的诊断目录已不再对应原清单，不能把它称为原封不动的已签源包。

首次 `Build-Dependencies.ps1 -Architecture x86 -Component All` 完成 zlib 至 Skia/GPU 的依赖编译，但 Miniblink 主库在 `TileGrid.cpp` 出现 33 条编译错误，日志同时有 28 条 C4819 旧代码页警告，见[原始失败日志](baseline-x86-failed.log)。该命令在错误出现后由监督操作中断，不记录为自然完成。源码清单将文本归一为 LF；四个 UTF-8 文件没有 BOM，MSVC 在本机代码页 936 下对它们发出 C4819。

一次从全新输出目录、全局传入 `/utf-8` 的诊断绕过了原报错，但混合编码的旧源码随即产生 3,086 条 C4828；该运行被主动停止，见[全局参数试验日志](global-utf8-x86-stopped.log)。因此不采用全局改变源字符集的方案。

preview.7 的定点修复只给 `TileGrid.cpp/.h`、`ActionsFrameGroup.cpp`、`LayerTreeHost.h` 和 Skia 的 `SkDisplacementMapEffect.cpp` 加 UTF-8 BOM。逐字节核对证明五个文件删除新增 BOM 并归一行尾后与上一提交相同；[第 7 个补丁](../../../third_party/patches/miniblink/0007-mark-utf8-source-encoding.patch)可正向和反向校验。依赖构建入口同时增加 C4819/C4828 告警失败门槛，已用双架构 SOUI 组件实际通过，并对首次失败/全局参数/定点修复日志验证了告警识别。

| 架构 | 底层重建 | 新内核散列 | 实际桌面启动 |
|---|---|---|---|
| x86 | 依赖库与 GPU 从搬移源码构建；Skia 编码标记后[重编](skia-x86-bom-rebuild.log)，Miniblink 从[全新目标目录完整编译及链接](x86-miniblink-bom-build.log)，C4819/C4828/硬错误均为 0 | `FFAAC5EEF1E520BAD5C5E450BF0A380B32871A4701ED452D4E28E7EA726FB68B` | [Starter 与 Workbench](x86-rebuilt-smoke.json)均退出 0，页面/Bridge 就绪并正常关闭 |
| x64 | [完整 Miniblink 依赖及主库](x64-miniblink-build.json)从源码重建；[Skia](x64-skia-bom-build.log)、[GPU](x64-skia-gpu-build.log)、[主库](x64-miniblink-bom-build.log)无 C4819/C4828/硬错误 | `CEE964213B2895E011E64E4A7034BC4039AB05CB9B56B671B7F7789E9E21A012` | [Starter 与 Workbench](x64-rebuilt-smoke.json)均退出 0，页面/Bridge 就绪并正常关闭 |

x86/x64 的 SOUI 基础库也分别从搬移源码重建，见[x86 报告](x86-soui-build.json)和[x64 报告](x64-soui-build.json)。preview.7 当前工作树的双架构 Release 构建、契约/原生测试和三个应用 smoke 另由[构建](preview7-x86-build-Release.json)/[测试](preview7-x86-test-current.log)及[x64 构建](preview7-x64-build-Release.json)/[x64 测试](preview7-x64-test-current.log)记录；这些固定运行包结果与新内核结果保持分列。新 x86 内核还随 preview.7 应用在[Win7 SP1 x86 QEMU 短测](../win7-sp1-x86-qemu-preview7-rebuilt-source-2026-09-25/README.md)中完成三入口启动与关闭。

SOUI 旧工程还会在源码树生成 DLL、目标文件和预编译头辅助文件；编译后源码目录的文件集合因此不同于开编前的源清单。必须在开编前校验原始包，并分别记录生成产物身份。本次五个 BOM 文件是对已验证的 preview.6 搬移目录所做的诊断性改动；最终 preview.7 整包的全新搬移、四配置构建、长期资源与目标设备等门槛不能由此替代。当前 `sourceDeliveryVerified=false`、`releaseReady=false`。
