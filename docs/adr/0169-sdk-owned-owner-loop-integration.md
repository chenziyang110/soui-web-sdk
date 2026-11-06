# ADR-0169：SDK 自有 owner loop 与普通输出 continuation

- 状态：实现候选；未发布，收益与正式接入待验证
- 日期：2026-11-07
- 目标版本：`0.7.0-preview.6`；不覆盖 preview.5 的源码包或原生安装

## 依据与范围

R54 的 preview.5 匹配消费者实际发送并接收完整3125个正标签输入，完成74315次真实Bridge往返；最终ACK500失败，严格窗口内2108条tag0使输入资格失败。单次受观测SDK pump最大1.7679ms，而load阶段paint/timer最大间隔约28.964秒。该失效诊断支持进一步检查持续消息调度，不能证明某一处SDK代码的唯一因果，也不是通过的输入或FPS对照。页面在阶段观测点隐藏/失焦的边界保留。

ADR-0168 的私有Win32原型已实际完成v143/19041/x64 Release一TU构建与20项行为测试，原期限未延长，所有Job0。它验证了pending/queued责任、worker并发、真实nested/sent路径、旧token/timer、取消失败和关闭边界；只支持单active root，不继承为正式SOUI/COM/SDK通过。

本决定将同一调度核心实现为SDK能力，而非改Demo的取消息顺序。普通应用显式使用SDK消息循环；未接入的既有Win32循环保留legacy通知。MiniBlink共享Engine、输入ordinal/FIFO、帧策略、Bridge协议和授权保持原契约。

## 公共入口与依赖

新增 `soui/host/windows_owner_loop.h` 中的 `windows_owner_loop`，只依赖现有Core与Win32，不依赖SOUI、Demo或业务代码。它拥有本线程的取消息循环，接受可选dispatch、idle、idle分类、Quit谓词和GetMessage错误处理回调；默认dispatch仍是TranslateMessage/DispatchMessage。调用者继续负责COM初始化和Host关闭，SDK不改线程优先级、timer精度或全局hook。

可选 `message_retrieved` 是被动观察入口，在GetMessage成功返回消息（含WM_QUIT）后、SDK工作或其他hook之前调用，用于保留应用原本可在取消息点记录的原始metadata。该观察者不得取走/派发消息或重入；未设置时不新增GetMessageExtraInfo/QPC等元数据查询。`quit_requested` 同样是只读退出谓词，不得投递/检索消息、派发回调或重入；循环会在可能派发sent消息的取消息API返回后复核它。测量工具不能把SDK前置工作之后的读取伪称为“紧接GetMessage”。

同一对象禁止重入Run；同一线程不同SDK loop允许正常嵌套，以匹配SOUI dialog loop。内部使用LIFO root scope，退出内层不取消外层服务资格。私有dispatcher消息仅在当前SDK Run中直接消费；其WndProc始终承担绕过SDK Run的legacy回退，不能用跨DispatchMessage的TLS布尔值冒充root。

SOUI适配为单独的可选头，Core不新增SOUI链接依赖。正常模板使用SDK的 `soui_owner_application`：它在SOUI基类刚构造、尚无外部filter/idle/loop指针时安装SDK factory。默认factory的具体类型是vendor实现细节，因此不提供静默替换任意现有SApplication的helper。自定义SApplication/factory继续原路径，应用可自行组合公开Win32 loop回调；不覆盖其factory。

SOUI factory在替换前预先构造主loop，避免原SetMsgLoopFactory销毁旧loop后首次分配失败；后续dialog由同factory创建SDK loop。外部OnMsg/PreTranslate/OnIdle/IsIdleMessage与m_bRunning/m_bQuit语义由适配器保留。普通SDK模板接入本能力；没有用原生WebView2 Demo局部技巧代替SDK实现。

现代Win32模板与auto模板也在Host准备前构造公开 `windows_owner_loop`，以回调保留既有dispatch/DevTools翻译，并在正常窗口销毁时退出。早到WM_QUIT或取消息失败仍执行原关闭、有限批次legacy检索和Runtime排空路径；新loop不跳过这些清理责任。auto选择MiniBlink时无普通continuation工作，沿无工作快路保留原消息派发。

## 私有调度与WebView2接线

每owner线程一个共享dispatcher、有限登记与root scope；登记寿命独立于文档generation。worker可发布普通pending；真实queued-poke、pending、服务中状态和epoch分别记账。仅empty→pending投首个poke，已有pending期间的worker发布只更新epoch，不重建跨slice posted链。通知、登记与timer ID不复用，耗尽时回退。

