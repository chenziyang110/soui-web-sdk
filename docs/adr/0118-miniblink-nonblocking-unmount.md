# ADR 0118：MiniBlink 关闭的分阶段继续状态

## 暂停中的再次 debugger 与清理回调重入规则（2026-10-23）

当前 SDK 与内核保持原调度及线程契约。普通 SDK FIFO 不在暂停循环中整体泵送；isolate 中已 ready 的 Promise reaction 可以执行。公共 Host 生命周期操作由 owner UI 串行调用，engine 回调需要请求 UI 调度，不能直接并发调用 Host.close。

独立场景114已实际证明：ready reaction 中第二个 `debugger` 前后的真实同步观察均为原 session/context、pause `1/1/0`、pins held、没有 close 或 Resume；post 后独立 UI 观察2秒，再恰一次 Resume，固定返回7、fresh pause `1/1`、页面和原A存活检查及严格退休通过。由此将“第二次暂停被抑制、原暂停继续保持”作为本内核的规则，不要求为支持深度2修改内核。该轨迹不直接证明命中了哪一个内部 guard。

独立场景115在真实暂停中调用公共 A.close，仍及时返回 pending 并撤销旧 receipt。原调用和暂停退出后，应用 Bridge override 的 `set_event_sink({})` 完整转发给真实 json_bridge，再在同 owner UI 清理栈内调用同 Host.close 一次，实际返回 `E_BUSY`。这一次外层是 adapter 自动重试，公共 close 外层标记为false；frontend在回调后才完成 drain。因此保证是重入不再次执行 unmount、不释放仍有借用的对象，不能把拒绝层或 frontend drain 先后固定成另一种契约。调用方仍需保留 Host、adapter、Bridge 和捕获，直至真实退休。

snapshot 的标识释放与本地 `session=0` 发布之间存在短窗口。夹具115仅在原 fresh snapshot 已核原调用返回、pause/dispatch归零且计数平衡后，才发布绑定原session的平衡见证；清理回调遇已释放snapshot时只接受这个见证。此修改仅影响观察器，不放宽 SDK 关闭条件。

来源：app `169611/859c903f…`，Host `215135/1a0d873b…`，SDK `50059136/91ac4447…`，kernel `39053312/9b516490…`，EXE `1119744/9209b012…`，ISO `52318208/719433c9…`。114 run `62740e0e…` native3656/owner25900ms；115 run `12aa9a1d…` native1656/owner24365ms，均自然退出0，原30/40秒不变。两个独立运行审查接受，当前证据入口为 `out/devtools-stage0/miniblink-sdk-close/nested-cleanup-cases-2026-10-22T17-03-48-558926Z/guest-5706d955163c402c94e79843e543982c/attempt-outcome.json`。环境另经保留句柄、自然退出和双EOF核验，并正常释放。

这两项与113的暂停中 bootstrap/关闭交叠共同定义已验证规则，不将所有Promise类型、错误线程调用或任意回调叉积加为原型A的新增门槛。A.1–A.7矩阵及一次有限源码影响独审现已完成，原型A技术可行性获接受（13158/af077b99…），没有确定需要补测的回归阻断。独立调试控制队列逐项检查两端存活、资源状态和view/document当前性；geometry只在open时完整核对，不宣称逐项重验全部identity。旧四关闭/A7成绩仍绑定原载荷，稳定生产候选的回归尚待执行。现在转阶段0 B/C，公共API冻结与生产验收仍未完成；以下历史记录保留各自产物身份。

## Bootstrap 回调与关闭交叠：真实缺陷已修复并验证

真实基线 `2092f7f98a61436b937c8a08fc5ea50d` 在同一暂停和 bootstrap 回调尚未返回时调用公共 A.close()，立即撤销旧 receipt 并返回 pending；原调用退栈、前端排空和 A 实际退休后，同一个业务 Bridge 的新 bootstrap 返回 `E_UNAUTHORIZED`。SDK 对本次新 session 的精确 shutdown 次数为 0。此后夹具才清理遗留 session，不能计为 SDK 成功。原生退出1，owner21.551秒，授权至结束1.562秒，原30/40秒未变。基线失败保留。

根因是 bootstrap 已在 Bridge 建立新 session，Host 却在记录这个 session 前因关闭而拒绝响应；后续关闭只看到了旧 session。修复仅修改 Host 的 bootstrap 交接区块：先提取本次成功响应的 session，未提交时在 Host 锁外精确撤销；提交后交由既有文档/关闭路径拥有。保持原响应及非 bootstrap 操作行为，不改公共接口或内核。

