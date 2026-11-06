# ADR-0166：测试页面同步读取 SDK 输入作用域

状态：独立测试消费者已完成 x64/v143 局部编译、真实冷初始化及正输入诊断；R37/R38旧精准点失败保持，R39新cell夹具的64项严格诊断已通过。最新实际结果见[当前验收报告](../performance/sdk-preview4-final-acceptance-20261106.md)，正式性能、物理延迟与完整Goal未通过。不改变公共 SDK、provider ABI 或默认运行库。

## 背景

M01 既有 full64 数据只能给18个精确画面 ACK、46个未知。R34 的持久64位原型能避免反馈覆盖，却不能证明 DOM 回调属于哪个原生包：旧完整原件包含64个带标记输入及真实到达 DOM 的重复坐标回调。以坐标或邻近时间猜测、忽略 tag0、把跳跃 ACK 补成64帧，均不能补齐因果关系。R34 已在构建/原生运行前停止，旧失败与草稿保持。

当前 SDK 已有可复用的公开只读函数 `input_frame_trace::current_context`。真实 `dispatch_input_on_engine` 在调用 `wkeFireMouseEvent` 外安装 `context_scope`，其中含单一输入 ID、kind 和 view/document/geometry。MiniBlink 原生 JS 回调在当前 Engine 线程同步执行，理论上可读取这一动态作用域；尚需实测，不能将源码可行性视为执行通过。

## 决定

1. 仅在独立测试消费者的已认证页面 context 安装专用原生 JS 函数。页面只进行一次真实 bootstrap：在其同步 Engine 回调中捕获实际 broker view 与 receipt，UI 随后取得 Engine，投递一次重新验证的安装任务；每次借用 fresh global exec 并验证 ownership，不跨帧保留 opaque es。安装后通知页面复用该唯一会话凭据。使用现有 `jsFunction/jsSetGlobal` 与有界 `jsData`，不使用进程全局绑定表，不覆盖 SDK 名称，不制造凭据或重置会话。
2. 使用同一个 EXE 中的冻结 SDK 静态库；不编译第二份 trace 实现，不读取内核 DLL 中另一份 TLS。回调验证 Engine owner、当前 execution-context receipt 和实际 `es` 的 ownership。
3. 只读当前 TLS。仅 `engine_begin`、pointer_move、非零单 input ID、预期代际可作为同步输入旁证；无上下文、任务作用域、旧代际或异常均明确 unknown。函数不注册 trace、不改上下文、不泵消息、不执行脚本、不调用 Host 或做 IO。
4. 返回可精确表示的有界 input ID；固定容量 POD 账本独立记录回调序号、真实 QPC/线程和完整身份。TLS 中 related_id、QPC、thread/producer 字段不能当作注入标签或已发布事件时间。按同 tuple＋input ID 精确关联完整 trace 的 T1，取得 injection tag。
5. 全部同包多 DOM 回调、tag0、重复坐标和未知保留。页面仅为实际反馈设置持久槽，并记录首次设置槽时的 input ID；读取器必须证明该 ID 的 T1 tag 与槽号一致。不得再强求所有原生回调恰好64个，也不得靠过滤未知回调制造对应关系。
6. callback cookie/state 保持到 Host、Runtime 与 Engine 确认静止之后。当前页面 raw context 只在认证调用栈内借用，不跨线程或异步保留；关闭先撤销接纳，再排空，不能凭窗口消失或截止到期释放。

## 验证与边界

首轮实际运行尚未触达输入：夹具先用 ready-bootstrap 建立会话，再用 capture-bootstrap，违反与冻结 SDK4 源码匹配的“每文档仅一个活动会话”契约。宿主自然退出2、入口1，5.932秒、Job0、正常 Runtime 关闭，双源完整/loss0，getter调用0。外层读取器的 readiness/GO 文案是宿主早退的次因，不是30秒届满。旧源码、输入、产物和失败均保留；单次 bootstrap 修正在独立目录进行，只重编消费者并复用原输入工具，不放宽期限。

修正后的消费者实际构建58.447秒、自然退出0，5项新增握手状态测试通过。两次实际启动均完成唯一 bootstrap、真实 cookie/receipt4、Engine owner、getter 安装、cold0及页面初始状态；冷调用分别精确匹配实际 SDK task8/task9，未伪造 TLS 的 thread、QPC 或 related_id。两源详细 trace 完整、loss0。两次均在 Root 发布 GO 前遇到窗口激活失败，未启动64次输入器；原30秒 GO 期限届满后正常 Runtime 关闭，Host2/入口1、Job0、无强制结束。操作系统层根因未知，不能将失败归因于 SDK，也不能把冷调用的 point12 等同于正输入之后的独立延后负对照。

- 先验证实际 owner/context 与同 EXE SDK TLS 来源，再运行一次125 Hz、64输入的短诊断；不降低原频率或放宽500 ms最终 ACK及既有关闭/监督期限。
- 冷调用必须返回未知；延后调用不能继承已结束的输入作用域。每个 tagged 输入须关联自己的同步 DOM 旁证；同 ID 多回调全部保留，不能冒充多次输入。
- 保留24字节 marker CRC、初始零位、位单调、完整最终位图、固定 ROI、真实前台/几何、run/PID/birth/generation 和完整 trace 资格。
- 捕获端报告首次有效正捕获的 begin/end 包围区间及结束上界；多个槽同画面可见不等于64帧。异步 DOM 来源、精确首次可见、物理光子时间和整体性能资格仍独立判定。
- 这是带同步原生探针的归因诊断，额外调用与详细 trace 有测量成本；未经测量税与同载荷配对验证，不作为正式性能基线或收益证明。

