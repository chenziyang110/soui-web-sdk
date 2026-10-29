# ADR-0126：GPU 输入到可见反馈的独立候选与资格门槛

- 日期：2026-10-26。
- 状态：proposed；设计/诊断候选，默认不启用，待软件主线 profile；不是已实现的新硬件后端，也不是不采用结论。
- 关联：ADR-0005、ADR-0122、ADR-0125；具体交接见 `docs/implementation/input-ui-gpu-gate-20261026.md`。
- 核查：在最终软件栈上按当前GpuSkiaSurface/LayerTreeHost符号重新静态核对；完整生产源码身份见Draft PR122及根清单。这里只交设计/诊断候选，未编译、执行测试、启动VM或测性能。

## 现有能力与未闭合边界

当前 `GpuSkiaSurface` 是实验性的 WGL/Ganesh surface。`initialize` 在调用线程取得 DC、创建并绑定 WGL context、创建 Skia context 和一个 texture/FBO；`resize` 重建 framebuffer；`beginFrame` 修复当前 context/GL state；`present` 走 blit/quad 与 SwapBuffers；`readback` 用 glReadPixels 和复用 vector 回到 CPU canvas。它不是一个已经交付的 DirectComposition 后端。

`LayerTreeHost::paintToMemoryCanvasInCompositeThread` 按实验开关选择该 surface，合成线程末尾可一次 present；`shutdownCompositeThread` 尝试在合成 owner 上删除 GPU surface 后退出线程。这些已有实现不能重新计作 W11 新成果。`present` 的成功/计数也不是 GPU fence、桌面首次观察或屏幕发光回执。

当前候选没有显式的两帧 lease/fence 状态机。`beginFrame` 的绑定失败没有完整逐帧结果协议；运行期 present/readback/context 失败到软件重画、dirty/action 保留、设备恢复的资格尚缺。当前 GPU 绘制/读回及客户端通知与 composite lock 的关系仍须独立审查；软件 W07 的 lease/锁外 callback 改动不自动证明 GPU callback 可重入或安全关闭。历史 GPU 生命周期记录还保留资源增长和旧关闭失败，不能用本轮源码或正常短退出关闭它们。

## 决定

GPU 工作单独 PR、能力开关、来源清单、正确性/资源/性能证据和回退。软件 DIRECT_HWND/independent 主线保持默认；GPU 未启用时不创建 context、surface、worker、周期唤醒或预热资源。先交软件数据和 W01 诊断，再决定进入何种 GPU 实现；无数据只说明待数据，不能说明“不值得采用”。

### 所有权与代际

一个明确 compositor owner 持有 WGL/Ganesh/context、GL 对象和提交/释放动作。UI 线程只持有不可变帧描述、可撤销消费需求和有生命周期的 lease，不从 UI 或任意 worker 调 GL/Skia owner 对象。每个对象绑定 view/document/geometry、device/context generation 与实际 owner；旧 HWND、旧尺寸、旧 device/context 的任务不能操作新对象。

初始化、make-current、begin/draw/flush、提交、fence、readback、释放都有类型化成功/等待/失败结果。不能因 `canvas()!=nullptr` 推断本帧 context 有效。能力/函数表按设备/context 代际核查；现有进程静态缓存的资格不直接推广到跨适配器或重建场景。没有可验证能力时保留软件路径，不提高最低 Windows 要求。

### 两个在途与替代规则

候选最多两个 GPU framebuffer/pixel lease，所有代际合计在途 GPU 提交也最多两个；另允许一个无像素的 pending visual descriptor。front、GPU执行、平台提交或 readback 正在持有的 lease 都不可改写，不能因 enqueue 成功立即复用。旧代际释放前不得按“每代两帧”叠加无限资源。

只有尚未发布、语义可替代的视觉描述可被最新描述替换。保留 dirty 并集、明确的输入覆盖分段、action/scene 事务和取消回执；DOM down/up/key/IME、滚轮位移与屏障不受该替代影响。覆盖分段继续有界，溢出标 causal_lost；不能丢产品输入或把缺失覆盖补成已显示。两槽占满时停止新增 GPU admission，以合并 pending 或软件进度解决，不能无界排队、忙等或饿死后台任务。