同一修复候选现已实际通过一次环境恢复验证：run `a619b97f62f44991a5394779c3959d26`，native0、owner26.576秒、原生授权至结束1.469秒，原30/40秒不变。Host `1a0d873b…`、SDK `91ac4447…`、EXE `872d8eaa…`、ISO `3069698a…`；app、页面与断言仍与基线相同，未重新构建或重跑基线。真实回调1进1出；A.close在同暂停中返回pending并撤销旧receipt，Closing5在释放回调门之前；observer及原调用退出后才推进detach。SDK对实际新session精确shutdown1，同Bridge新bootstrap成功，没有夹具补清理，没有关闭后查询旧view，fresh pause1/1及frontend/A/B/Runtime严格退休。独立原始运行审查接受这一窄结论。

第一次修复尝试的环境2acd中断仍为UNKNOWN/运行未验证；它与baseline FAIL均未升级。恢复时复用同一VM/overlay和原启动逻辑，新增typed prior只分开历史退出结论与当前安全启动检查。新周期经一次正常ACPI结束，原QEMU/监督器自然0、未强制、双EOF；串口真实远端EOF，HTTP停止并等待线程，自己的控制页关闭，使用权释放。独立源码审查及9组内存谓词检查均通过，没有复制控制器或新建磁盘。

证据索引：基线 `bootstrap-callback-close-baseline-2026-10-22T15-52-43-010779Z/guest-2acd5554261d421095013845412dde5e/baseline-observation.json`；修复 `bootstrap-callback-close-fix-2026-10-22T16-02-36-720710Z/guest-88b68637c58e4028b952ec6a36d874fa/attempt-outcome.json`，均位于 `soui-sdk/out/devtools-stage0/miniblink-sdk-close/`。新原始单帧104774字节/`9d184286…`恰6文件，独审 `independent-runtime-review.json` 10148/`299cad59…`，新held CP5956/`bac2885b…`由保留原物理退出谓词的Router接纳。最终截图只证明控制台结果，不冒充暂停像素。这一跨线程关闭交叠不等于回调自身重入关闭或嵌套暂停；完整阶段0及生产验收仍未完成。

## 当前暂停调度边界（2026-10-22 15:40Z）

当前kernel9b516保留isolate-wide checkpoint：新run72fbbdba…实录一个已排队reaction在同session/receipt4/doc2、pause1/1/0且本fixture请求Resume前执行；它的同步bootstrap也在该栈中完成。UI另观察65样本/2秒后一次Resume，随后count1/value7/真实bootstrap/tail、pause1/1、原A存活和严格退休均通过。原生observer未直接读到7；checkpoint0不影响现场证据。

普通SDK FIFO仍不在暂停循环泵送；旧Bridge run9ab526…的queued resolver/then延后至Resume。不能扩大为所有业务冻结或所有Bridge调用延期。继续保留原所有权规则：暂停、dispatch、原生回调或原调用未退栈前，不detach/release其借用对象。回调栈关闭/强撤销与nested独立未验证；没有据此改kernel调度。新消费者app2c20d2f6/EXEb028e534，仅此新增场景一次通过，原30/40秒不变，环境正常闭合。完整生产交付未完成。

原始索引：soui-sdk/out/devtools-stage0/miniblink-sdk-close/queued-promise-observation-2026-10-22T15-25-58-681730Z/guest-2c5e75c4f1cd46ee8dbf2fca189076c8/attempt-outcome.json；当前闭合CP5858/395a4f38…已由原validator接纳。此节更新事实与边界，以下历史结果保留原app/kernel/run身份。


## A.5 当前公共 Bridge 暂停回复一次通过；全部环境已关闭 — 2026-10-22 15:10Z

唯一实际run `9ab526e253a24524a6d4093e1f3c2e40`，PID5920，native0、owner29959ms；原生授权至结束3765ms，原30秒case/40秒owner均满足，无强制、无重置。app121762/`7a6859c33d5485590f4b0a096355f8304d29a460b4c10b66d2bbe0df3df8c5da`、EXE1057792/`d8fd38caabc2d0cc9b8249df383367f268a20046e54c5647a8d22155d4d80391`、ISO52252672/`36c1a3ccfc97008c3a4e54c564ce7bed4adf2a5a5fb970a995a692d4c623b667`；SDK库b6ab、kernel9b516保持原样。首次初版EXE27b336未打包未运行，实际只使用fresh-snapshot修订版。

