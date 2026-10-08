# 性能系列实时门禁：父进程 PID 复用回归

实时门禁原先仅凭当前 `ParentProcessId` 与已归属进程 PID 相等，就可能把一个**比测试进程更早创建**的外部 `electron.exe` 归为测试子进程。Windows 的进程 ID 可以复用，这会漏报同名外部负载。新测试使用真实 Windows PowerShell/CIM 日期序列化格式 `/Date(毫秒)/` 固定该边界，并同时确认真正较晚创建的子进程仍被归属。

先加入测试、尚未修改分类器时，`red-attempt1` 原始退出码 **1**：实际归属 PID 为 `[100,200]`，预期仅 `[100]`。加入创建时间先后检查后，`green-targeted-attempt1` 原始退出 **0**、1/1；完整无图形界面的 `series-interference.test.cjs` 在 `green-full-attempt1` 原始退出 **0**、20/20、stderr 空。`Capture-Node-Test.cjs` 保存命令、时间、原始 stdout/stderr 字节数与 SHA-256；`Verify-Index.cjs` 只读核对原始文件、结果声明及 Git index 字节。旧 PR40 整合测试原件保持不变。

`liveInterferenceCheck` 对真实 CIM 时间戳要求可解析的正整数时间；分类时只有子进程创建时间不早于当前父进程创建时间，才允许按父 PID 收养。日期缺失或异常的真实扫描失败关闭。纯分类夹具中的旧占位字符串不代表真实扫描输入。

在同一暂存源码 SHA-256 `d3edaefdf0ca2e4f337ec7446499a82b65774cfbd8cba45de507d8fde40c7af3` 上，另以只读 `Observe-Live-Gate.cjs` 查询实际运行中的外部 QEMU：`live-gate-observation-attempt1.json` 记录自身运行器 PID/创建身份均被识别，外部 QEMU PID 44672 被列为唯一竞争进程。该次工具调用在任务日志中返回 0；本目录保存结果 JSON，但未单独保存外层命令日志。该观察只证明当前门禁能发现这一个正在运行的外部进程，不补足采样间隙，也不算性能系列样本。

运行回归时有其他 QEMU 活动；这里只验证门禁逻辑和自身测试进程清理，**没有**运行 SOUI/Electron 正式性能系列，没有取得隔离资源、时延或五对交替结果。`benchmarkValid=false`、`releaseReady=false`。合并分支的完整源码清单在宿主空闲后按最终暂存输入重新生成、验证和提交；本目录的回归不能代替该步骤。
