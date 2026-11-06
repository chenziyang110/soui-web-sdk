# SOUI Web SDK preview.5：集成验收接续

**结论：新SDK的标准x64构建、源码包安装及471文件原生安装已通过限定核验；真实输入延迟、有效帧率提升和持续负载公平性仍未通过验收。** 本轮没有重复旧帧矩阵，也没有启动Electron。旧preview.4包与失败原件保持不变。

## 实现与交付身份

`0.7.0-preview.5` 只在本轮新增WebView2活动drain内的输出通知合并。首个实际pop在原mutex段取得活动标记，worker入队由当前drain接续，退出时若仍有输出才补通知；生命周期通知独立，原空pump尾部的post失败重查保留。没有新增线程、timer、队列状态查询或公共API，没有改变输入、lane顺序、COM前额度释放、业务deadline或owner STA。设计与回退见[ADR-0167](../adr/0167-webview2-active-output-wake-coalescing.md)，私有组件验证见[活动输出报告](sdk-preview5-owner-output-wake-20261106.md)。它不等于完整跨slice公平性修复。

正常工具将当前源码及官方编译GUI打包为1594文件，再安装到全新源码SDK目录；没有覆盖旧安装。随后实际执行安装SDK内的 `tools/Build.ps1 -SdkOnly -Architecture x64 -Configuration Release -WithWebView2 -Install`，保留正常Web构建及SDK-local zlib编译，不重建SOUI/MiniBlink内核。

最终受管源码另经原 `verify-source` 实际核对为 `source-matches-package`；本次候选包与源码内容保持一致。

| 项目 | 实际身份或结果 |
|---|---|
| 源码SDK版本 | `0.7.0-preview.5`，未发布 |
| 正常Build ID | `c98cec41-40e0-4872-880f-fca107757219` |
| Build source SHA-256 | `92915183c8b1d2d65deb2cf7e2656f728997b0f69f522d4d83108bea7e6799e2` |
| 工具链 | MSVC v143 14.44.35207、Windows SDK19041、C++17、x64 Release |
| 原生构建 | PID65272 / birth`134384513291050222`，116.001秒，自然0、Job0，无强清 |
| 安装 | 471个实际文件；公共头、Core/renderer/Loader三库及版本核对通过 |
| Core archive | 57,437,214 B，SHA`a02b458f1c5d40c000a9467f559a83db8088bc46ec87404f27a57f3cdc63377c` |
| WebView2 archive | 8,406,518 B，SHA`9aceb462651173ce676cdecaef8d2fe96801417ba947413230c3b7154e31feb1` |

构建保留原600秒总监督期限和2秒drain，没有逐阶段重置。首轮在编译前因既有VCTIP事件被原接纳规则拒绝，PID82956/birth`134384511837742030`，1.889秒自然1、Job0，尚未执行Web或编译；遗留助手自行退出，没有终止外部进程。第二轮原生Build成功后，外层核对错误地要求安装清单不得重复，故外层仍返回1。CMake原有两处将同一个LICENSE安装到同一路径，造成472行/471唯一键；实际没有缺失或多余文件。原失败不改写，后续只读文件核查仅允许这一个已知重复、核对许可证内容并重新核对原构建/源身份及471文件闭集，通过后单独落盘；**没有第三次构建或重新安装**。

## 正确性与Web验证

- SDK4基座的私有renderer：原owner策略及10项新增通知责任反例通过，原真实隐藏WebView2容量/续租/拒绝/关闭fixture通过。该组合保留自己的版本身份，不继承为SDK5运行通过。
- 源码SDK小合同16/16通过；SDK5真实1594文件打包、正常安装及原GUI交付校验通过。
- Web客户端31项在首轮通过。首轮Web/GUI合计60通过、2失败：新版本交付映射缺项及新测试对旧元组不可变性的预期错误；修正后两项定向2/2通过。中间1/2失败原件保留，没有声称重新跑过全部62项。
- 官方GUI构建入口对Web/React/Vue执行各自正常安装和编译，实际六条命令均通过；版本、锁文件及精确peer元组为preview.5，未声明preview.6。首次React/Vue使用错误安装模式的失败与后续官方 `--install-links` 成功分别保留，没有升级第三方依赖。
- SDK5匹配消费者已实际编译、链接5TU/4个目标；整体消费者构建因原编译助手退场门失败。现有产物单独核验后，既有23项v2策略合同和最终官方renderer的隐藏容量回归各执行一次通过；该独立功能诊断不把整体构建改为通过。

## 最终消费者：构建失败与功能结果分开

原消费者首次在启动前因遗留VCTIP/MSBuild助手被拒绝，没有编译或运行。检查确认两个旧助手的完整birth/image、原parent已结束、没有编译任务且全部线程等待后，按既有空闲清理授权结束它们；前几次检查拒绝时没有终止进程，也没有停止活跃构建。

随后v143/19041/x64的原配置和四目标编译命令均返回0，Host两TU、输入器一TU、策略一TU及原容量fixture一TU全部编链。配置助手正常收口；build助手PID100904/birth`134384529050978396`没有在原30秒等待内退场，故wrapper/root87600/birth`134384528702168175`实际1、91.744秒，整体构建门失败。outer最终Job0、无强清；没有把助手的未知个体退出码补成0，也没有延长30秒或再次编译。最后原始分组已不含助手，不能将其消失归因为root退出、Dispose或外部动作；具体原因未知。

没有生成成功的inner/accepted构建摘要。另一个独立诊断先核对原输入、独审、实际5个obj/4个EXE、CL/link读取与命令、SDK5头和三库、版本/单trace recorder及资源闭集，且把上述**准确失败**作为诊断前提。首次文件核对误拒了CL日志中的Windows排序/时区数据，尚未执行测试；新尝试仅显式分类这两个实际OS数据路径，保留其历史内容等同性unknown，不放宽业务或SDK头校验。随后执行：

