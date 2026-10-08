# WebView2 实验与 Miniblink 回归证据

对应 [验收报告](../../../docs/acceptance/webview2-experiment.md)，日期2026-10-03（Asia/Shanghai），事件和结果保留真实 UTC 时间。所有记录仍为 preview；`releaseReady=false`。

## 成功记录

| 目录 | 保存内容 | 实际执行 |
|---|---|---|
| `webview2-x64/` | 独立安装/编译/依赖/载荷身份、两场景结果及原始JSONL事件 | reload、recreate各10轮，退出0 |
| `webview2-x86/` | 同上 | reload、recreate各10轮，退出0 |
| `miniblink-x64/` | SDK/消费者/Web构建身份、四场景结果及原始NDJSON事件 | reload、recreate、multiwindow、bridge各10轮，退出0 |
| `miniblink-x86/` | 同上 | 四场景各10轮，退出0 |
| `unit-tests.log` | 此轮传输、证据验证器和生命周期监督器测试 | 27通过、0失败、1跳过 |
| `standard-unit-test.log` | 标准x64 `-UnitOnly` 入口 | 包14/14、交付12/12、Web74通过/1跳过、CTest7/7 |

跳过项要求额外的 x86 调试器。WebView2 两个构建ID为 `0530a6aa-6a0f-418f-9503-3459b95ffc94`（x64）、`93bd4af4-6e3b-4500-b6a7-f49e6f38454c`（x86），均使用固定SDK `0.3.0-preview.9`、开发包 `1.0.4258.31` 和实际系统 Runtime `154.0.4258.48`。每个结果内的 `logSha256` 绑定旁边的原始事件文件。

本地完整编译、载荷和运行目录仍保留在忽略目录：WebView2 为 `out/webview2/17f57773-1e9c-4152-9fa6-716a1f2931c4`、`out/webview2/8a06df78-5981-4aff-8c7a-5992bb77547c`；Miniblink 为 `out/acceptance/d11aebfa-8b0b-4d0f-a927-ca5efa4f9a90`、`out/acceptance/256821f3-093b-4435-a266-514e4c048e82`。复验会生成新身份，不覆盖旧结果。

## 保留的失败

- `compile-failed-first.log` 与 `compile-failed-lambda.log`：首次 Win32 哈希接口可见性和嵌套捕获编译错误。
- `failures/ab60a2e7-1959-4670-82ec-33c2735f1ece/`：最初 reload/recreate 就绪失败，两次原生退出1，监督器未超时。
- `failures/0bcb272a-b0fe-4ac8-9737-daf13513204f/`：页面诊断显示计时器探针失败。
- `failures/2d4423a6-89dc-4d05-b2bb-35d9dd54e0ed/`：更细诊断记录 `result=120`、`ticks=0`、`visibility=hidden`，据此修正实际窗口启动状态。

失败构建和运行使用各自的源码摘要，不能作为最终成功构建的结果。JSON、JSONL、NDJSON均按原始字节复制，Git属性禁止转换事件日志换行；编译与单元日志归一化开发机根路径为 `<SDK_ROOT>` 和换行为LF，编译失败日志另清理行尾空格和末尾空行，错误、结果与计数未改写。未提交二进制、用户数据目录或依赖缓存。

## 证据边界

WebView2 的 `terminationConfirmed` 只表示直接启动的原生进程退出；`processTreeGate` 与两后端 `resourceGate` 均未验收。Miniblink 原始资源样本包含短循环增长，没有在此宣称资源收敛。应用载荷大小不含系统 Runtime。40轮 WebView2 与80轮 Miniblink短循环不能替代1,000轮、8小时、性能对照、平台/真实输入矩阵和生产发布验收。