真实暂停1/0后UI释放有界worker门；handler返回不当作worker完成。原owned worker FIFO样本为queued0/executing0、posted1；同一回复的ordinary outstanding1、共享engine queued1/executing1；两侧同暂停夹持和固定id/adapter trace确认posted但resolver/then未执行。worker idle后观察64次跨2000ms，再唯一Resume。原script返回7，随后同一then固定7一次和报告ACK，两个请求trace四阶段各一次，fresh snapshot1/1、原A receipt4存活及frontend/A/B/Runtime严格退休。此处worker idle表示回调已退栈，不宣称暂停期间销毁worker线程；捕获一直保留到Host真实退休。

独立a51原数据窄审接受完整单帧、恰7个原文件、1406字节trace及事件顺序。串口115305字节/`5ee7d10b272ba6c62c051e5527cb8359af89bf7ada8ae70e40a3fb347a932e52`，真实远端EOF。没有拍到2秒暂停期像素，不据此声称A5暂停GUI内容；不为截图重跑。旧A2异常原图与通过仍只保留其05c005产物身份，不转挂新消费者。nested、独立业务microtask和关闭重入仍未证明。

同一原VM/overlay经唯一正常ACPI ed0c5a/0关闭；原START32246→4b9f65/0 CLOSED，QEMU63612/监督器90156自然0、未强制、双EOF。串口bc0db2/0、HTTP64b069/0停止并等待线程、自有IAB2/tab11关闭、pool039218/0释放，Functions bridgeReplyVmClaim/Raw与pageDeferralVmClaim/Raw均null。最新可复用CP5918/`343cd8dfd36184c44e49374575ac9fb29dacb821c7aa744ffe6c11d9d38cd067`，原validate_closed_packet380268/0接纳并核当前Lab pin。B actor资格流程未执行，P0/product/release与strictFinalReturnDeadlineProved均不升级。无新VM/盘/控制器/准备helper源码。

当前原始索引：`soui-sdk/out/devtools-stage0/miniblink-sdk-close/bridge-paused-reply-final-2026-10-22T14-55-27-001180Z/guest-ae09a242e848431e9e5ac97c67d08cdd/attempt-outcome.json`。Root be3827/0复核18当前输入/副本、原guest runner SHA及runtime app/kernel；独立源审a51、构建介质审a5 f7ab5a/0和本次原数据审分别保留，不把自动化结果称像素证明。Learning start/secondpass无匹配项，791d46/0明确none，复用既有完成发布后fresh-snapshot教训。

下一步只读收敛当前内核独立业务Promise/microtask、nested与close-reentry的最高风险接缝，尚不新增运行。完整Stage0A/B/C、双后端公共DevTools、模板/安装、D01–D14及稳定候选长测仍未完成；Goal ACTIVE，Work621/fence3（下一checkpoint622）。没有commit/push/release或替换现有安装。

## 当前异常 Sources、单步与继续切片通过；环境已关闭 — 2026-10-22 14:44Z

当前 kernel `9b516490…`、消费者 `9ddcc8b7…` / EXE `05c005c9…`、ISO `7fec03e1…` 的固定 caught 异常切片已限定通过。首例 `506532…` 因输入观察过久超出原期限，native1/owner64567ms，保留失败。仅一次预先冻结的输入补测 `f90c6a…` native0、owner30733ms、授权至结束17016ms，原30/40秒不变；没有第三次运行。

真实截图06为 `index.html:17` 的 throw、异常原因及 Local `seed=7 / doubled=14 / corrected=0`；F10 后07到19行并出现 Catch scope；F8 后固定返回15，暂停计数2/2平衡，原A上下文存活，前端/A/B/Runtime严格退休。独立只读审查确认原图和完整第二串口帧，成功导出实际6文件全部长度/SHA匹配。首轮150694字节前缀未改；终态串口229972字节、`764b5ad475b760eebfb8810d497e272cccafb00b47225c498a95dd37d73af612`、真实远端EOF。

同一原VM/overlay已一次正常ACPI关机；原QEMU29860/监督器86716自然0、未强制、双EOF。自己的网页关闭、HTTP停止并等待线程、使用权释放，所有工具终态。最新 `environment-held-closed.json` 5891字节/`386844e01a13f23610ebfe44b7dd690948a8f5c401d4e5ceac1eab23547b46ee` 经原 `validate_closed_packet` 35bce3/0接纳，当前Lab pin另核；环境闭合与原生清理分开。未执行B actor的资格流程，不升级其标记。关机前旧Root POWERDOWN因缺少初始资格证明而在QMP前失败，之后官方pool只发送一次正常ACPI；错误记录均保留。

原始索引：`soui-sdk/out/devtools-stage0/miniblink-sdk-close/exception-page-deferral-2026-10-22T14-07-06-512769Z/guest-b6f3b91e9ddd4cc789307d3c4f1730b5/attempt-outcome.json`。此前旧产物各项通过/失败保持原身份，不转挂当前构建。观察到新Sources恢复，但没有单独隔离的因果A/B实验。

