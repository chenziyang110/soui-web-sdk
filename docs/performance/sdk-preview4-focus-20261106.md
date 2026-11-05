# SOUI Web SDK 焦点缺口与预算对照追加验收

结论：**已确认旧 MiniBlink 失焦缺口，私有候选在本轮窗口切换中满足限定功能复验；整体输入延迟和帧率验收仍未通过。** 此报告接续[重载修复与追加验收](sdk-preview4-continuation-20261106.md)，不覆盖旧失败或修改冻结的 SDK `0.7.0-preview.4` 包。机器记录见[continuation-r31.json](../../verification/performance/sdk-input-frame-20261105/continuation-r31.json)。原始证据路径均相对于本地验收归档 `PR129-performance-20261104/`。

## R30：已确认的普通失焦缺口

复用已有、实际构建通过的 full/manual 输入消费者，EXE SHA `03d38044cf611ebf50d60946dc22fdf60801ee1e27572e03294d3c0de556f537`，没有改页面或新增采集器。SDK x64 Release build ID 仍为 `18e47fa2-69df-4530-b1f9-365f6b2aec28`，MiniBlink DLL SHA 仍为 `56d460c0b4136d85d3fcb8d4e4f00c19a04ff56532228031b55368742ef54308`。

run `8a12e79a243d3336a8eb8bdb9f962a13` 的两个任务窗口均正常关闭，原生进程/监督器/驱动退出0、Job0，无强制清理，总耗时49.717秒。SDK/vendor trace 分别4734/26217点，完整、无 loss/fault，同一 run/PID/进程诞生时间及 view2/document2/geometry2 可关联。

- focus66→blur67→focus68 均有唯一 host_receive→accepted→engine_begin→handler_return 链。SDK 的 required `wkeKillFocus` 已调用并正常返回；这次不能归因为 SDK 没路由失焦。
- 页面 Finish 点击 input91 的 Engine 派发与同因果 bootstrap 请求，将累计快照限定在 QPC `[1396180899116,1396180921850]` 的源码/动作区间。blur67 返回 `1396120948100`，早于下界5995.1016 ms；快照 `lifecycle.blur=0`。这是功能错误证据，不是输入延迟读数。
- 排除启动期 doc1 的 stale focus1；快照先在内存接纳，正常 Runtime/trace 收尾后才落盘。不把关闭事件、接纳时刻或 rAF 计数混入失焦判断。

实际证据：`repair-r30-focus-functional-r1/analysis-r1/actual-focus-chain-r1.json`（SHA `c9aa4fbc436c1ca9811979c552882a8ad0a7ce6ae612a783bdf0690dcf170f0e`），独立审查 SHA `144c57a292c6ac655a07416b557df50dbf930380f9439093a589054991c74f43`。证据确认 normal blur 行为缺口，不唯一定位 DLL 内部分支，不认证 IME、input-to-photon 或物理 FPS。

## 修复边界

[ADR-0164](../adr/0164-miniblink-keyboard-focus-loss.md)冻结私有候选设计：修正 NULL/NULL 被误当本页 popup 的条件，并评估仅恢复 FocusController 的键盘失焦。保留正常 caret/selection 焦点变化和 DOM 回调，不额外清空 frame/element/range，不用恢复/取消 API 代替普通 blur。

完整 Widget setter 会确认 composition 并关闭 IME acceptance；SDK 原生 FIFO 还可能收到最终 commit/end。因此不能直接恢复注释的完整 setter，必须验证晚到 commit、重新聚焦、popup 回调、导航和关闭。两份独立源码审查只批准私有 prototype，不代表默认采用或完整 IME 通过。

候选进一步限定为现有 SDK broker 受控 view。`CHECK_FOR_REENTER` 本身不保活 `WebPageImpl`；legacy raw 销毁回调另有生存风险，新失焦事件不能未经验证扩大到该路径。非 broker 路径保留原行为，必须核查 broker 活跃回调的销毁拒绝，不能只靠状态检查声称安全。

