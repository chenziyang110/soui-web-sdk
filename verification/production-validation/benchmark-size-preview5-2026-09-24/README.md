# preview.5 同页面最小运行包体积测量

日期：2026-09-24。候选源码指纹 `b28bcdfb7a31cc6f8e4398e89a6c78dc96c490c1c648c7984e45097e14da96eb`，SDK x64 Release build-id `8e7786d3-b47e-49e2-a867-505a5e9ce636`。Windows x64 宿主版本 `10.0.26200`；Electron 精确版本 `44.4.4`。两端使用逐文件相同的本地页面快照，`benchmark.html` SHA-256 为 `d6264e7dd53caf89eddf78986e3d6b09ddf278a9cde3e050641127ab8ca2f74b`。测量程序见[源码和复现命令](../../../benchmarks/size/README.md)。

| 交付目录 | 文件数 | 解压后字节 | 同算法 ZIP 字节 | ZIP SHA-256 |
|---|---:|---:|---:|---|
| SOUI 最小宿主 | 20 | 38,765,828 | 14,533,916 | `f515c1865d491512e3b61953fa18683a5338230ec3223984d7f59494c670b4b9` |
| Electron 最小宿主 | 85 | 386,055,337 | 154,312,842 | `290d8c7cd8e1bb99274f669a6d3f7572a6278209ead36089e70bb229cc488220` |

SOUI/Electron 比值为解压后 **10.04%**、压缩后 **9.42%**。两份 ZIP 均通过 `7z t`；完整逐文件长度、SHA-256、共享页面身份和计算结果见[机器报告](report.json)。ZIP 保存在本地测量输出目录，不纳入 Git 仓库。SOUI 包含宿主 EXE、Miniblink、SOUI、应用本地 VC/UCRT DLL 和现有声明文件；Electron 包包含锁定发行目录及最小应用和同一页面。两端都不是正式安装程序或六组业务 Demo。

脚本从打包目录各运行一次 S01 结果落盘探针。SOUI 使用包外副本将测试入口覆盖为探针页，未改动被测包；Electron 原包直接启动。两端退出码均为 0，场景与 fixture 一致；[原始 JSON 探针文件](smoke/)及其散列在报告中。完整 stdout/stderr 保存在本地测量输出目录，报告记录其 SHA-256。Electron 标准错误记录两条 `GPU state invalid after WaitForGetOffsetInRange`，探针仍正常退出；不能据此判断图形路径长期稳定。探针没有真实输入、物理呈现或完整进程树资源采集，不能比较启动耗时或帧性能。

此项只建立**最小宿主体积**样本。完整产品体积、依赖再分发许可、安装签名、两端同机交互性能、内存和维护成本均未闭环。`benchmarkValid=false`，`releaseReady=false`；SDK 生产准入及相对 Electron 的整体采用价值仍不可判定。
