# preview.5 Windows 7 SP1 x86 QEMU 100 次重载

日期：2026-09-24。在已安装、由用户确认授权的 Windows 7 Enterprise SP1 `6.1.7601` 32 位 QEMU 来宾运行独立公共 SDK 消费者。候选版本 `0.3.0-preview.5`，源码指纹 `9c2fbfe61094b2a5dbf86e58c010736df2c772aed40295a3103ad12d17005d3c`，x86 SDK build-id `5ecdd31c-2a38-4e77-b662-80c1fa98d975`、消费者 build-id `d0e3b526-2cd7-4ae6-9b22-e929c3c806f9`，运行载荷 SHA-256 `ed3c7c823b6afa3a42d0fa7786f512d1425447c687750aedf9db1e660707cb15`。构建身份见[宿主构建记录](../preview5-timeout-diagnostics-2026-09-24/README.md)；本次来宾配置和二进制散列在 [environment.json](environment.json)。与[原冻结载荷的两次 x86 Bridge 千轮失败](../preview5-lifecycle-2026-09-24/README.md)不是同一来源，不能合并判定。

QEMU 11.1.0 使用 TCG、Core 2 Duo、2 vCPU、2 GiB、标准 VGA、无网卡、系统盘快照。76 个运行文件打包为 ZIP，经只读 vvfat D: 导入；来宾先用 x86 7-Zip 验证，再解压至 C: NTFS，结果也先落在来宾 C: NTFS，最后由 COM1 导出。来宾 ZIP、验收 EXE、`soui.dll` 和 `node.dll` 的 SHA-256 与宿主输入一致。测试完成后通过 ACPI 正常关机；[完成画面](completed-screen.png)仅作为辅助证据。

测试参数为 3 次预热 + **100 次测量重载**，每轮静置 1 秒、单步 60 秒。来宾 EXE 退出码 0；[逐轮事件](results/reload-events.ndjson)完整记录 103 次重载、109 个生命周期步骤、无失败事件和 `run.complete`，原生单调计时 271,974 ms。监督器式[事件校验](Verify-Events.cjs)复用公共验收验证规则，输出[核对摘要](results/verified-summary.json)。

| 样本 | Private Bytes | 句柄 | 线程 | USER |
|---|---:|---:|---:|---:|
| 预热结束，第 3 轮 | 28,459,008 | 188 | 10 | 19 |
| 最后测量，第 103 轮 | 43,335,680 | 245 | 14 | 19 |
| Runtime 关闭后 | 6,270,976 | 125 | 8 | 4 |

预热末点至末轮 Private Bytes 增量 **14,876,672 字节**，低于方案的 16,777,216 字节数值线；全过程峰值 48,783,360 字节。每 10 次测量的 Private Bytes 中位数在约 31.6–40.7 MiB 间波动，未见单调增长，但句柄与线程末点分别增加 57 和 4，归属与持续趋势尚未解释。因此仅确认本次 Win7 x86 软件路径的 100 次重载操作和内存端点数值观察，`resourceGate: not-evaluated`、`releaseReady: false`。

[原始串口导出](serial-export.txt)包含 15 个文件的长度、SHA-256 和 Base64。运行 `powershell -NoProfile -ExecutionPolicy Bypass -File Verify-SerialExport.ps1` 会重新核对完整帧、每个文件散列、Win7 SP1 32 位身份、ZIP 与二进制身份、三个操作退出码、103 次事件和资源样本。还原文件在 [results](results)，来宾脚本在 [harness](harness)。串口原文 SHA-256 为 `FAC4BD6F4189BAA6ACE0480CAEC8A02C6621B65B7D39E46A2F1F66F2FFD805D6`。

QEMU TCG 用时不是 Electron 性能对照，虚拟机不替代物理 Win7 x86。此运行没有覆盖 Win7 客体 1,000 次 Bridge、8 小时混合负载、公共任务满载业务、GPU、中文输入法/DPI、多屏、TLS 信任根与代理、安装升级回退、许可再分发审阅或真实业务资源门槛。
