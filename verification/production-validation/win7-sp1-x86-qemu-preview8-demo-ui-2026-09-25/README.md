# preview.8 Workbench Demo 在 Windows 7 SP1 x86 QEMU 中的真实交互短测

状态：**指定虚拟机中的 Demo 万行列表、公开任务完成与取消通过；生产准入仍未通过**。这次运行的是 Workbench 的 React Demo 页面，与[独立 SDK 消费者的 Bridge 三轮短测](../win7-sp1-x86-qemu-preview8-public-task-2026-09-25/README.md)分开计数。它使用 [input.json](input.json) 锁定的 x86 Release Workbench 载荷：候选源码 ZIP SHA-256 `EF4C036E190D111273B8183BCCBC931141A5F7FE3E8AAFDF5B1989CC32903DC6`，运行 ZIP SHA-256 `EC69C61F1955E1B1D00DC1710811AA9BAAD4B12A7D5D9AE6508D49F580FF8D25`，475 个文件。没有在来宾中重新编译应用；本次也不以虚拟机帧率评定性能。

宿主用 [Prepare-DemoUi.ps1](Prepare-DemoUi.ps1)从冻结的载荷生成 [475 项逐文件散列](payload-sha256.tsv)，将 ZIP、7za 和来宾脚本放入只读 FAT 传输盘。QEMU 通过 [launch.json](launch.json)记录的参数启动已安装 Windows 7 Enterprise SP1 x86：TCG、2 vCPU、2 GiB 内存、`-snapshot`、断网。来宾运行 [run-demo-ui.cmd](run-demo-ui.cmd)，在 NTFS 上测试/解包 ZIP，检查全部 475 个文件与五份许可文件，校验 EXE、Miniblink 和 SOUI DLL，再以 `--react --renderer=cpu` 打开 Workbench。来宾系统记录为 6.1.7601、SP1、32-bit；应用经标题栏正常关闭，退出码 0。QEMU 已退出，基础磁盘没有写回。

真实 GUI 操作及画面：

| 操作 | 观察 | 画面 |
|---|---|---|
| 进入“万条数据”页面 | 页面显示 `10000 / 10000`，列表首行可见 | [01](evidence/01-data-list-start.png) |
| 列表获得焦点后按 End | 虚拟列表滚至 `fixture-09988` 附近，非仅在首页显示计数 | [02](evidence/02-data-list-last-rows.png) |
| 点击“运行” | 公开 SDK 任务呈现 `running · 7%` | [03](evidence/03-task-running.png) |
| 等待任务完成 | 页面呈现 `succeeded · 100%`、`Count complete.` | [04](evidence/04-task-succeeded.png) |
| 再次运行并点击“取消” | 页面最终呈现 `cancelled · 5%`、`Native task cancellation confirmed.` | [05](evidence/05-task-cancelled.png) |
| 点击标题栏关闭 | 控制台显示 Workbench 退出码 0、COM1 导出 15 个文件 | [06](evidence/06-normal-exit.png) |

GUI 操作由 QEMU 监视器注入鼠标/键盘事件，并人工核对截图；这不是像素自动断言或持续性能采样。一次较晚的取消请求与任务结束竞态，[页面显示](evidence/08-late-cancel-terminal.png) `The task already has a terminal result.`，最终状态 `succeeded`；改在任务早期点击后才得到上述 `cancelled`，不能把晚取消记作取消成功。设置 `SOUI_INITIAL_PAGE=data` 后[最初仍显示首页](evidence/07-initial-home.png)，测试通过首页链接进入数据页。随后核对 [Workbench 启动入口](../../../demos/workbench/app/main.cpp) 确认 `run_gui` 在未传 `--page` 时主动清除该环境变量；原始测试脚本只设置变量，漏传 `--page=data`。这个落点符合启动入口的行为，本轮保留原始脚本；后续直达数据页应使用 `--page=data`，不能把未执行的修正命令记为通过。没有测试中文 IME、窗口缩放、安装升级、多屏、物理 Win7 设备或长时资源收敛。

[原始串口](serial-export.txt)导出 15 个来宾结果文件。[Verify-UiSerial.ps1](Verify-UiSerial.ps1)在内存中只读检查串口帧长度与 SHA-256、系统/应用/内核身份、ZIP 测试与解包、逐文件和许可散列、六个有序生命周期步骤及退出码；[核验输出](verifier-output.txt)通过。[反例自检](self-test-output.txt)覆盖修改载荷帧、截断串口，以及重新计算正确帧散列后的非零退出码、关闭步骤缺失和乱序；五个负例均拒绝，原样本通过，每次调用前后确认归档结果文件及字节不变。截图及原始结果均纳入源码清单以固定字节身份。这个短测只补充 Demo 交互证据，不改变 `releaseReady=false`；生产准入还需要物理平台、真实项目场景、长期资源/稳定性、发布链与同机 Electron 配对性能结果。