## 备选、兼容与回退

坐标邻近匹配和忽略重复已有反证，拒绝采用。新 SDK/provider ABI 可另行设计，但现有只读作用域足以尝试最小消费者实现，当前不扩 ABI。全局预注册需要更广生命周期和多 view 约束，当前选定页面 context 安装。

仅测试消费者/协议产生新身份，冻结 preview.4 SDK、内核、runtime/ZIP与旧收据不改。若实际调用不在输入作用域、ownership不成立或关闭不安全，保留失败并停止此路线；回退移除测试函数和新协议，不修改产品输入派发或终态契约。

## R37：缺失 DOM 信任属性的显式诊断合同

用户恢复桌面后的真实 R5 已通过前台检查，但首个普通回调的 `isTrusted` 非布尔值，原页面按 `!== true` 置 invalid 并绘制nonce0，故原生观察器拒绝初始标记。真实账本保留raw -1，native getter在同一 input2/tuple2/1/2/2 的正常T3/T4之间返回2，T1标签为0；后续真实task14的独立point12控制返回0。页面已提前冻结，native账本也有268/256溢出，且输出从DISPLAY5变为DISPLAY1，R5继续失败，不回溯接纳。

实际旧源的 `Event.h` 与生成 `V8Event.cpp` 没有isTrusted字段或绑定，与运行时缺失一致。下一份独立测试页使用显式v4及 `native-dispatch-scope-required` 模式：只兼容值为undefined且属性不存在的情况，保留raw -1、实际typeof及属性存在位，不把unknown改成true；false、null、字符串等异常仍拒绝。ID0或失败getter不能首次置反馈槽；全部tag0、outside和重复回调仍原样记录。

这项诊断只证明固定页面内的原生dispatch作用域关联。所有64项仍需真实native标签、完整tuple＋ID的T1、T3/正常T4对getter的包围、DOM ordinal、实际firstsetter、CRC与单调mask、桌面捕获及独立task12和账本完整性；缺失DOM属性不能替代任何一项。固定脚本闭集不得dispatchEvent或改写Event信任属性；真实输入回调内部的同坐标合成事件可能继承TLS，因此不宣称任意业务页认证或DOM信任真值。无作用域合成、显式false及错误来源须有指定负对照。

新页、读取器、请求及实际显示配置分配独立身份，只复用已有Host/输入器；先重新校准本机输出，再冻结请求。原v3与R5失败不由新读取器改绿，原容量、超时和物理性能边界均保持。

## R39：独立空间码本夹具的采用边界

R37/R38的相邻像素注入实际仅在原生child入口观察到56个标签；R38新增记录证明64份INPUT参数符合原中心采样整数式，但首个请求全局188/211的原生/光标记录为187/210。此证据不确定OS精确逆映射、DPI或缺标签机制；原精准点与64覆盖失败继续保留，不更换公式或容忍误差将它们改绿。

独立批判审查有条件允许另建v5 M01夹具：以64个固定、不重叠的16像素cell编码序号，中心为x=40+16*i、y=128，cell边界为x=[32+16*i,48+16*i)、y=[120,136)。仍为64次/125Hz、原64反馈位/CRC/500ms最终ACK；不改变SDK、Host或业务。它是不同轨迹，不能与旧相邻像素样本称为同负载或计算收益，后续A/B必须双方使用同一v5重新冻结基线与噪声。

cell只编码测试序号，不承担因果匹配。新夹具仍要求全部64个标签、完整tuple＋独立inputID、正常T3/T4包围getter、真实DOM ordinal/firstsetter及全部旧身份/完整性/关闭/capture门，并新增firstsetter的DOM实际XY必须严格等于相应tag的原生实际XY。原生实际点必须位于该tag预先固定cell；缺tag、错cell或错来源仍失败。只读取到相同cell、最近时间或相邻位置均不能代替这些证据。

请求中心像素与原生实际点的差值单独报告，不宣称精准点注入已修复；未修改旧Host的相邻网格诊断必须保留并按原模型重算一致，不静默忽略其mismatch。旧模式、旧失败和源码保持。新结论仅为固定源“实际原生cell事件到反馈”的归因，不是任意页面认证、独立物理帧、旧缺标签原因或整个Goal通过。重负载、测量税、键盘、DPI/多屏和有效帧等门槛另行处理。

实际R39首次入口在预定burst前已有tag0将cell35置位，原zero门正确拒绝，未发送预定输入；原失败保持。用户明确暂停操作后，同字节工具/页面/reader仅以新run/display绑定复验，64项严格关联、完整trace及正常关闭通过。T1→T3 P95为0.6111ms，T1→首次有效正捕获结束上界P95为35.0122ms；36捕获不是64帧，0/64达到精确请求中心，物理T8与收益未知。新地点源码快照、CPU合同和局部构建各自保留范围，不将这次探针诊断默认采用为正式性能基线。
