# preview.7 最终完整源码包的双架构底层重编

状态：**原封源码包的 x86/x64 Release 底层重编、应用编译和短启动通过；整体源码交付及生产门槛仍未通过**。本记录对应 `0.3.0-preview.7`，不是早先被诊断性修改的 preview.6 搬移目录。

冻结输入是 `soui-sdk-0.3.0-preview.7-full-source-reviewed.zip`，211,361,180 字节，SHA-256 `A4F0703A4C6F6E13DC4A07BC59174D347487622143AAB51E82B3D3F4190D802E`。分别解压到两个新目录，均无 `.git`；在编译前，各自 `node tools/internal/source-archive.cjs verify` 通过 30,719 文件，清单 SHA-256 均为 `11FCBB4DE0BC269CA7DC6D0268FC35FDE67C268FF132A403F3DFB790F1487619`。另留有一份全新未编译目录的[解压记录](clean-extract.log)及[清单验证输出](clean-verify.log)，同样通过 30,719 文件。`Doctor.ps1` 确认本机 MSVC v143、Windows SDK 19041、ATL、Node 和 CMake 工具链可用。

两个目录分别执行 `Build-Dependencies.ps1 -Architecture x86|x64 -Component All`，11 个 Miniblink 依赖、GPU、主库及五个 SOUI 组件均退出 0；[x86](x86/build-All.json)、[x64](x64/build-All.json)报告各记录 6 个 DLL 和散列。两架构各 18 个[完整依赖日志压缩包 x86](x86/dependency-logs.zip)、[x64](x64/dependency-logs.zip)已通过 ZIP 完整性检查，C4819/C4828 编码警告均为 0。其他旧依赖警告仍可能存在，本次不宣称“零警告”。

从各自同一份解压目录继续执行 Release `Build.ps1`、完整 `Test.ps1` 与 `Test-Dependencies.ps1`，均退出 0。[应用构建报告 x86](x86/build-Release.json)、[x64](x64/build-Release.json)的源码指纹相同：`7b3813eb4516f4e4870040840343cbe8a661c59a744853d4e5cd32f1b2af419a`。两份[应用 smoke x86](x86/smoke-Release.json)、[x64](x64/smoke-Release.json)各通过 Starter、Workbench 默认页和 React 页 3/3；[完整测试 x86](x86/app-test.log)、[x64](x64/app-test.log)含原生 CTest 6/6、安装包测试 14/14，以及 58 项通过、1 项因未配置 x86 调试器跳过的 Web/监督器测试。[重编 DLL smoke x86](x86/rebuilt-smoke.json)、[x64](x64/rebuilt-smoke.json)都通过 Starter 与 Workbench 默认页启动、Bridge 就绪和正常关闭，并记录运行 DLL 散列。x86 `node.dll` SHA-256 为 `134412B631E10A98083D36B7CE42FE20386769A63F50EA8545CA3EF70C23907E`；x64 `miniblink.dll` 为 `BF70CBE25DBBADEC2CC6E9E98C12F2630E1AEDA51524A2543DD4042FB7831F72`。控制台日志和完整构建日志保存在各架构子目录。

**编译后再运行完整源码清单 `verify` 会失败**：[x86](x86/archive-verify.log)、[x64](x64/archive-verify.log)都报告文件集合不同。通过[只读检查器](inspect-post-build.cjs)逐个比较清单源文件后，[x86](x86/post-build-source.json)、[x64](x64/post-build-source.json)均确认 30,719 个清单源文件无缺失或内容改变，但 SOUI 旧工程在源码树里各生成 112 个未列入清单的编译产物。打包与完整清单验证须从干净源码目录进行；该构建副作用是待修的交付流程缺口，不能把编译后的目录误称为原封源码包。

[同一最终包的重编 x86 DLL 在 Win7 SP1 x86 QEMU 三入口短测](../win7-sp1-x86-qemu-preview7-final-source-2026-09-25/README.md)通过。完整源码交付仍缺 Debug/四组配置、目标设备与升级链；生产还缺资源、Bridge/GPU、真实页面性能、安全维护、授权和签名等门槛。`sourceDeliveryVerified=false`、`releaseReady=false`。
