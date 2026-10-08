# preview.5 挂载阶段诊断

日期：2026-09-24。针对 [同一 SDK 候选的两次 x86 Bridge 千轮挂载超时](../preview5-lifecycle-2026-09-24/README.md)，只在公共验收消费者增加可选分阶段日志；SDK/内核运行时代码未改。诊断源码指纹 `d557823617e290ead5b7ea79ee27a34cadf7408042765ccbedbb73fbfc4cff2b`，与原固定载荷源码指纹 `16d1dccc47ebacff83bf0a94abeed21c75cd7bcd049559802598a5e44656f237` 不同，运行结果不得混算。两套消费者均在 Windows 11 x64 宿主运行，**不是** Windows 7 客体长测。

诊断开关为 `SOUI_ACCEPTANCE_MOUNT_TRACE=1`。测试程序在常规 `events.ndjson` 旁写入 `events.ndjson.phases.ndjson`，每轮挂载分别记录构造、窗口创建、Host 启动、显示和页面 ready 等待的开始/结束，并立即刷新。记录使用 `GetTickCount64` 单调时钟；标准验收事件与判定没有放宽。外部只读观察器连续 8 秒看不到标准事件增长时才尝试抓原生线程栈，观察器结果保存在长循环目录。关闭开关的短测验证不生成阶段文件。

| 运行 | 配置与结果 | 原始证据 |
|---|---|---|
| x86 阶段短测 | Bridge 3 轮，0 次预热；exit 0、无超时；30 条阶段事件 | [结果](x86-3-bridge/result.json)、[阶段](x86-3-bridge/events.ndjson.phases.ndjson) |
| x64 阶段短测 | Bridge 1 轮，0 次预热；exit 0、无超时；10 条阶段事件 | [结果](x64-1-bridge/result.json)、[阶段](x64-1-bridge/events.ndjson.phases.ndjson) |
| x86 默认行为 | Bridge 1 轮，关闭阶段开关；exit 0、无超时；未生成阶段文件 | [结果](x86-1-default/result.json)、[事件](x86-1-default/events.ndjson) |
| x86 长循环诊断 | Bridge 3 次预热 + 1,000 次测量；每轮静置 20 ms、单步 20 秒；exit 0、无超时，完成 2,010 个生命周期步骤和 1,003 次挂载 | [结果](x86-1000-bridge/result.json)、[标准事件](x86-1000-bridge/events.ndjson)、[10,030 条阶段事件](x86-1000-bridge/events.ndjson.phases.ndjson)、[外部观察](x86-1000-bridge/external-hang-watch.json) |

x86/x64 SDK 构建和独立消费者的完整身份分别见 [x86-build.json](x86-build.json) 与 [x64-build.json](x64-build.json)。x86 SDK build-id 为 `bbcc7317-6392-4b97-84de-e7a9c214f915`，消费者 build-id 为 `183cb430-f514-42d0-a5b3-56f3f88f41ff`，运行载荷 SHA-256 为 `7f6b3b275ab60d769a39149daa6ffab5fad111cf9f3055f60c4bbf8d23212075`。x64 短测 SDK build-id 为 `26a01958-f8b3-40b1-a3f7-d36058e81028`。归档的 26 个原始文件与测试目录来源逐文件 SHA-256 相同；标准长循环事件散列 `594ed645de115b21d36ca29f07f85f678b71c5abb91d6c4b5a9a5591f70d2357`。

本次最长挂载是第 53 轮的 1,423 ms；原失败点第 977 轮在此诊断载荷中完成。外部观察器输出 `no-stall-observed`，因此没有停滞线程栈，也没有定位根因。千轮每百轮 Private Bytes 中位数从约 79.4 MiB 升到 96.0 MiB；峰值 119,771,136 字节。预热末点、最后一轮与 Runtime 关闭后的 Private Bytes 分别为 49,500,160、63,713,280、19,468,288 字节。分段总体上行仍需解释缓存/对象所有权；不能仅凭末点较低判定无长期增长。`resourceGate` 仍为 `not-evaluated`，`releaseReady=false`。原固定载荷的两次超时不因本次成功而撤销；需要复现时抓栈并定位修复后才可重跑生产门槛。