下一项是A.5当前公共Bridge在真暂停期间worker完成、resolver/then延期、Resume后同一回复完成的实现及限定验证；尚未实施。nested与关闭重入独立保留。完整Stage0A/B/C、双后端公共SDK、模板/安装、D01–D14和稳定候选长测仍未完成；Goal ACTIVE。已通过且身份未变的项不重复运行，没有commit/push/release或替换现有安装。

## Page延期修复已构建、待真实运行 — 2026-10-22 14:14Z

新内核 `39053312 / 9b5164905000ff5de6d3016b3dc1fd688136bd02d34fd7d6d2fc2f886cebd5cc` 已由真实新PCH、六个受影响TU及3016个原body链接完成。六TU实际依赖989/520/974/700/1324/1031均确认使用新Client头；20721项PRE与前后输入稳定，原私有include差异保留。构建29.696秒自然0，3023个repro输入闭集匹配，9662导出名称逐项保持一致。LLD额外自动搜索本机14.51/SDK26100四目录仅为已核对metadata，未接纳任何新库。原参数文件换行、PCH子进程计数、历史PRE漏头造成的三次收集失败及首次audit错误均保留，不转为通过；已有真实PCH/CONFIG经独立限定核验复用，未重复生成。

消费者app106394/`9ddcc8b79c720dc9409593decb9b3178c3f1331e3411d7311be6292c76e08799`仅在已审被动诊断源码上更换1个内核SHA和4个大小字面量，整文件逆变相等；断言、30秒case/40秒owner未变。一次受影响构建7.718秒退出0、双EOF、18源码及副本稳定，复用SDK库b6ab；EXE1010688/`05c005c98c5b61af1faa1f46620aad1ec2c1e9036dbcef9f6a0ef79fcae61e86`。

介质ISO52203520/`7fec03e1ac3517cb4bb4c387bd67f71dee776c2259053cde74e96166277afa84`，659成员仅EXE/kernel/runner三项变化、656原样；原writer/launcher复用、runner单SHA逆变，未新建准备helper源码。独立只读构建/介质审ae3f3d通过；独立ISO成员reader仍未运行。产物：`soui-sdk/out/devtools-stage0/miniblink-sdk-close/exception-page-deferral-2026-10-22T14-07-06-512769Z`；内核构建：`soui-sdk/out/devtools-stage0/miniblink-sdk-close/page-deferral-kernel-2026-10-22T13-46-15-374572Z/complete-source-pre`。

**当前未启动VM/产品，没有pool租约或活跃工具进程；运行修复尚未证明。** 下一步仅复用`soui-devtools-windows`原overlay和未修改的closed-empty-streams operator，以最新旧环境CLOSED证据启动一次新产物异常/Sources/F10/F8验证；旧异常两例失败与旧57620各窄通过均不挂到新内核。全Stage0A/B/C、公开API、双后端、模板安装、D01–D14和稳定候选长测仍未完成。

以下保留历史记录，描述只适用于各自冻结产物。

日期：2026-10-22。状态：已实现，局部回归、当前 x64 Release 真暂停关闭四种目标、同一 A 重开及关闭后目标重建隔离的限定场景通过；完整 Stage0 A、公共 DevTools 和生产验收尚未完成。

## 问题与决定

Sources 懒加载的限定源码核查另发现：全局 modal deferrer 将 Page 标志设为延期，
既有 frontend hook 只恢复现有 frame loader；新 ResourceLoader 仍从 Page 读到延期，
异步请求在创建 URLLoader 前被存放。该源码缺陷与空白现象一致，但旧实际载荷的
整链源码身份及本次运行分支没有单独重证，不能写成已观测根因。

修复边界为当前原生绑定的受控 frontend：先保留 session，核对当前完整身份、该
frontend 的有效资源 policy/state；当前性使用既有view/document代际检查，不宣称
逐字段geometry身份比较。在同一LIFO元素保存模式、原Page指针、原延期值
及session，再临时解除该Page延期。退出先移出并弹出元素，再按捕获值恢复，不因
撤销后的session状态跳过；pause/dispatch及endpoint pins必须持有至恢复完成。
legacy路径维持现有frame级行为。不得修改业务Page、全局timer queue、资源允许表、
Worker、业务FIFO或raw-view保护；完整Sources功能仍需新载荷真实验证。

该窄修复已落在 `DevToolsClient.cpp/.h`，**尚未构建或实际验证**。受控guard失败
保留本层no-op配对，绝不转入legacy恢复路径。Client内部栈元素类型已改变，下一构建
必须从实际依赖图重编译受影响TU，不能只把新源码与旧DLL关联。

