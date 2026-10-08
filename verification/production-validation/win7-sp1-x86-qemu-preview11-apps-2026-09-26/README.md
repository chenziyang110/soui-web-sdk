# preview.11 Win7 SP1 x86 应用短测准备

状态：**输入已冻结，客体未运行**。当前候选来自提交 `bec71cef4135087b76ccb368db8544950c1806b8` 对应编译输入，源码 SHA-256 为 `a0c1d7900d39555508288d5459e65d3852e543cf7ec3879126bd6af59d2c2550`，x86 Release buildId 为 `7e000158-98f0-4349-854e-15c581663737`。这是已编译应用候选，不是完整源码搬移重编结论。

原外部准备目录为 `C:\SOUI-SDK-production-validation\preview11-win7-preparation-20260926`；只使用其 `transfer/` 作为新只读 FAT 传输盘。Starter 71 文件、Workbench 475 文件及各 5 份 legal 材料已逐项核对。两份 ZIP 校验通过；宿主完整载荷校验退出 0。三项计划 smoke 为 Starter、Workbench 默认入口与 React 入口，各有 60 秒监督期限。

- [输入清单](inputs/input.json) SHA-256：`84f275ea91aa5af51b8844724ec1dc4d70d0a2116268a4d1070f9539266863b0`。
- [整个外部准备目录冻结清单](inputs/freeze.json)：618 文件，SHA-256 `96d05251d6dd78848306fa3868c671f511c2b9ebc658951bf2f29d8e6a81b5ba`。
- [串口校验器自检](inputs/self-test-only-v2-results.json)：1 个完整合成样本被接受、17 个负例被拒绝。合成结果不是客体通过证据。
- [原准备说明](inputs/README.md)、[只读串口校验器](inputs/Verify-AppSerial.cjs)、[完整冻结目录校验器](inputs/Verify-Frozen.cjs)与传输脚本按原字节归档；[归档清单](archive-manifest.json)绑定本目录的小文件副本。

本仓库只归档脚本和身份记录，不重复提交 ZIP、运行二进制、全部载荷副本或合成串口样本。`Verify-Frozen.cjs` 必须在完整外部目录运行；仓库的部分归档不能伪装成完整传输盘。执行自检时另建新目录，禁止向冻结目录追加结果。

当前其他 QEMU 占用桌面，未启动本候选。后续先复核桌面空闲和冻结身份，选择空闲端口并保存启动参数、PID、串口和截图到新的 attempt 目录；确认 Win7 桌面就绪及传输盘盘符后，只发送一次 `cmd.exe /c D:\run-preview11.cmd`（仅当传输盘实际为 D:）。每次使用不写回基本盘的独立 snapshot，保留失败原样本。完整串口帧、系统身份、载荷前后散列、原始退出码与就绪/关闭顺序全部符合后，才另行记录短测结论。

未关闭真实桌面、Win7、超过 30 分钟及八小时续期、V02、资源/性能或生产门槛。`guestExecuted=false`、`sourceDeliveryVerified=false`、`benchmarkValid=false`、`releaseReady=false`。
