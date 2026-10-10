# ADR0040：受控 cursor descriptor broker

状态：架构决定；已记录两架构受影响内核文件及完整 SDK 静态库的编译，x64 软件 DLL 的构建和普通 SDK 窗口启动关闭已有有限实测。完整生命周期、系统与性能验收尚未完成。它不追认旧 cursor 候选的失败记录。

## 背景

SDK HWND 属于 UI 线程，wke view 属于引擎线程。旧接口只提供 cursor 类型，不能表达同类型自定义位图更新；跨线程调用普通 wke API 不符合线程契约。历史短诊断观察到系统 `CURSOR_SUPPRESSED`、全局 handle 为 NULL，但这不说明真实硬件输入原因，也不能单独证明 handle 可以销毁。

## 决定与固定 ABI v1

独立的 `wke_cursor_broker.h` 在 vendor 与公开 include 中字节一致。`wkeGetCursorBrokerApi(1)` 是可选新导出，缺失返回能力不可用；现有 interaction v2 表不变。结构使用 pack 8、structSize、32 位状态及 64 位 generation/sequence。realm/endpoint 是验证注册表中的随机 128 位令牌，不是可解引用对象或 HCURSOR。

表仅允许引擎 owner 调用：`beginRealm`、`createEndpoint`、`updateIdentity`、`requestClose`、`inspectDrain`、`destroyEndpoint`、`endRealm`。UI 原生任务使用独立的 `onNativeEnter/onNativeLeave/onNativeComplete` 回调；它们不能调用引擎、等待或泵消息。`onDescriptor` 在引擎线程执行，像素只在回调内借用，SDK 必须立即复制进一格 latest slot。上限为 64 endpoint、每 realm 64 普通 native task、256×256 BGRA8；全程不导出 HCURSOR。清理另有每 endpoint 三格固定 continuation（菜单、拖放存储、OLE 撤销）及一格代次取消，取消格中菜单/拖放各有一个可覆盖的动作，互不覆盖；它们不能用于普通工作。完整精确签名以该头文件为准。

初始化后、任何 view/document/plugin 历史之前才能首次 `beginRealm`。成功后不可逆关闭 NPAPI；raw setter 不能重启，虚拟 plugin 注册被拒绝，createPlugin 再次硬拒绝。NPAPI/Flash capability 明确 unavailable。已有 runtime 不能凭 void setter 被采用。realm 内仅 owned create/destroy，拒绝 mixed raw view；endRealm 不恢复 legacy 模式。

SDK 每 UI queue 独占创建、选择及销毁一个非 shared custom handle；其它 window 只保存像素描述。同类型 custom 变化仍发布新 sequence。标准 cursor 使用共享系统资源，none 明确选择 NULL。HWND 身份由真实 PID/TID、view/document/geometry generation 验证，关闭撤销所有迟到选择。

## 生命周期与退休组合 P

P 是受控集成中的明确推理，不是 Win32 提供的退休 fence：私有 handle 不跨 queue、不外泄、不存 class/shared/system cursor；所有缓存和迟到选择引用已撤销；临时借用与原生 modal lease 为零；UI 同一无消息泵/无重入操作中 SetCursor 替换后 GetCursor 确认不同；全局查询成功、合法 flags 且未显示旧 handle；最后 DestroyCursor 实际成功。全局 NULL 只在完整 P 成立时表示没有反证，绝不是单独的销毁依据。全局仍为旧 handle 或查询失败保守保留，并报告证明未完成。普通 suppressed/hidden 状态不被视作永久 fault，也不以默认 quarantine/增长队列交付。

普通 SDK 菜单和既有拖放保留。每个实际 TrackPopupMenuEx/DoDragDrop/OLE 注册与撤销边界进入 UI-owner native scope；嵌套 lease 钉住当前一个 handle，期间只合并像素，最外层结束后重新执行 P。engine 独立标准 popup/helper 不接收 custom handle，保持独立 queue。禁止跨 UI hierarchy 和 NPAPI 未知 borrower 绕过 authority。

UI task 使用 endpoint generation、有限队列、一次 payload disposal 和完成回执。关闭先禁止新工作、取消自有菜单/OLE、在实际 UI owner 撤销注册，再等待已排队/运行 UI task、scope、engine callback 与 COM 借用实际排空；引擎按回执非阻塞推进，UI 不等待引擎。callback/userData 存活直到 destroyEndpoint 成功；窗口消失或时间经过都不代替 drain。