| 对象或状态 | 行为与验证边界 |
|---|---|
| 当前受控frontend Page | 原生绑定与有效policy/state确认后，保存原值并临时解除Page延期；新旧加载器一致 |
| 进入后session撤销 | 退出按已捕获Page/原值恢复；不因撤销跳过，pins持有至恢复完成 |
| 受控guard拒绝 | 成对no-op，资源及页面不恢复；不落入legacy |
| legacy Client | 原frame级行为保留，不能由本修复扩展其授权 |
| 业务Page、全局timer/FIFO | 不改动；本改动也不证明Sources剩余初始化或计时器已恢复 |

为定位 Sources 空白，仅在现有异常消费者增加被动元数据：前端 Console 回调按
七类级别计数，仅对错误比对 Runtime 的“空脚本”“资源加载失败”两个固定前缀，
不记录正文，不读取来源、行或栈；像素复制成功后在原缓存锁内递增帧号，
WM_PAINT在同一锁内关联该帧与GDI返回扫描行数，锁外限量记录。已有资源快照仅作为
非事务性的lookup/交付汇总，不能解释为请求数或Sources加载完成。首次暂停、单步、
固定按键、鼠标释放和收尾记录这些值；不新增JS、资源钩子、队列任务或通过谓词，
不改变绘制算法、权限、原30/40秒。诊断不能替代真实原因、位置、局部变量的画面验收。

本轮异常切片实际失败：首次owner超期；一次固定输入补测16962ms内自然退出1，
首次暂停、F10新暂停及F8接受成立，随后固定返回校验失败，Sources主体空白。
两例与原30/40秒保留，没有第三次。仅消费者存在明确临时 execState 跨暂停失效风险，
现改为 RunJS 返回后重新取得状态再立即转换，保留严格15断言并拆分错误。
独立源码审查接受，修复构建7.5秒退出0，未打包运行。原复合错误未区分实际失败
分支，不能把源码解释当已观测根因；Sources原因/位置/局部值仍无实际证明。

下一项 A.2 异常切片只扩展现有私有消费者及 A 固定页面，不改变 SDK、内核或公共
契约。单次原生菜单授权后，复用既有固定前端策略脚本的 enable/全部异常成功回调
握手，再调用无 debugger 语句的固定 caught-throw 函数。只在原 engine 的普通外层
阶段轮询策略，目标暂停后不投递取值任务或泵业务 FIFO。

| 受影响状态 | 决定与证明边界 |
|---|---|
| 前端策略 | 保留当前 frontend/session/context；phase3 才触发固定调用，原期限不变 |
| 首次异常暂停 | 原调用未返回、同 session、真实 pause 计数；异常原因、throw 行和局部变量另需实际 Sources 画面，ABI 计数不能代证 |
| 原生 F10 / F8 | 仅当前 inspector、无 Alt 的首次按下；命令接受后持有配对释放及 repeat，单步须新 entries/exits 与新暂停，继续须真实脚本返回 |
| 正常收尾及失败恢复 | 保留原 Host/Bridge/HWND 与策略 operation，等原调用返回、暂停/dispatch 平衡及固定返回值后，才沿用 detach/release/drain；失败仍走原清理和原期限 |

该分支已完成唯一受影响的 x64 Release 增量构建及独立源码、介质身份核对，实际
异常调试已实际失败，修复产物尚未验证；前端检查不能在 native 结果中预置为 true。快照字段由内核分别
更新与读取，已按源码审查修正为在原期限内等待精确暂停配对，禁止据瞬时过渡组合
提前放行或误报完成。既有关闭、重开、
reload、重建及 A46 结果留在各自冻结候选，不因新增该分支重跑或改挂。当前异常后的
单步取得新暂停的计数已有有限证据，完整异常检查及恢复结果仍未通过；本例也不证明 A.5 的 Bridge/microtask/nested/reentry。

A.7 重建切片使用现有消费者：第一代 inspector 严格退休且原 A 返回状态
已核验后，第二次原生菜单授权在原 UI owner 轮询 A 的 close，再完成 destroy；
旧 Host/adapter 保留为已退休 owner 到最终清理，B 和原 Runtime 不关闭。新 Host
和 Adapter 使用独立固定窗口槽位；旧 HWND 隐藏保留，旧 Bridge payload 只交回
原 adapter 消费，旧窗口的菜单、关闭、焦点和尺寸消息不再路由给新 Host。
新 Host 部分启动失败也必须保留并走 destroy 排空，不能沿用旧 A 的 closed 标志。

