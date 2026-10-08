# preview.11 Win7 SP1 x86 三应用短测输入

状态：**已准备并冻结；尚未运行客体；生产准入未通过**。此目录只绑定当前已编译候选，不声称在无 Git 目录从完整源码重编，也不借用 preview.8/preview.9 客体结果。

- 版本：`0.3.0-preview.11`，x86 Release。
- 构建 ID：`7e000158-98f0-4349-854e-15c581663737`。
- 源码 SHA-256：`a0c1d7900d39555508288d5459e65d3852e543cf7ec3879126bd6af59d2c2550`。
- 对应维护提交：`bec71cef4135087b76ccb368db8544950c1806b8`（PR #32）。此记录不把完整源码 ZIP 当成本次构建输入，也不声称该源码 ZIP 已经搬移重编。
- 原始构建记录：`build-Release.json`。`input.json` 绑定记录、两个完整载荷、ZIP、引擎/SOUI DLL、资源与声明文件以及所有传输脚本。
- Starter：71 文件，28,571,761 字节；Workbench：475 文件，39,504,344 字节。两者各含 5 个 `resources/legal` 文件。
- `payloads/` 从当前构建清单逐文件复制；复制前后均核对原目录和副本的集合、长度、SHA-256。忽略范围仅遵循原构建工具明确允许的运行日志/PDB，副本本身严格等于构建载荷集合。
- `transfer/` 是唯一供新只读 FAT 传输盘使用的目录，共 10 文件。ZIP 单线程、无压缩准备，已实际执行 `7za t`。`7za.exe` 是此前使用的 Win7 x86 19.00 工具，SHA-256 `ea308c76a2f927b160a143d94072b0dce232e04b751f0c6432a94e05164e716d`。

## 已执行的准备检查

`transfer/verify-apps.ps1` 已在宿主对复制载荷执行完整集合与散列核对，退出 0；原始观察记录见 `host-payload-check/`。该检查不启动应用，也不能证明 Win7 PowerShell 2 的实际执行。

最终输入对应 `self-test-only-v2-results.json`：1 个完整合成协议样本被接受，17 个负例被拒绝，覆盖原始帧散列错误、截断、重复/缺失文件、载荷或 legal 散列错误、客体身份错误、传输脚本身份错误，以及帧散列正确但非零退出、超时、参数变化、缺失/重复/乱序关闭。`SelfTest-Serial.cjs` 还检查校验器不改变文件集合、不创建 `results/`。

`self-test-only/` 与 `self-test-only-v2/` 内全部是**合成校验器输入，不是客体执行证据**。早期一轮用于脚本编写时自检；最终 v2 报告绑定最终 `input.json` SHA-256。两轮均保留，不能拿这些样本宣称客体通过。

三个 PowerShell 脚本已做宿主语法解析；客体脚本按 PowerShell 2/.NET 2 可用语法/API编写，使用 `HashAlgorithm.Clear()`，不使用 `ConvertFrom-Json`、`Get-FileHash`、`$PSScriptRoot` 或新版压缩 API。实际 Win7 兼容结果待运行。

## 空闲后运行

先重新确认宿主没有 SouiAcceptance、任何 i386/x86_64 QEMU 或其他桌面验收负载。不要中断别人的 QEMU。完成该门禁前，只能做准备。

冻结检查（只读，不输出客体结果）：

```powershell
node C:\SOUI-SDK-production-validation\preview11-win7-preparation-20260926\Verify-Frozen.cjs
```

使用既有授权镜像 `D:\SOUI-SDK-QEMU-validation\system.qcow2`；运行新的 QEMU snapshot、只读 FAT 盘、无网络。沿用已验明的 `pc,accel=tcg`、`core2duo`、2 vCPU、2048 MiB、标准 VGA。仅把传输盘指向下面目录：

```text
-drive file=fat:ro:C:\SOUI-SDK-production-validation\preview11-win7-preparation-20260926\transfer,format=raw,if=ide,index=1
```