候选起始硬上限为每 pixel lease 64MiB、最多两个；CPU readback 同时一个、最多64MiB；另为 Skia/tile/cache 分别冻结字节预算。尺寸乘法和平台上限检查先于分配，超过任何上限走软件。上述是待验证候选上限，不是实际占用或采用依据；更改必须重冻配置和受影响数据，不能按屏幕/核数无限扩张。

### Fence、呈现与输入新鲜度

提交前仍满足现有完整 action 区间、raster/scene ready、scroll coverage/action fence 和生命周期要求；取消不等于资源就绪。GPU command-complete、平台 present/ownership 与 readback lease 是不同条件，资源只在实际所需条件全部满足后归还。不能把 flush、SwapBuffers 返回或一个 GL fence 冒充桌面 T7，更不能冒充发光 T8。

平台/驱动隐藏的提交深度也必须量测；只限制应用对象数并不能证明显示队列只有两帧。若现有 WGL 路径无法证明所需进度与上限，则该路径不能获得“两在途”资格；可另立受支持 presenter 候选，仍需版本/能力检测及软件回退。禁止每输入 DwmFlush、全局实时优先级、永久提频和零间隔重试。

### 状态与软件回退

状态为 disabled → qualifying → ready → quiescing/lost → software-recovering → software-active；显式新设备代际/重新资格确认才允许再次进入 qualifying，同一故障不得逐帧重建。

无 GPU、功能不足、软件 GL 被识别、初始化/分配/绑定失败：不发布 GPU active，保留输入/action/dirty，使用软件渲染。运行期 fence/present/readback/context 失败：停止 GPU admission、使旧代际通知失效、保留尚未证实提交的 dirty/输入覆盖，走实际 CPU 资源重建与全量首帧；成功的软件提交后才能确认对应进度。不得将旧 GPU 像素或取消资源宣称 ready。

正常退出在 owner 上停止接纳、取消未开始任务、等待有界实际在途/lease 完成，释放 canvas/Ganesh/GL接口/FBO/texture、解绑/删除 context，再释放 DC/HWND lease，逐项恰好一次。context 已失效、不能 make-current 时须使用经验证的 abandon/失效资源释放分支；现有 `GrContext::abandonContext` 可作为待验证入口，不能盲调失效 context 的 GL delete。cleanup post 失败或 owner 未退出时保留 intent、对象和模块/线程引用，报告 pending/失败；不转到 UI 强删，不伪造 drained。

### Resize、DPI、透明与 readback

resize/跨屏/DPI/刷新率变化重建 geometry/device/context 关系和分数 phase；不重放旧 pulse。旧 lease 仍被消费时不释放或改尺寸；新代首帧全量重画，之后才讨论 dirty 优化。透明、alpha/premultiply、通道、上下翻转、缩放及文字几何均与软件 oracle 比较，不能用 opaque strip 诊断通过替代。

DC/截图/readback 是显式消费者需求；没有消费者才允许省去 CPU mirror。请求绑定 frame/size/device generation、一个在途额度和明确字节上限，完成前持有 lease；失败返回明确不可用/重试，不能返回过期或已释放画布。客户端 callback 锁外执行前必须拥有可重入、可关闭的 lease/owner pin；仅 unlock 后继续使用裸 surface 不成立。

## 采用与关闭标准

只有软件关键正确性与可比 profile 就绪、GPU可处理部分确实在相关输入关键路径上且预期收益超过噪声，才进入实现/实测。采用须同时满足输入/视觉/生命周期、两在途与字节预算、软件回退、平台矩阵、CPU/内存/后台进展及第4节输入/帧预算；已确认卡顿争取 P95/P99 改善30%，基线已优于绝对目标时不机械强求比例。关键场景回退超过 max(5%,噪声界) 必须调查。

错误输入、错帧/露白、fence/coverage/ACK失真、UAF/死锁、资源持续增长或恢复失败即关闭该候选，回到软件并保留失败原件。资料不足标待数据；不采用必须给出具体 profile/成本与后续触发条件。当前结论仅为默认关闭的设计/诊断交付，硬件实现、编译、正确性、恢复及性能资格均未关闭。