等待实际新文档 ready/ACK 后才借用 fresh context，并要求同 engine/module/broker/
realm、B 仍挂载及新 endpoint。新适配器的 receipt、view 地址、generation 数字均
可能重复；完整身份必须包含 endpoint。旧完整 context 的拒绝在原 engine 上核验，
不能用 UI 线程本来就返回 false 的结果冒充隔离。旧 target open 在新 session 前
精确 Stale，之后沿用新真暂停下旧会话拒绝及严格收尾。原 30/40 秒不重置。

本切片已有单独的实际证据：最终 app `979f3e15...`、EXE `3ccebe33...`，沿用
SDK 库 `b6ab96c8...` 和内核 `57620c1a...`。首轮 run `92f950a4...` 原生退出 0，
但 owner 42939 ms 超过原 40 秒，整体失败保留。先记录输入等待与一次流程修正后，
相同源码和断言仅补测一次；run `d7ea5a0a...` 自然退出 0，owner 33035 ms、
grant 至完成 24125 ms，原 30/40 秒内。旧 A 完整关闭后，新 A 实际复用 receipt 4
和 viewGeneration 1，而 endpoint 不同；原 engine 拒绝旧 context/target 后才创建
新 session，旧会话四操作在新暂停中为 Stale，连续三次心跳不改变新暂停或关闭计数。
明确的非原生结束策略之后严格退休全部对象。独立只读复核接受该有限结果；原件见
`out/devtools-stage0/miniblink-sdk-close/target-rebuild-final-2026-10-22T11-56-23-870857Z/guest-217a7c1144de418b9c431d426eca34ed/attempt-outcome.json`。
旧 HWND 路由有源码审查，但没有主动注入旧原生消息竞态；不扩大该运行结论。
这也不证明暂停栈内 reload/重建、活跃 B JS 或完整 Stage0 A。

同一消费者的重开验证：旧 inspector 严格退休后保留原 A/B，以第二次
明确的原生菜单授权创建新前端和新 session，原 30/40 秒不重置。两代分别保留
固定 callback cookie 和 HWND 至最终 Runtime 退休，旧回调核对自己的 identity，
不修改新代的 ready/OLE/paint 状态。新 session 建立前检查已删除的旧 endpoint
及旧前端 identity 被拒绝；新 session 真暂停时再调用旧 session 的 Resume、Close、
input、snapshot，要求精确 Stale 且新暂停和 closeRequests 不变。该重开切片现已
取得独立实际证据；reload 在另一冻结候选单独验证，见 ADR 0120，重建则见上文独立候选。

首次真实重开已命中第二次暂停并拒绝旧操作，但额外等待第二次手动关闭导致原
30/40 秒超期，整体失败保留。A.7 的断言是重开与隔离；后续诊断在第二次暂停下
完成精确 Stale、暂停保持和三次 UI 观察后，按本次诊断的明确结束策略 requestClose，
仍要求原脚本返回、暂停平衡及严格清理，不把这次自动收尾记录为第二次原生 X。
首次原生关闭和第二次原生重开授权保留，原期限不变。

修正后的实际 run `636fcf1297794af9bbf17b974ba8d5b4` 在同一后台 VM 中自然
退出 0，完整 owner 38578 ms，原生首次授权至完成 21672 ms，原 30/40 秒内。
新会话旧四操作均为 Stale，三次心跳保持同一暂停；诊断自动 requestClose 后
原脚本返回、pause/dispatch 平衡、detach/release、前端严格排空，再核对原 A
receipt 4 的既有固定函数已返回，最后 A/B 和 Runtime 退休。第一次真实原生 X
单独记录为 true；第二轮 `closeInputObservedAtSamePause=false`，不伪称人工 X。
源 app `8212b88c...`、EXE `931eacad...` 的六个原始文件见实验 README 的
`same-a-reopen-finish-2026-10-22T11-18-25-068940Z/guest-4c1a.../`。此前超期
失败原件仍在前一个候选中，未被升级为通过。未改 SDK、内核或 VM 控制器。

私有 Stage0 消费者继续复用同一程序和原 30/40 秒期限，通过原生菜单选择
暂停关闭 inspector、A 或整个应用。实际关闭输入到达时重新核验同一 session/暂停栈，
不提前发送 Resume。A 的公共 close 使原 receipt 失效后，清理由保留的原 engine、
Host、Bridge 和 HWND 执行，先结束 inspector、等原脚本返回及 pause/dispatch 平衡、
detach/release 并排空前端，再完成 A；不能等待 A 关闭成功后才释放 inspector 借用。
关闭 inspector 的场景在公共 Host 收尾前，用原 A context 核验既有固定函数已经返回，
并保留同 engine 的静态 B。三项新增场景已取得独立实际证据，不继承已有 B 单项状态。