菜单的 HWND/HMENU 另有内部真实资源账本：成功工厂返回后绑定 exact handle、endpoint 与 UI owner；窗口只有自有 DestroyWindow 成功且本次 WM_NCDESTROY witness 到达才退休，HMENU 从未 attach 给窗口，实际 DestroyMenu 成功才退休。资源销毁失败保留 callback storage/owner 和两格资源上限，以初次加最多三次 cleanup admission 重试；未证明销毁时 drained 必须 false。原生工厂/queue authority 契约本身被破坏属于能力拒绝，不能以 semantic FAILED 或窗口消失替代证明。cleanup/identity 两个动作在 UI admission 前原子注册，失败不创建 UI 资源；WM_COMMAND 的值复制与投递异常不能跨 WNDPROC。

native scope 继承正在执行的真实 UiTask 非零 taskSequence；独立 COM invocation 分配新的非零序列，scopeSequence 单独递增，nested timer 恢复上一 lineage。序列耗尽拒绝 admission，不填常量冒充任务。leave 使用进入时同一 receipt，即使代次已撤销。destroy 开始后拒绝重入新 cleanup，避免已证明 drain 后再次挂入任务。

create 在任何 UI 注册前交接有效 endpoint token。若已有 owned view 而后续 bind/register 失败，返回 FAILED 和有效 receipt，调用方保留 hooks/storage 并推进真实清理。FAILED 禁止普通工作，不禁止已进入 scope 的 leave、OLE_REVOKE、terminal disposal、inspect 或已排空的 destroy。requestClose 不要求 scope 为零，重入原生 modal 的 close 返回 pending；不得等待当前调用栈自己的 leave。endRealm 成功后允许最终 shutdown/finalize，但不能重新进入 legacy realm。

关闭动作首次必须执行，即使普通 UI 工作或 modal scope 尚未结束。随后重复 `requestClose` 在清理已经排队、正在运行或动作回调仍在调用栈中时，只返回既有状态；真实资源账本已经排空时也不再创建清理任务。队列到运行的计数交接与重复关闭判断共用 registry mutex，判断后先释放锁，再取得动作 lease 或调用业务回调。FAILED 状态保持不变，closeSequence 不重复递增。

无待执行任务、无运行任务和无动作回调，不足以证明清理结束。COM 引用、原生 HWND/HMENU 归属和资源 admission fault 仍参与 `drained` 判断；真实资源未排空时保留再次尝试固定清理动作的入口。不得手工清零资源账本来获得 drain。2026-10-09 的 x64 与 Win32 有限实测均复现旧源码六项重复 admission 反例；修复后十一项真实 `CreateMenu`/`DestroyMenu`、Windows timer 和实际 task 分配失败回归及十一项 CTest 均通过。覆盖首次 admission 分配失败后的资源释放、动作重入、清理运行中、既有 FAILED 和完成后重复关闭；资源 admission fault 仍保留 false-drain 状态。这些测试使用同一原生线程作为 UI 与 engine owner，COM 仅验证实际账本 API，不代表独立双线程运行或真实 COM 对象验收。新规则已写入源码，随后普通 SDK 的 x64 真实窗口关闭和运行时关闭实测通过。

真实 main-document hook 立即撤销 epoch、清 engine cursor 缓存并发布 NONE；新 CSS 像素暂缓，直到 SDK 的真实 main-context/ready 边界确认严格大于 pending 旧值的 document generation。同一数字 ACK 无效。该 generation 表示撤销 epoch，不承诺与浏览器 document 数一一对应。resize 和 document 更新读取最新完整 tuple，避免各自覆盖另一维。旧 source/target OLE、菜单 command 与迟到 engine task 必须验证进入时的 epoch。

SDK 的 main-context 使用不可回绕的 receipt：奇数表示 pending，仅该 receipt 的精确 CAS 可以提交预留的偶数完成值。重载在请求和执行边界撤销旧 receipt，嵌套 context 产生新值；迟到的外层 ACK 不得清除内层 pending，也不得借用新 context 的授权。最终观察失败只撤销自己的完成值，耗尽保持拒绝。只有 worker、原生回调和 HWND 实际排空后，owner 才能重置 Host 的 receipt。

首次不透明 `jsGetWebView` 尚未选定 Host 时，以进程级不可回绕 revision 包围 lookup；选择 Host 后继续验证原 receipt、view 和完整 broker tuple。参数读取、原生业务调用、JS 执行及响应转换等可能重入的边界保留进入时 receipt。异步工作、响应和事件 sink 同样保留它；任务事件还验证真实 envelope 的 sessionId 与该 context 绑定的会话。新的 context 不得让旧排队工作复活。已进入的业务或 JS 效果不是可回滚事务，也不承诺关闭可撤销正在执行的调用。