| 独立功能检查 | PID / birth | 实际时长 | 结果 |
|---|---|---:|---|
| 既有v2策略 | 88348 / `134384537772565978` | 74ms | 23项通过，自然0、Job0 |
| 官方SDK5真实隐藏容量/关闭 | 36236 / `134384537778273692` | 8.648秒 | 128接纳/拒绝与恢复、129业务、2清理及Runtime正常关闭，自然0、Job0 |

固定WebView2 Runtime156.0.4314.58，browser79256/birth`134384537779713629`正常结束；没有激活窗口或注入输入。原15/60秒和2秒drain、容量内部55秒均保留。它直接链接正常SDK5三库，不是R44私有renderer，也没有重建SDK或内核。源码/链路及实际功能结果均经非作者审查。`functionalChecksPassed=true`与`buildGatePassed=false`同时保留，**不授权新输入入口冻结或启动**。

## 体积边界

同为v143/x64 Release的471文件静态SDK安装，preview.4→preview.5为80,244,800→80,253,090 B，**+8,290 B（0.010331%）**。其中Core +2,772 B、WebView2 archive +5,518 B，Loader不变；公共头仅版本内容改变、大小相同。包含源码目录/版本身份差异，不能将全部archive增长归因通知逻辑，也不能把静态库增量当最终EXE增量。

两份源码SDK的128个固定依赖DLL清单（含两架构）逐项路径、大小、散列相同，合计均83,701,904 B，增量0、无新增DLL；旧ZIP身份先核对SHA。此为依赖载荷身份对照，不是实际加载或最终应用runtime认证。当前候选源码payload为113,410,645 B；新应用EXE、完整runtime、ZIP与匹配基线的增量仍为unknown，不能用旧preview.4体积数据补齐。

## 性能与剩余门槛

旧R43已发送的3125个正标签在原始回调中全到，但400项晚于旧Finish边界；tag0拒绝及页面fence超时使该轮失败，约26.205秒绘制/定时器间隔仍需归因。新v2工具分开发送完成、接收排空与最终Finish，增加来源/早晚extra/可见性旁证，保留tag0 veto、原负载、3125输入和原期限。其源码审查通过不代表已经编译或采样；无标签消息的来源仍未知，不按推测过滤。

既有WebView2帧对照未超冻结噪声，MiniBlink基线漂移超阈值；I02的0/0噪声门仍失败，默认预算0及输出准备关闭保持。MiniBlink默认focus/IME缺口及私有补丁未默认采用的边界见[preview.4报告](sdk-preview4-final-acceptance-20261106.md)。本轮不对这些未改动路径继承新资格。

新v2 reader/入口的单次Node合同5/5通过，源码独审通过，仅为纯软件合同；消费者构建门失败时默认冻结/启动仍拒绝。最后软件反馈捕获与owner排空封口是独立见证，允许合法先后，不把行政封口强行变成绘制前置条件；完整标签、marker、严格窗口和Finish/fence门保留。下一步先解决编译助手收口的具体原因，取得完整消费者资格，再在读取入口确实就绪后安排一次有新假设的原生采样；不机械复跑相同失败或为了好看数字重复矩阵。输入分位、可见有效帧、CPU/完整内存/成功吞吐与必要交互仍须绑定最终载荷及合格样本。本轮没有执行x86/Debug、MiniBlink新载荷交互或新帧矩阵，不从旧版本继承通过。

## 原件定位与独审

下列路径相对于本任务本地验收包；绝对定位由持久进度记录提供，原件没有改写为通过。

| 原件 | SHA-256 |
|---|---|
| `R44-active-output-candidate-r1/source-code-independent-review-r2.json` | `96e65fd66c88f3be30f9571a3a6dc0589049b041658c3ecf8b3e0c24a550bae9` |
| `R44-active-output-candidate-r1/actual-independent-review-r1.json` | `e32763bf98fbaa95d246636ccbb9936ec9c2be0903f945b236a2fc8887f47ff6` |
| `R46-preview5-integration-r1/run-once-r2/verification-existing-r1.json` | `2ca306778736bceed17d54ee87144e6d52a7beeb220e61f1299324724ce75d4b` |
| `R46-preview5-integration-r1/actual-independent-review-r1.json` | `46cd09956c0a8befa38fe3f803863476ebda8add7390c4380e7c6c3ba1c525be` |
| `R46-preview5-integration-r1/source-delivery-independent-review-r1.json` | `ee26e39d976d204737e9652b39a651186325161b2b30bbc251c8ea3968a2f976` |
| `R47-sdk5-native-consumer-r1/functional-diagnostic-r2/diagnostic-result.json` | `d5b5236ef226612160651a28f21a8a913dc834e06eabc7f2e20eec19ad66e2ec` |
| `R47-sdk5-native-consumer-r1/functional-diagnostic-r2/actual-independent-review-r1.json` | `99fc1d63154c0fad18f45d4e60604d82048110b13a359c45013abfb3600bf059` |
| `R48-sdk5-native-entry-r1/source-independent-review-r1.json` | `32e8aacf2fa23b55f29a3863f8255615ae6de742ba21e9bf6bd91dbefef80fb1` |

R44源码、R44实际结果及R46实际结果分别由非实现作者独审。R46独审限定正常SDK构建/安装，不授予GUI、runtime、FPS或输入资格。`performanceVerified=false`、`inputLatencyVerified=false`、`releaseReady=false`、`wholeGoalComplete=false`。