公共 Host 的 close/destroy 会直接调用 dynamic_miniblink_adapter::unmount。
原 unmount 在 UI 上等待 cleanup FIFO 屏障、Bridge worker join、WebView
清理和 endpoint drain。共享引擎停在 JavaScript 暂停栈时，cleanup lane
仍不能抢占当前任务，UI 因而可能一直等待。原风险判断来自活跃源码，
当前公共 SDK 的限定真实暂停结果见下文；私有 inspector 原型不能代替公共 Host 的证据。

仅把关闭链改为适配器持有的阶段和完成票：屏障、Bridge 退休、引擎清理、
endpoint request/drain、UI 资源退休。每个尚未完成的引擎步骤只入队一次，
通过原 cleanup FIFO 执行；UI 只读已完成且同步发布的结果，未完成时返回
已有 E_CURSOR_CLOSE_PENDING。Bridge 使用原 try_stop 持原线程零等待观察，
先使排队的旧文档工作失效，再让其闭包退休；不停止共享引擎。

关闭开始即撤回 Bridge、文档和普通操作准入。持有状态取代 requestClose
等任务的栈引用，直到真实完成才能释放适配器、broker、借用 Bridge、HWND
和 Runtime Host 计数。重复调用继续原步骤，不能把已接纳的任务当作取消。
原生失败保持失败，定时重试只是进展机会，不能充当 drain 或关闭证明。

线程约束来自实际子 HWND 和已开始的继续状态，不从诊断的构造线程推导。
未挂载、无存续资源的适配器允许跨构造线程进行幂等 unmount/析构。

## 后果与边界

| 受影响对象/状态 | 保留与行为 | 验证/恢复 |
|---|---|---|
| Host、adapter、借用 Bridge、两个 HWND | pending 时调用者继续持有；析构未完成仍 fail-stop | 原 close/destroy 重试，成功后才销毁 |
| 共享 Runtime / engine FIFO | 不插队、不泵业务 FIFO、不停止别的 Host；真实清理前不减 hosts | 原 native 退出与回调 drain |
| Bridge worker | 旧 generation 失效，保留原线程直到真实 try_stop 完成 | pending 不 reset，不同步 join |
| endpoint / native scopes | 请求结果与 drain 结果跨调用持有；暂停/重入保留所有权 | 原 inspectDrain/destroyEndpoint/finish_close |
| 普通操作 | 关闭开始后拒绝 reload/show/resize/focus，pump 只推进清理 | 不允许新任务排在清理之后借用 adapter |

## 替代、兼容与迁移

不全局更改 invoke_lifecycle：mount、registration 仍有同步栈捕获，
提前返回会产生悬空引用。reload 后续采用独立持久结果，见 ADR 0120。
不给 future.get 加超时，不恢复 A 再宣称关闭 B，
不从其它线程强制销毁 view，不扩大 raw-view 权限或引入公共 DevTools API。

沿用既有 pending/保活契约；普通 close 也可能需要后续 UI 轮次。调用方
须按原关闭总期限重试。超时仍失败并保留所有权，不增加现有监督器期限。
私有类布局改变需要重编源码消费者；不替换已发布 0.5.0 安装内容。

## 验证范围

局部无窗口回归编译实际 Host 源码，在私有测试编译边界绑定真实 engine
worker，不加载 Miniblink DLL。验证阻塞 FIFO 下 unmount 返回 pending、
重复轮询不重复入队、普通操作拒绝、原 engine/Bridge worker 保留及完成后
继续收尾。先记录旧实现失败，再运行受影响目标；不复制关闭算法。
该回归不证明 V8 暂停栈、COM/OLE、真实 inspector 或完整 SDK 生命周期。
该局部回归之外的真实结果单独列于下文；Stage0 A 的活跃 B JS、完整调试能力、
暂停中 reload/重建、完整独立审查和最终候选仍须完成；关闭后重建的有限结果见上文。

