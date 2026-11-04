# ADR-0159：MiniBlink 原生 Bridge 的有界分段观测

状态：proposed；决定用于本地诊断实现，尚未编译或验证，不代表性能采用或生产资格。

## 背景

现有15项trace直方图不能分开首次native JSON解析、resolver创建和回复JS执行。旧私有重负载仅提供这些调用所在的混合跨度；分位不能相减，也不能据copy计数0或没有输入样本作调度、damage-history或GPU采用结论。ADR0158已移除重复接纳解析，本次需要直接观察剩余路径，不改写既有metric含义。

## 决定

增加独立冷开关`SOUI_DIAGNOSTIC_BRIDGE_PHASES`，每次实际mount读取一次。unset、空值、`0`关闭，`1`启用；其他值报告configuration_invalid并关闭采样，不记录原始值。默认关闭时不分配新增统计块、不取新增QPC、不更新桶、不序列化扩展；这不代表现有scalar停止计时。

统计块仅归属私有event_dispatch_lifetime，不增加公共adapter字段、vtable、WKE接口或15项metric。启用时分配固定3×64桶与计数；分配、64位原子非lock-free、频率或incarnation失败仅使诊断unavailable，不能使产品mount失败。Engine在native bindings发布前绑定owner与QPC频率。构造用的未mount lifetime不读取开关或分配观测块。

三个固定阶段紧邻原调用：`native_first_parse`只包`bounded_bridge_packet(request)`；`resolver_create_js`只包创建pending resolver的`jsEval`；`reply_run_js`只包正常异步回复的`wkeRunJS`。首次raw复制、字段提取、script序列化、原前后receipt/generation检查、同步bootstrap、拒绝后的即时回复及其余JS调用不挪动、不重复、不纳入这三个范围。

scope只持有自己的metrics块，不持Host或event_dispatch_lifetime。跨JS导航、关闭或重入后只完成旧块，不再读取adapter；旧数据不进入新mount。统计为inclusive wall-time，包含同步嵌套JS/native及调度停顿。returned仅指C++返回，unwound仅指异常展开，不表示JS或授权成功；不能把三段相加当CPU成本。

单Engine写者只在begin/end作固定短原子发布，seq_cst为首版内存序。所有被快照读取的字段均为原子；禁止用普通字段配seqlock造成data race。开始QPC在begin提交之后、结束QPC在end提交之前；不能跨被测调用、QPC、分配保持odd序列、锁或guard，不能自旋等待。错误owner只置sticky独立故障位，不建立第二个计数写者。计数/sequence溢出、QPC0/倒退使sticky fault和unusable，不回绕或补零耗时。UI最多3次一致读取，失败返回busy/counters=null；UI坏时钟也只发布独立atomic clock故障位，不改sequence或phase计数。复制前后fault取并集，仅保证本次已观察窗口，不保证返回后没有新fault。快照保留in-flight、nested、异常与captureBegin/End QPC，不假称quiescent或整个Host原子。

沿用application_diagnostics字符串，仅显式启用或非法配置时追加`,native_phases=`固定JSON，放在旧`,script=`之前；保持旧script末尾解析兼容。独立helper不调用Engine invoke、wkeGlobalExec、jsEval、文件IO或产品回调；不打开SCRIPT。整个旧getter仍有原锁及SCRIPT可选行为，不能宣传为整体无阻塞。关闭/失败前缀不改，只附加可安全读取的旧块状态。

扩展schema为`soui.miniblink-native-phases/v1`：固定3阶段/64桶，全部uint64用十进制字符串，无请求、method、requestId、payload、token、origin；序列化上限8KiB，失败返回unavailable。默认关闭时保持旧诊断输出。数据在原UI缓存机会采集；消费方必须记录实际captureQPC、observerIncarnation/sequence与缓存age，不能把每秒缓存冒充新鲜Begin/End，不增加worker等待UI的同步调用。

固定字段使用`requested/enabled/status/usable/coherent`、`observerIncarnation/qpcFrequency/captureBeginQpc/captureEndQpc/publicationSequence/snapshotAttempts`、`faultMask/counterSaturated/clockInvalid/ownerViolation/sequenceExhausted/closing`、`inclusiveWallTime=true/wholeHostAtomic=false/quiescent=false`。`phases`按上述三阶段顺序，每项`phase/started/completed/returned/unwound/inFlight/nestedStarts/sumElapsedQpc/buckets`。非可用快照的phases为null，缺失时钟为null，不用零桶补缺。status ready仍不表示没有in-flight；完全相同缓存capture单列no-new-capture，不冒充新取数点。

## 兼容、备选与回退

不扩公开trace/WKE ABI，不复用旧桶、不用高频detailed ring推导这三个完整分布；不在热路径读取全recorder snapshot、环境、字符串或动态回调。保留原Bridge权限、FIFO、capacity/lease、reply credit、deadline与关闭顺序。新增原子与QPC仅为显式诊断成本，不能据开启观测后的FPS宣称产品收益。

可删除冷配置、三个scope及字符串扩展，恢复此前行为；不回滚ADR0158的去重解析或已有安全/生命周期修复。历史已验证SDK/ZIP保持不可变，新代码使用新本地BuildID。此决定不关闭I03调度/切片、I02默认或F02/F03采用任务。

## 验证

直接测试生产helper：关闭不读时钟/分配、绑定、三段独立、异常、嵌套inclusive、旧块退休、owner/clock/计数/sequence故障、64桶边界、一致与busy三次封顶、reader clock故障不丢失。验证生产序列化大小及旧script后缀，独立审查三个调用的前后guard与寿命。实现稳定后集中编译受影响x86/x64目标；新匹配载荷仅一次正常功能/诊断验证，未执行项如实保留。缓存快照仍不足以证明逐输入前缀、物理呈现或CPU份额。

本地BuildID `candidate-native-phases-sdk-r1`已在LLVM22/MSVC142/19041下完成x86/x64 Release受影响目标编译，每架构9项精确CTest通过，新增helper各31项CPU合同通过。显式启用的recorder实际为1752B，snapshot为1768B；关闭不新增这些分配。独立诊断消费者的35项CPU合同在COM和窗口初始化前通过。构建证据为`I03/native-phase-sdk-build-r1/build-result.json`及`W02/native-phase-consumer-r1/build-result.json`，不外推为v143或物理交互资格。
