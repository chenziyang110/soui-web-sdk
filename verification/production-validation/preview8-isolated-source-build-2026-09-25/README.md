# preview.8 无 Git 源码包：双架构隔离重编与四组应用构建

状态：**本轮工程复现通过；生产准入未通过**。冻结输入是 `0.3.0-preview.8` 的[候选源码包身份](input.json)：ZIP 213,578,018 字节，SHA-256 `EF4C036E190D111273B8183BCCBC931141A5F7FE3E8AAFDF5B1989CC32903DC6`。它是从待审查暂存索引生成的候选包。加入本记录与文档后的最终源码包散列不同；逐项清单比对发现 0 个非文档/验证代码文件变化、0 个文件删除，最终 ZIP 另从新目录解压并通过完整清单验证。最终 ZIP 的散列随 PR 交付，不把两个 ZIP 称为同一个文件。

将该 ZIP 分别解压到新的 x86、x64 目录，均无 `.git`。编译前两目录的完整源码清单验证通过 30,794 文件，清单 SHA-256 为 `F5AFD607932DF38EEDA641ECFB8E8B9AE3A2C88C5E9E387A96D30533EF8389C4`。各自从随包源码完成全部 Miniblink、Skia GPU、五个 SOUI 组件重编；[x86](x86/build-All.json)、[x64](x64/build-All.json)记录六个 DLL 的路径与散列。SOUI 旧工程写入的对象文件现在被约束到各架构的 `out/dependencies/<arch>/soui-source-*` 副本。**底层重编后**，两个原解压目录的[完整清单验证 x86](x86/postbuild-verify.log)、[x64](x64/postbuild-verify.log)再次通过 30,794 文件。两架构各 18 份[依赖日志 x86](x86/dependency-logs.zip)、[x64](x64/dependency-logs.zip)已压缩并测试可解包；C4819/C4828 告警计数均为 0，其他旧依赖告警不在此结论内。

同一目录的 x86/x64 × Release/Debug 应用构建与完整 `Test.ps1` 均退出 0。四组测试各有安装包测试 14/14、Web/监督器测试 58 通过和 1 项未配置调试器而跳过、原生 CTest 6/6，并各自完成 Starter、Workbench 默认页、React 页 3/3 实际桌面短测。[构建报告](x86/build-Release.json)、[Debug 报告](x86/build-Debug.json)及同名 x64 文件分别保留构建 ID 和完整运行载荷；原始构建、测试、smoke、原生日志在各架构目录。两架构的[重编 DLL 实际启动 x86](x86/rebuilt-smoke.json)、[x64](x64/rebuilt-smoke.json)也通过 Starter 和 Workbench，核对 DLL 散列、Bridge 就绪与正常关闭。x86 `node.dll` SHA-256 `BA4D9A6F9C6CA1A064FFC67EEA4318106DB7A7925D0B8533863DCAF8360D6536`，x64 `miniblink.dll` SHA-256 `887D0444BA5E316B82BA519696EBFE226BA2457377EBF9672DA1D828A7B81CB6`。

[独立生成项目](independent-project/generate.log)从该 preview.8 SDK 包安装 408 文件，在仓库外分别完成 x86/x64 Release 构建及真实 `--smoke-test` 启动/关闭；[逐架构报告](independent-project/x86/run.json)和 x64 同名文件记录 EXE/包锁散列。另一个独立业务小样的[preview.7 → preview.8 → 回退 → 再升级](../preview8-independent-upgrade-2026-09-25/README.md)在双架构各四阶段通过。由本轮重编 x86 DLL 制作的[Windows 7 SP1 x86 QEMU 三入口短测](../win7-sp1-x86-qemu-preview8-isolated-source-2026-09-25/README.md)也单独留证。

此处关闭的是候选源码包的可搬移、可重编与短启动问题。物理 Windows 7 设备、真实业务长稳、资源与 GPU 回收、旧 Bridge 超时根因、Electron 实际呈现对照、签名安装、内核安全维护和再分发授权仍缺证据；`sourceDeliveryVerified=false`、`benchmarkValid=false`、`releaseReady=false`。