实际 atomic/CAS helper 的两架构 14 项回归与 SDK 静态编译支持上述基础实现检查。首次[普通 SDK 实际诊断](../../verification/production-validation/0.5-controlled-cursor-2026-10-09/normal-sdk-native-close-failure-x64.json)已接入新 x64 软件 DLL，完成真实窗口启动和主文档就绪，但关闭超时失败；此历史结果保留。重复关闭修复和重新链接后的新 DLL，在相同普通 SDK 诊断中完成真实子窗口、主文档就绪、关闭、context 撤销、重复关闭、C++ owner 析构及运行时关闭，实际进程约 1.6 秒自然退出；[新诊断与双架构 56 项原生测试记录](../../verification/production-validation/0.5-native-finite-2026-10-09/README.md)独立绑定实际载荷。初始化后的模块仍被保留，没有取得完整卸载或 vendor 线程排空的外部回执；这次所选源码重链接也不证明全部依赖源码的构建资格。Host 的真实 getter 重入尚未验证。默认关闭的旧 Test Kit 分支没有随本次普通 SDK 编译得到验证；它也不承担生产 Agent 主体或实际输入、呈现测量的证明责任。

受控 DevTools 和无 host createView callback 的默认 raw 浏览器新窗口不可创建；host 可用 owned endpoint 实现新窗口。这与保持独立标准 select/tooltip/helper 和普通 SDK 菜单/OLE 拖放是不同的能力边界。不得让构造器抛出成为隐式的 raw-window 拒绝接口。

## 兼容与迁移

旧 getter/旧表及 frozen binary 保持原状。SDK 仅在完整新 ABI/capability 与新 realm receipt 可用时启用 broker；缺失 getter 可以走明确的旧能力路径，不能静默宣称自定义 cursor 或受控 ownership 成立。所有 SDK view 同属初始化前建立的 controlled realm，普通 raw 创建不能混入。

## 备选方案与验证

拒绝只传类型、跨线程 fireCursorEvent、共享 HCURSOR、仅全局 NULL 销毁、固定延时退休和以 quarantine 代替常规资源门槛。验证需覆盖同类型 custom 更新、一格覆盖、generation 迟到回调、嵌套菜单/拖放、每个拒绝及取消 payload 恰好释放一次、register/revoke HRESULT、重入关闭、NPAPI 再启/混合创建拒绝，以及 UI queue 本地选择与全局 presentation/suppression 分离。两架构编译和真实窗口/长期资源验证由后续 owned 执行完成；本文不记录未执行通过。

普通 broker 分配/入队失败必须给出失败状态并释放未交接 payload；source 在 flags admission 前完成可能分配的值和 callable 构造。WebThread pending TaskPair 未 transfer 时独占 Task，transfer 后立即清指针，timer 持有后由真实执行/退出排空释放。timer heap admission 在同一 owner thread 预留容量后才构造 timer，中途无消息泵或 callback；退出 flag 为 atomic，最终 pending admission 与退出发布共用 mutex。priority、deadline 和调度选择不因此改变。

引擎拖出数据从转换器部分构造到调用方借用均由 destroyWkeDragData 的独占 RAII owner 管理；数组取得后先无抛初始化全部 Item，再提交可销毁长度，UTF-8 buffer 取得即挂到对应成员。后续字符串复制、broker payload 分配或 host callback 抛出时释放已经取得的数据。只取一次 WebVector，检查 int 长度上界；默认受控拖出在转换前遵守既有 256 项限制，超限仍走既有 NONE 完成分支，legacy 与自定义 callback 不新增该限制。callback 的局部无分配 scope 恢复 CheckReEnter 和拖动标记，不吞异常，不宣称 GC、page 生命周期或 Blink 操作已回滚。2026-10-09 的 full-native-suite-v5 在同一开发机 x64/Win32 各通过 87/87 项注册 CTest，包含 11 项公共 dragdata 源码体回归中的部分转换、下游分配和 callback 异常路径；有限 fixture 不证明完整原 TU 环境、legacy 内部异步 task 或真实 OLE 生命周期。后续默认 legacy 同步路径在改变通知/重入计数前分配 task，由局部 scope 在成功和异常出口恢复计数，并用 task 的幂等 complete 关闭自身计数；同步 consumer 抛出时仍传播异常。新增三项源码体回归检查分配失败、同步成功和 consumer 异常。2026-10-09 的 full-native-suite-v10 在同一开发机 x64/Win32 各自的注册 CTest 中，本叶片三项与原 dragdata 十一项均通过（各 14/14）；当轮八个 Owned 阶段均自然 0/0、allGenericPassed=true。此事实绑定该轮 083f78... 的实际源码与字节夹具，不关闭完整 vendor、GC、COM/OLE 或发布资格。既有异步 raw-task 交接协议未改变，不能继承同步路径的清理资格。

heap 仅容量不足时受 max_size/overflow 保护几何扩容，避免每个 task 精确 reserve 产生积压平方复制。updateIdentity 对既有失败和过程中新增失败返回 FAILED，不能被当作 SDK 文档 ACK 完成。

