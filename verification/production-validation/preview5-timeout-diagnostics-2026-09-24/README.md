# preview.5 x86 Bridge 超时诊断与复测

日期：2026-09-24。目标是为 [冻结载荷的两次千轮超时](../preview5-lifecycle-2026-09-24/README.md)留下可定位的现场，而不是放宽 20 秒单步门限或宣布问题消失。两次新长测和旧源码对照均运行于 Windows 11 x64 宿主中的 x86 独立公共 SDK 消费者；不是 Windows 7 客体长测。SDK 与内核运行时代码未改。

监督器现于单步超时时先保存 `timeout-observation.json`：已读原生日志偏移、磁盘文件长度、未读字节、最后解析事件、当前步骤和日志末尾。可通过 `SOUI_ACCEPTANCE_DEBUGGER` 指定 x86 `cdb.exe`，并通过 `SOUI_ACCEPTANCE_DUMP_ROOT` 指定仓库外的绝对目录，在终止消费者之前尝试保存所有线程栈及完整转储。未配置、抓取失败或抓取成功均不改变超时失败判定；转储可能包含进程内敏感数据，留在仓库外。受控 x86 PowerShell 进程的实际调试器测试已生成 49,338,470 字节转储（SHA-256 `b25448aac3abeb367e2e0f4027264cfa58cec7688b03e7f799372784edb155b1`）和 10,796 字节线程日志（SHA-256 `c3b0cfd5166e0c64aee06e2f7a3074e448ac3800e62d750c3cb3c6c3416d8b57`）。这只证明抓取链路可运行，不是 SDK 故障栈。

新源码指纹 `9c2fbfe61094b2a5dbf86e58c010736df2c772aed40295a3103ad12d17005d3c`，x86 SDK build-id `5ecdd31c-2a38-4e77-b662-80c1fa98d975`、独立消费者 build-id `d0e3b526-2cd7-4ae6-9b22-e929c3c806f9`，运行载荷 SHA-256 `ed3c7c823b6afa3a42d0fa7786f512d1425447c687750aedf9db1e660707cb15`。身份文件为 [SDK 构建](sdk-build.json)和[消费者构建](consumer-build.json)。这是与原失败载荷不同的来源，不能把成功次数并入原冻结样本。两次运行均开启 `SOUI_ACCEPTANCE_MOUNT_TRACE=1`；阶段日志不参与通过判定。

| 运行 | 配置与结果 | 原始证据 |
|---|---|---|
| [诊断 A](x86-1000-bridge-a/result.json) | 3 次预热 + 1,000 次测量；20 ms 静置；20 秒单步门限；exit 0、无超时，2,010 个步骤完成；10,030 条阶段事件 | [事件](x86-1000-bridge-a/events.ndjson)、[阶段](x86-1000-bridge-a/events.ndjson.phases.ndjson)、[请求](x86-1000-bridge-a/request.json) |
| [诊断 B](x86-1000-bridge-b/result.json) | 相同配置；exit 0、无超时，2,010 个步骤完成；10,030 条阶段事件 | [事件](x86-1000-bridge-b/events.ndjson)、[阶段](x86-1000-bridge-b/events.ndjson.phases.ndjson)、[请求](x86-1000-bridge-b/request.json) |

A/B 的 run-id 分别为 `61ac2aa5-047c-4e01-a3c9-cc74c24c57e1` / `ca78e8be-f0eb-480c-bc7a-08341fb79e8e`；原生事件 SHA-256 分别为 `ec8f95e1e65ae265d75571f5f821fcd81f63263357b0016990dcece089671e27` / `306dd3a704cb74592f2a6e97014f389de3818fe3b2c0327eecd1ed1e00c7cf8a`。归档的每个请求、结果、事件、阶段、stdout/stderr 与运行目录逐文件散列一致。最久挂载分别为第 20 轮的 1,800 ms、第 748 轮的 1,034 ms；旧失败点第 977 轮分别在 772 / 941 ms 内挂载完成。因此均未触发调试器，**没有捕获故障现场**。

两次运行每百个测量周期的 Private Bytes 中位数首段至末段分别为 82,884,608→96,071,680 与 84,809,728→99,282,944 字节；峰值为 119,107,584 / 119,533,568 字节。最后测量周期分别为 109,916,160 / 107,016,192 字节，Runtime 关闭后为 16,130,048 / 18,767,872 字节。成功退出和末点回落不足以说明资源收敛；`resourceGate: not-evaluated`、`releaseReady: false`。

另以原失败源码指纹 `16d1dccc47ebacff83bf0a94abeed21c75cd7bcd049559802598a5e44656f237` 重新编译独立消费者并保持原 20 秒门限运行一次千轮 Bridge；它与旧失败样本的**源码相同，构建和二进制载荷不同**。只读 [旁路观察器](harness/watch-original.cjs)在连续 8 秒无原生事件时尝试抓取现场，不替代原监督器判定。此对照与前述两次新载荷结果分开统计。

原源码重建的 x86 SDK build-id 为 `54bcefce-e9d1-4a1a-98b1-15b703a1c7e2`、消费者 build-id 为 `dcd20c44-a310-4f3f-b3f2-0db8a2dcac4f`，载荷 SHA-256 `fbddfb47ee6fd20e059e0b9b24ec96eb05f98bf684cc8edb0967eb25a9caa034`，与旧失败载荷 SHA-256 `53c4e8fd50b7fbc1c82854863d06e29d80076ae6393478cecf2dad702e589289` 不同。独立身份见 [SDK 构建](original-source-sdk-build.json)及[消费者构建](original-source-consumer-build.json)。[对照运行](original-source-rebuild-x86-1000-bridge/result.json)的 run-id 为 `e064f4d9-bf3c-4733-b299-46d6bba75d0e`；3 次预热 + 1,000 次测量、20 ms 静置、20 秒门限，2,010 步全部完成，exit 0、无超时。原生 [5,028 条事件](original-source-rebuild-x86-1000-bridge/events.ndjson)的 SHA-256 为 `c340e7003a18e6498ebb07a4bee89185161838c12f2ba4414ee4cf5e74f7f284`；[观察器报告](original-source-rebuild-x86-1000-bridge/external-watch.json)为 `no-stall-observed`，未生成故障栈。请求、结果、事件、观察器、stdout/stderr 从原运行目录复制后逐文件散列一致。

该对照每百轮 Private Bytes 中位数从 84,500,480 升至 96,147,456 字节，峰值 116,236,288 字节；预热末点 49,565,696、末轮 88,797,184、Runtime 关闭后 17,883,136 字节。资源增长/回收需解释；`resourceGate: not-evaluated`。至此原冻结载荷仍为 **两次千轮失败、三次诊断成功**；原源码重新编译的成功对照是另一个二进制载荷，既没有捕获原故障，也不能撤销它。生产零超时门槛及 `releaseReady: false` 保持不变。
