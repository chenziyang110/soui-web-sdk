# 双端对照宿主结果落盘探针

日期：2026-09-23。此归档只验证共享页面能在最小宿主中启动并保存 S01 结果，**不是性能对照样本，也不产生采用价值结论**。[`verification.json`](verification.json)由 `tests/acceptance/verify-benchmark-smoke.cjs`核对页面/资源散列、Electron 版本与隔离设置、三份结果及导航日志；`benchmarkValid` 明确为 `false`。

| 宿主 | 实际执行 | 结果 |
|---|---|---|
| SOUI x64 | 公开 C++ SDK + 公开 Web SDK，固定 preview.4 x64 内核，探针入口自动完成 S01 | [`soui-x64-run5/`](soui-x64-run5/)落盘并正常退出 |
| SOUI x86 | 同一页面资源，固定 preview.4 x86 内核 | [`soui-x86-run1/`](soui-x86-run1/)落盘并正常退出 |
| Electron | 精确版本 44.4.4，沙箱/上下文隔离/Web 安全启用，Node 集成关闭 | [`electron-run1/`](electron-run1/)宿主元数据及结果落盘并正常退出 |

两端使用的正常 `benchmark.html` 散列均为 `d6264e7dd53caf89eddf78986e3d6b09ddf278a9cde3e050641127ab8ca2f74b`。`shared-page/`保存正常入口和全部资源；SOUI 的 `soui-smoke-page/`只把测试脚本置于受信的 `index.html`，其他资源相同。该测试脚本不会用于正式对照。两架构 SOUI 宿主 Release 程序散列分别为 x64 `6dd90b20b307955ef56eab3ead29a976d0d0ff082fbdba146c369c8cca2024f9`、x86 `524a914226be99215a2329046067db97f3b18328f12c0b986920c204110e120e`；Electron 锁文件散列为 `db51b0767e5a03de455d8dae6beb3719ec3ab60f458346caa6fb460c190307f4`。SOUI 使用此前归档的 preview.4 双架构独立消费者安装 SDK 与内核。

最初 SOUI 探针尝试加载 `benchmark-smoke.html`，被公开 SDK 的本地受信入口规则拒绝，[`source-navigation-rejected.log`](source-navigation-rejected.log)保留拒绝观察。改用独立测试目录的 `index.html` 后，Bridge 接收 `begin` 但首个 `chunk` 失败；根因是公开 Bridge 规范化 JSON 对象时按键排序，宿主解析却假设网页提交顺序。修正解析并全新重跑后，x64/x86 均完成 `begin → chunk → commit`，[`source-navigation.log`](source-navigation.log)保留允许访问观察。上述失败不是性能数据，不能用后一次成功覆盖缺陷历史。

正式对照仍缺自动输入轨迹、完整样本量、物理呈现采集、整进程树资源与包体积、同机交替运行及报告。S01 探针约 1 秒完成，页面事件和 rAF 样本为空，不得用于计算启动或帧性能。