旧单文件编译头闭包已核对：两架构各1331个唯一实际包含文件，与冻结的包含文件 pin 全部匹配；这不表示全部历史工具未变，也未扫描完整源码或冒称全量内核资格。真正构建仍必须复核完整链接输入、所需工具、头闭包和新编译的实际 includes。SDK 包、旧 DLL/OBJ 和既有收据保持原字节。

私有候选函数 SHA `79127218104da00d9a44f06ccb269066be6c8e920f2166bb665e8f36001221de` 已完成 v143 14.44 / SDK19041 的单 TU 源码投影验证：27项合同通过，两个旧代码负对照在同一失焦断言上失败，原始退出分别0/1/1。configure/build 和两次编译助手均自然退出0，五个 Job 均空、无强制清理。它只证明候选控制流与既有 broker 许可判断，不能把 fake FocusController 当实际 DOM 或 OS IME。

首次私有内核构建在准入阶段因历史缓存 `pwsh.exe` 身份变化停止，架构结果为空；未启动任何编译/链接或 GUI，不生成新 DLL。失败保留，后续只能先核实实际工具闭包，不能直接更新所需工具的旧 pin 或盲目重试。

## R31：私有候选实际构建与窗口检查

随后确认旧 `tools` 集合混入历史对象、日志和未使用的准备工具。R3 准入保留实际链接对象/库、两侧1331头文件、真实编译器及原 CRT 库的旧 pin，仅排除实际闭包之外的历史记录。另发现两架构 `OLDNAMES`/`USERENV` 共四项旧 pin 缺失，明确以当前同安装工具链中新冻结的字节纳入；它们的历史等同性为 **unknown**。因此只能认证可标识的私有功能构建，不能声称整个构建只有一个输入改变或获得配对性能资格。

R3 计划 SHA `72bc02940e6ecd994d16cb9124f7b1a440cc232e635c107d9da949a89b039de7`，实际构建30.082秒、退出0。LLVM22/MSVC142 的 x64/x86 单文件编译及链接四阶段均自然0、Job0，无强制结束；实际头闭包各1331项且未准入0，PE 架构及各自导出集合通过。它不是完整 v143 内核构建或 DLL 运行 ABI 矩阵。

| 私有 DLL | 字节数 | 相对冻结 DLL | SHA-256 |
|---|---:|---:|---|
| x64 | 39,212,544 | +512 B | `a4f573415d0bc38d420088d3f70a14d66e42b5703707e6220015e5f476a147cd` |
| x86 | 28,325,376 | +1024 B | `365d3ff387e26ab4f8bec6cb24d09c20bdf66fb4ff6d4e1a7cae46f25154ccc4` |

实际构建记录 `repair-r31-focus-prototype-r1/kernel-build/build-result-r3.json` SHA `2f2dbd25e065e7a9c2c100f147aecbd54b47105c2d96ec413ae95b4f665a6c3e`；独立收口审查 SHA `fb75f585206b6f2b4f072d31dd027df9bc733e7ef32d6463975587d37acb2043`。没有生成新 SDK/runtime/ZIP，不能把这两个 DLL 增量直接写成完整交付增量。

x64 私有 DLL 逐字复制到新测试目录后，复用原 R30 launcher、消费者和页面，执行一次任务窗口切换。run `d942d91fdbecd8b5c48b8abe8833477c` 的原生两进程、监督器及入口均0、Job0，无强制清理，52.471秒正常完成；页面接纳前可见 blur2，正常关闭后原快照持久化 `lifecycle.blur=2`，无 hidden/resize/overflow，Runtime 正常关闭。只操作本任务两个 SOUI 窗口，没有再测 FPS 或启动 Electron。

[未默认应用的函数补丁](patches/0164-sdk-focus-private.patch)供审查，`git apply --check` 实际退出0；仓库 vendor 源码仍为原字节，SDK4 包未覆盖。该补丁只含失焦函数差异，完整私有编译原件另绑定 source SHA `b26a19f24aef7d85297a5b5b2041caba52a4b64d1a091efe3b9e986a0e7fa4e2`。