启动器必须另建仓库外的新 attempt 目录来保存启动参数、QEMU PID、门禁、桌面就绪截图、命令发送记录、串口原始流与关闭结果；禁止放入本冻结目录。VNC、monitor 端口须现查空闲后选定，不照搬历史 PID/端口。串口文件必须是新文件；保留失败样本。

确认 Win7 桌面和传输盘盘符后，只发送一次命令。若传输盘为 `D:`，准确入口是：

```cmd
cmd.exe /c D:\run-preview11.cmd
```

脚本固定创建 `C:\soui-app-validation-preview11` 与 `C:\soui-app-results-preview11`；任何一个已存在就退出 10，绝不覆盖旧记录。再次尝试需退出 QEMU 并使用新的未写回 snapshot，不要删除旧失败结果后重复发送。

客体依次验证 OS 6.1.7601/SP1/32-bit、完整传输集合、ZIP 测试和解包、全部载荷（含 Unicode 路径、5×2 legal 文件），然后串行运行：

```text
SouiStarter.exe --smoke-test
SouiWorkbench.exe --webview-smoke-test --arch=x86 --renderer=cpu
SouiWorkbench.exe --webview-smoke-test --arch=x86 --renderer=cpu --react
```

每项使用独立的 `.NET Process` 句柄，60 秒期限，250 毫秒轮询；超时终止该应用，最多再等 5 秒，并记录退出码/超时/残留状态。任何应用失败即停止后续应用并导出失败原样本。应用正常结束后，移动该次 smoke 日志；若源码内核生成 `source-navigation.log`，另存为该次导航日志，之后复核完整载荷仍与输入一致。不会修改候选资源或放宽集合检查。

宿主仍须有整体客体监督期限（建议 10 分钟，短轮询保持可中断），覆盖开机后输入/传输异常与串口阻塞；该期限不是三应用的性能指标。串口单次写入有 15 秒超时，结果文件上限 1 MiB、最多 40 文件。未收到完整终态不得宣称通过，禁止因短测耗时记录推导隔离性能。

## 验证与归档

收到 `SOUI_SERIAL_END` 后，在宿主执行（替换为本次真实串口文件路径）：

```powershell
node C:\SOUI-SDK-production-validation\preview11-win7-preparation-20260926\Verify-AppSerial.cjs C:\SOUI-SDK-production-validation\preview11-win7-apps-attempt1\serial-export.txt
```

校验器只读，不自动写出/覆盖解码文件。它要求 30 个基础结果文件，允许额外 0–3 个对应运行的导航日志，验证每帧长度/SHA-256、原始退出码、命令行、超时与进程状态、输入/传输/所有载荷身份，以及 ready → Host/adapter 有序关闭。全部条件通过后，才输出 `verified-functional-smoke-evidence`；任何缺失、错误或额外未知文件均非零退出。

将真实原始串口、校验输出和 QEMU 退出状态写入该次 attempt 目录。关闭 snapshot QEMU，保留基本磁盘不写回。若要解码归档，必须在只读校验成功后另建新目录，绝不拿合成自检样本当真实结果。

即使三项短测通过，也仅证明该 compiled candidate 在此 Win7 QEMU 环境的三个应用入口、资源身份、就绪和正常关闭；不覆盖 30 分钟/8 小时续期、V02 IME/滚动/缩放与任务并行、托盘/快捷键、物理 Win7 设备、网络信任、安装签名或性能门槛。`releaseReady=false`。

## 归档范围

`preview11-win7-preparation-records.zip` 是小体积记录包，内容由 `records-file-list.txt` 明列：脚本、原构建记录、完整输入清单、传输/载荷 TSV、最终合成自检报告与宿主载荷核对记录。它不包含运行载荷 ZIP/EXE/DLL，也不包含合成串口样本；因此不是可独立运行的传输盘。完整准备目录及实际应用 ZIP 保留于此。顶层 `freeze.json` 冻结全目录，归档时与记录包一起保存；真实客体 attempt 结果始终另存。