受控外部拖入只支持来源允许的 COPY/NONE，且需要实际 owned payload 复制与 counted admission。异步 COPY 可在导航后取消，来源文件始终保留；不宣称 MOVE 完成。未来 MOVE 需要实际接收完成协议。CF_HDROP 用 HGLOBAL handle 调 DragQueryFileW，字符容量包括终止符，256 文件、单路径 32767 字符及 1MiB 总路径预算；STGMEDIUM 与 GlobalLock 均由 RAII 在正常/异常路径释放。它不证明 inherited ClipboardUtil 其它格式的任意外部 provider 都符合完整资源预算。

现有 `CWebView::_initPage` 内部 `WebPage/WebPageImpl` 部分构造的通用 OOM 回收尚未证明。这里的 receipt 不能追认上游原有构造事务为安全，也不能把该未决项包装成普通 quarantine 或 accepted 泄漏；`pending_ctorOOM` 继续未关闭。

### 构造失败的有限支持边界

**背景。** 独立 WebPage 外壳尚未初始化 engine 时，handler 的普通分配异常可以安全撤销。原实现先向 WebPage 全局集合登记，再分配 handler；CWebView 也在 page 创建前登记 activation ID，而构造失败不运行该对象析构。进入 WebPageImpl 构造后可能已启动 compositor、建立 frame/Page 注册和 clients；普通 close 不支持这种未完成状态，不能让外层 catch 返回已回滚的 FAILED。

**决定。** WebPage 用局部真实 owner 保存 handler，完整初始化自己的成员后才登记全局集合，再交接 handler。CWebView 成功登记 activation ID 后单独准备尚未 init 的 WebPage 外壳；外壳构造抛出时撤销该 ID 并重抛，ID 和 broker 历史仍单调前进。随后调用 initWkeWebView/WebPage::init 起进入明确的非恢复构造阶段，直到默认 handler 设置和最终 g_liveWebViews 登记完成；这个阶段的 C++ 异常调用 std::terminate，不跨 ABI 返回普通 FAILED。最终 std::set 登记的分配失败也由此覆盖，不提前发布半构造 view，不依赖 C++17 node_handle。恢复阶段不调用普通 close；若未来增加外壳删除路径，只可删除未调用 WebPage::init 的完整外壳。

**兼容性。** 成功创建、正常关闭和已有 owned view 后 bind/register 失败的 receipt/drain 契约保持不变。该构造边界适用于 CWebView 的既有创建入口；此前可能从不安全 engine 部分构造传播的异常现在明确终止进程，调用方不得把它当作可重试 FAILED。前置 WebPage 外壳准备失败仍可向既有调用方传播；在 broker 工厂内仍由既有失败状态处理。WTF/PartitionAlloc 的 fatal OOM、Oilpan GC 分配/收集及其它非返回故障不因此成为可恢复能力，不承诺完整 Blink/V8 OOM 回滚。

**验证。** 2026-10-09 的 full-native-suite-v5 在同一 Windows 开发机 x64/Win32 各通过 87/87 项注册 CTest，包含 7 项公共 ctor_shell、11 项 dragdata 与 12 项 worker_lifetime 有限回归。CTest Owned Job 的根进程/监督器退出码为 0/0，无强制 disposal 并自然闭合；配置/编译的 0/124 保留 genericPassed=false 的受控尾进程 partial，整体 allGenericPassed=false。该同机实测不关闭 Win7/PE、GC/OOM、完整原 TU 环境或 DLL 卸载资格。必须检查 _initPage 的全部调用位点，编译实际受影响文件，并用真实源码的有限回归检查 handler 异常没有发布 WebPage、外壳异常撤销 activation、成功登记不被撤销，以及 engine 阶段异常真实终止而非返回 FAILED。可用独立子进程观察终止，不把替身状态或模型回归当作完整 engine 验收。compositor producer admission/counter 补偿、GC owner-thread 退出、完整 engine abort 和 DLL 卸载资格继续未证明。

Win32 原始约束见 [GetCursor](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getcursor)、[DestroyCursor](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-destroycursor)、[RegisterDragDrop](https://learn.microsoft.com/en-us/windows/win32/api/ole2/nf-ole2-registerdragdrop) 和 [RevokeDragDrop](https://learn.microsoft.com/en-us/windows/win32/api/ole2/nf-ole2-revokedragdrop)。它们支持线程/非 shared/注册借用边界；组合 P 的充分性仍是本集成的明确推理。

拖入字符与 medium 责任见 [DragQueryFileW](https://learn.microsoft.com/en-us/windows/win32/api/shellapi/nf-shellapi-dragqueryfilew)、[IDataObject::GetData](https://learn.microsoft.com/en-us/windows/win32/api/objidl/nf-objidl-idataobject-getdata)、[ReleaseStgMedium](https://learn.microsoft.com/en-us/windows/win32/api/ole2/nf-ole2-releasestgmedium)。