同一未改脚本的离线分析最终退出0，SDK/vendor 原始 trace 分别4400/22782点，完整、loss0/fault0。view2/generation1/document2/geometry2 的 blur20、blur22 已正常返回；focus21→blur22→focus23 的完整 accepted 派发链可关联。Finish46 的源码/动作快照区间为 QPC `[1441543103221,1441543121121]`，两次 blur 返回分别早于下界10439.5633/5410.4195 ms；累计页面 blur2。因此满足“快照前至少一个正常返回 blur，且实际页面累计 blur>0”的预定限定条件，不是一对一 DOM 映射或输入延迟读数。关闭后的事件没有补入快照前证据。

实际分析 `repair-r31-focus-prototype-r1/analysis-r1/actual-focus-result-r1.json` SHA `d9c3f584f4b1b6b9654c969113bd004090b0bfb20da2f111a694ce0409999b5f`。首次离线分析因最终绑定证据时缺入口审查文件而退出1，失败 SHA `5583156efc9c2734d83df30bdf43c0ce840b7c93f00dec4e97176812400325f0` 保留；随后审查文件明确是**执行后的追溯入口审查，不是事前批准**（SHA `882b5a7a56cee51d14c67b0dd8ee0c48fe38fb7dad055484578f1ea9b55454ca`）。补齐记录后只重新分析原数据，没有改判据或重开窗口。真实 IME、完整焦点路由、默认采用及性能资格未据此通过。

独立实际复核 `repair-r31-focus-prototype-r1/analysis-r1/independent-actual-focus-review-r1.json` SHA `0fd750d11d1883b1d695b9ec24056c3dca8823a7c1e05d2654990f7e153e9e6b`，核对26项证据 pin、原始派发链和部分时序后确认上述限定条件；没有重启测试。另执行本次提交的 SDK Web 依赖审计，退出0、漏洞0，仅限 `sdk/web`。

## R29：不重复追测的预算对照

为补齐此前未测的2000预算，在新的空闲资源窗口启动预先冻结的0/0/2000/0序列。前两个0基线各成功接收3750输入，完成32次预热和1024次固定4 KiB/最多8在途 Bridge 调用；进程均0、Jobs0。数据完整不等于基线可比较：Bridge P95 两侧2.215/1.970 ms，跨度0.245超原阈值0.2215；P99两侧3.370/4.705 ms，跨度1.335超0.4705。

原控制器据此停止，B0/B1 控制器原始退出分别0/1（Host/Input均0），**未运行2000候选或最后0档**。不放宽阈值、不改期限、不重复采到满意数字；默认预算继续0，I02保持未完成。两个基线的 queue P95/P99 桶上界同为0.8191/1.6383 ms，Canvas callback 约37.49/37.31 Hz，均不能当作物理帧率或逐输入可见尾延迟。

证据：`repair-r20-budget-input-consumer/same-consumer-native-witness-r1/scalar-2000-vs0-r29/summary-r1.json`（SHA `6b52b99725b3f67c9e583bf0365cbcb48921f4b19aa327dfb3a19751308cec61`），独立 closeout SHA `18f4ef7279acc575333bef421767877d24f7f5510295e023f4a0993190ad0471`。外部噪声原因未知，不据此宣称任一策略因果退化。

## 仍需关闭的门槛

普通焦点修复的默认采用与真实输入法验证、完整输入到可见反馈、有效帧与物理显示边界、I02完整对照、W02持续 owner 公平性、配对CPU/完整内存、拖放/DPI/多屏仍开放。冻结产品体积仍只适用原匹配载荷；私有候选若未打包，不给出新的 runtime/ZIP 增量。`performanceVerified=false`、`inputLatencyVerified=false`、`releaseReady=false`；PR129保持draft，不合并或发布。