SDK Run使用无过滤的普通消息检索；只有Peek确认当前没有可用系统消息，才提供一次普通continuation。每次只调用一个adapter的原bounded pump并round-robin，之后重新选择消息。普通dispatch后与私有poke消费时不直接pump：私有消息只确认真实queued token，不清pending/epoch。这避免持续请求/回复补位自行维持posted队列而长期挤占input/paint/timer；它不按QS_INPUT过滤重排键盘、IME或posted命令。

业务idle callback返回后重新检查队列，不沿用callback前的空队列判断。一次业务idle之后，下一次真正空队列机会先给pending SDK一次服务尝试，避免持续return true的有限idle callback饿死输出；服务不重置业务idle计数或开启新idle轮次。业务idle返回false或应用没有idle callback时，SDK pending仍有独立服务机会。没有工作时使用快速pending提示，不为普通MiniBlink应用增加逐消息registry mutex。私有poke不按外部dispatch再次预投通知。

进入外部dispatch/首个外部idle callback前预置coalesced fallback poke；只在普通pending时armed的单safety timer覆盖取消息API内部sent重入等入口。正常root吞吐不靠10ms节拍。绕过SDK Run的modal临时恢复legacy；真实回到SDK迭代后，快速resume提示只恢复仍live/pending、未serving且无永久故障的登记，保留pending/epoch/queued身份。资源失败、epoch耗尽、pump异常不得被这种恢复重新授权；Post/SetTimer失败进入永久legacy且可查询失败。KillTimer失败保留原ID/窗口清理义务、禁止rearm并锁存失败，不能把清字段当取消成功。旧消息不得授权新登记。

在有限的外部消息和可返回回调条件下，该安排提供系统消息选择机会；持续外部posted/sent流、无空隙输入、过滤消息的modal和单个不返回的回调仍无普通输出有限延迟保证。对吞吐和输入尾延迟的影响必须实测，不能据此保证绘制完成或物理帧率。

WebView2在普通SDK loop上下文存在时自动登记，捕获真实adapter state的弱寿命，不捕获裸adapter指针。新mount登记使用新incarnation，导航不永久取消线程登记；卸载/失败/loop退出撤销或恢复legacy。

普通输出才走continuation；控制/续租/清理/生命周期仍使用原即时通知责任，原lane FIFO/优先级、COM前admission credit释放、原deadline、owner STA与pump重入guard不变。active-drain通知合并继续保留。

原public pump真正消费Host通知时才清legacy wake_pending；SDK continuation不清尚排队通知。public pump若有有效登记，通过同一登记的legacy service入口更新pending/epoch，重入拒绝仍由外层持有通知责任。私有服务抽取原pump主体，保留current_source、代际、序列化、COM、失败及空尾重查；不重复引入PostWebMessage替换或删除安全检查。

## 失败、兼容与回退

创建/登记/资源失败有可查询状态并使用原通知路径；不能为了隐藏初始化失败而报告cooperative已启用。所有loop服务、timer、登记撤销和析构均在创建线程；只有普通发布允许worker。真实回调寿命由adapter state保护，不以独立dummy token冒充。异常退出Run必须释放root scope并恢复legacy。

新增头/API要求匹配6版头/库与应用重编。已有Host接口及旧应用循环继续工作，不宣称只替换二进制就能获得新loop收益。Core不引入新DLL或vendor改动，小量EXE/archive增长单独实测。

回退可将SOUI便利类型改回原SApplication，或将Win32入口改回原循环；adapter没有有效上下文时自动走原通知。SDK及header/lib按完整版本回退，不能混用已冻结5版与新6版内容。

## 正式采用门槛

先完成实际SDK接线、正常模板和独立消费者，复用原型反例并新增nested SDK root、真实adapter寿命/导航/关闭、私有服务与legacy通知交错反例；覆盖x86/x64及可选SOUI头的正常编链。原型20通过不代替这些检查。

最终同负载对照使用正常SDK入口，绑定真实头/库/内核及测量源码。私有continuation不会全部经过旧Host消息计数，旧app-only pump计数只能标为局部观测，不能冒充完整SDK耗时。完整输入/帧/资源资格、未知物理显示及原失败继续如实记录。未取得有效收益与交互回归前，不关闭M01/W02/R01或声称对齐Electron、生产就绪。