真实接入使用独立实验构建的 `SOUI_HOST_STAGE0_DEVTOOLS_TEST_ONLY`。
该构建只在 SDK 内部借用已挂载 Host 的原 module、engine、realm、endpoint
和文档 identity，把既有私有 inspector 接到公共 Host；不另建目标或 Runtime，
不把测试入口安装或开放到网页。借用者必须保持原 Host/Bridge/HWND 存活，
view 只在原 engine 上使用。关闭开始、文档变更或未就绪时不得取得新借用。
先在 A 仍暂停时调用 B 的公共 close，再允许恢复 A；原私有原型结果不能代证。
该实验的初始本机 x64 Release 构建当时尚未运行实际窗口，不改变上述
无 DLL 局部回归的验证边界。原始编译记录在
`out/devtools-stage0/miniblink-sdk-close/build-2026-10-22T05-41-12-078Z/`。
最终窗口适配与两秒暂停观察只增量编译程序，结果见同目录下
`incremental-2026-10-22T05-45-53-795Z/result.json`；SDK 库身份未变。

2026-10-22 新增当前冻结 SDK 的实际单项：run
`4328d4d0e662492db5d9a93842a4431f`，x64 Release，SDK 库 `b6ab96c8...`、
内核 `57620c1a...`。A 真暂停 depth=1、entries=1、exits=0 时，静态 B 的公共
close 返回 `E_CURSOR_CLOSE_PENDING`；尚未 Resume 或关闭 inspector。原 UI
继续记录 64 次同一次暂停心跳，满 2000 ms 后才 Resume。随后原脚本返回、B
关闭、inspector detach/release、前端 drain、A 关闭、Runtime 退休，原进程自然
退出 0；授权到完成 3985 ms，owner 27237 ms，分别小于原 30/40 秒。独立只读
复核核对了事件顺序、六个原始文件及源码/载荷身份。完整索引见
`out/devtools-stage0/miniblink-sdk-close/async-attachment-retirement-2026-10-22T10-14-25-558979Z/guest-df878fe3f33645588ffd4fe7478be3cc/attempt-outcome.json`。
这次历史 B 单项的范围仅为当前内核与静态 B 的公共关闭，不证明活跃 B JS、
完整 Stage0 A、公共 DevTools API、其他架构/配置或生产准入。失败样本保留，
未因本单项将任何最终产品通过标志改为 true。

同日新增的实际 A/应用/inspector 场景分别为 run `fbd256e85b944f12abe17beef3b3ed49`、
`1b11ac52f0dd4f848f2903a5d3740a7c`、`ac83185a035c4e2383348db04db89304`，
完整 owner 为 20295/18407/21545 ms，grant 到完成为 10969/9812/12937 ms；
全部在原 30/40 秒内自然退出 0。关闭输入发生在同一次真实 1/1/0 暂停中，未提前
Resume。A 的公共 close 先返回 pending 并撤回 receipt；fixture 随后明确关闭
inspector，原脚本返回、暂停平衡、detach/release 和前端 drain 后，A 才关闭成功，
B 仍挂载原 engine。Inspector 场景另核验原 A receipt 下固定函数的真实返回状态。
这是私有消费者的明确关闭策略，不表示公共 Host 已自动管理私有 inspector。
首次 inspector owner 47157 ms 超期仍为失败；具体输入耗时及一次精简流程补测见
fixture README。四次原始记录和限定索引位于
`out/devtools-stage0/miniblink-sdk-close/native-close-context-2026-10-22T10-40-38-622157Z/guest-a313e8481ee944259e8be5a0f0090cbf/`。
此组仅覆盖当前内核、静态 B、`debugger` 暂停和上述关闭顺序；活跃 B JS、断点/异常、
单步/面板、重入、完整 Stage0 A/B/C、其他架构/配置、安装与生产验收仍未因此关闭。

当前 SDK 的 `SOUI_TEST_KIT` 条件引用所指向的两个 test-kit 目录均不存在，
未确认该配置可构建。相邻旧 devkit 的 pipe_agent 析构确有同步 join，
但不能据此认定当前 SDK 实际绑定该实现。该配置的构建与关闭行为均未验证，
不纳入此次非阻塞结论；本轮未修改旧 devkit 或测试工具依赖。

2026-10-22 本机 x64 Release（MSVC 19.44、Windows SDK 10.0.26100.0）结果：
旧 unmount 先在真实占用的 engine FIFO 下失败（c38d72，CTest 退出8）；
独立审查发现的构造线程门禁问题另有真实失败（3af0d1，CTest 退出8）。
修复后的实际 SDK 及三个局部目标构建退出0，四个注册测试通过，合计约1.54秒
（8f3dc9）。包括 unmount 延续、未挂载跨构造线程、Host 转发和引擎诊断生命周期。
原始日志、实际编译/执行身份和两轮失败均在
`out/devtools-stage0/miniblink-unmount-unit/`；这次显式未注册 vendor 回归。
独立只读主体审查和线程门禁增量审查未留下普通 SDK 配置的新确定阻断。
这些结果不能外推到 x86、其它 SDK、真实 Miniblink、其它配置或安装消费者。
