# preview.4：匹配输入诊断与软件帧阶段

本记录追加 R40 的实际执行结果。R39 候选与 R40 的 0.6.0 诊断基线均通过固定 v5 的完整64项关联；这解决了夹具在两侧能否读取完整输入链的问题，尚未证明 SDK 性能提升。产品仍是原冻结 `0.7.0-preview.4`，没有重建 SDK、内核或产品包。

## 匹配范围

基线 SDK 为实际 v143 构建的 `0.6.0`，source SHA `4b9104883d43acbab96a20f477eb6bd22643bca858c14ea725ac2894a11070c5`、build ID `e3ecec57-2ec7-48ba-817e-5a749b8e2e17`；不是发布二进制字节身份的声明。基线消费者只通过编译时能力开关避开该版本不存在的两个帧率覆盖接口，版本读取器仍严格检查预期版本。页面、cell、输入器、marker 和 trace 协议保持 R39 原字节。新读取器对既有候选结果的离线重读与原输出相等。

基线使用既有私有 K0 trace-scope 修复内核，node.dll 39,205,888 B、SHA `76df23c64507f3e8fa6d9bf1a2417f2ed04b812443511feec582089c80a98253`。它不是默认发布内核。旧 K0 的 source-loss 失败保留。源码审查核对了双方相同的实际 Gin 工作项、scope helper、trace 头和实现以及工具链；其他 trace 对象存在差异，不声明整个链接闭包只有一个差异，测量税也未认证。

本轮 v14314.44 / Windows SDK19041 的消费者局部构建自然退出0、69.681秒，仅两个消费者 TU；没有重新编译 SDK、内核或输入器。Host 为1,251,328 B、SHA `8266533c21aaddd56b9c71cff2dbe449f45454dcb032cd0fa1516e1837b65bbf`。版本反例定向测试3/3通过，没有重复旧全组。

## R40 实际结果

run `e83827c74fcae98b5cfd3da58178198b`，请求 SHA `a02f360b6116f702429d2f5c0b66a4df4f0afd4f669bac5bb1aada096c43b92b`。Host PID57328/birth`134384318550839252`，10.933秒；输入器 PID81288/birth`134384318648904396`，654ms；入口及两个进程均自然退出0，Job最终0，无强制结束。启动前核对实际加载的 node 路径及该文件 SHA，不将文件散列称为进程内存散列。

64次输入全部通过原生标签、完整 tuple＋ID、T1/T3/正常T4、getter、DOM首次setter严格顺序及坐标相等检查。原生67行中3项tag0、DOM66行中2项非标记回调均保留；测量集合没有裁样。冷读为0；真实 Engine point12 的独立负控制发生在前一项非标记输入之后、64项 burst 之前。36次捕获，最终完整64位图；SDK/vendor trace完整、loss0、fault为空。原通用 scalar 可用性字段仍为false，资格来自本轮独立的严格诊断检查，未篡改旧字段。

| 指标，64项 nearest-rank | 0.6.0 诊断基线 P95 ms | preview.4 诊断候选 P95 ms |
|---|---:|---:|
| T1→T3 | 1.1096 | 0.6111 |
| T3→正常T4 | 0.3988 | 0.3664 |
| T1→首次正捕获结束上界 | 35.3206 | 35.0122 |

两侧都是带同步探针、完整trace的轻页短诊断。单轮差值不构成超过噪声的收益，不能据约0.31ms的捕获上界差异宣传输入改善。请求中心精准落点仍0/64，delta分布与R39相同；cell通过不关闭旧精准点偏差。首次正捕获的end是软件观测上界，不是物理T8。

可审查的[基线夹具源码快照](../../verification/performance/sdk-input-frame-20261105/diagnostic-source-r40/README.md)保存19个原字节文件，共183,896 B；相对路径清单SHA为 `671b7b0f7ec98afe636d25f18faedeeb09dde29c73680e0154de6ed8c2f56aa0`。它不包含开发机操作驱动；新地点未重新构建，不能借此沿用原二进制资格。

## 复用 R39 原件的软件帧阶段

没有新开窗口或重测。使用仓库既有 `input-frame-lineage.cjs` 对R39原始SDK/vendor事件离线关联，再逐项计算QPC差值。64个输入全部关联到软件提交，其中44个不同的软件frame，0项拒绝。独立审查复算了算术和边界。

| 软件阶段 | 口径 | P50 ms | P95 ms | P99/最大 ms |
|---|---|---:|---:|---:|
| T1→软件submit | 按64个输入 | 8.2852 | 17.0821 | 22.5925 |
| 正常T4→processed | 按64个输入 | 4.6004 | 12.0109 | 14.9079 |
| 正常T4→submit | 按64个输入 | 7.9073 | 16.7785 | 21.1756 |
| processed→submit | 去重44个frame | 1.6992 | 10.8939 | 14.5095 |
| activated→submit | 去重44个frame | 0.5771 | 1.0356 | 2.1694 |

按64个输入加权的processed→submit P95为9.7617ms，与去重frame口径不同。软件阶段关联没有证明某次捕获中的像素来自该frame；22个首次正捕获区间横跨关联submit，不能把捕获end与submit的差全部归为呈现等待。事件没有相应due字段，不足以判定定时任务迟到，也不足以认定F03是当前主瓶颈。

F03继续保持不采用：ready-now、已到期、frame/scene/resource就绪的唤醒提升需要先解决精确一次计数、任务分配/准入失败后的有界恢复以及Host存活期。单独提升唤醒会绕过既有pacing/fence或丢失唯一工作项，现有模型不能直接接入默认内核。这里是具体风险评估，不是省略测试或推断已经取得收益。

## 证据与结论

以下路径相对于本机验收包 `PR129-performance-20261104`，不是仓库命令：

| 原件 | SHA-256 |
|---|---|
| `repair-r40-matched-baseline-r1/entry-r1/actual-baseline-full64-review-r1.json` | `f28d86507e0a2cef126fb3917b4400a2317699edb9a670728c5737c55c9e888c` |
| `repair-r40-matched-baseline-r1/current64-software-frame-lineage-r1.json` | `12b60cabf2646362622d5f2b599e94b5d958efede475257f9965105512c282c2` |
| `repair-r40-matched-baseline-r1/current64-derived-frame-phases-r1.json` | `b42b9f4182fb3a81fc1be8c046c4613ead6bb5002368fb8bbd3f0d764a90d094` |
| `repair-r40-matched-baseline-r1/current64-frame-phase-independent-review-r1.json` | `5ebe551f7b671f37dab0a52fa2df9f1b7089faa17334de028d10ffe3945622b0` |

第一次在REPL跨realm传递Map被原读取器拒绝的错误另存；没有放宽读取器，随后以普通CJS入口读取原件成功。没有重跑两后端八轮帧矩阵。`inputLatencyVerified=false`、`performanceVerified=false`、`releaseReady=false`、`wholeGoalComplete=false`。后续缺口仍包括重负载输入、可比测量税/噪声、WebView2持续owner公平性与适用交互；诊断完整性通过不能替代这些门。

报告独审逐项核对数字及边界，收据 `repair-r40-matched-baseline-r1/report-independent-review-r1.json` SHA `73c52bc73352a243777fab2103ed736568c2a7ede411a8d60dd33cfe7c2e8fdf`。提交前实际SDK Web依赖审计退出0、该范围漏洞0；未改依赖，不能外推全仓库或内核依赖均已审计。
