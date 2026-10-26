# ADR-0124：帧截止时间、资源通知与有界画布所有权

- 日期：2026-10-26。
- 状态：源码候选；编译、运行、性能与兼容性由验证人员执行。
- 基线：`44eff565bf9ab7778a8b9fdaa5403cc3b6f568ed`；依赖 ADR-0122。

## 背景与决定

W00 的 legacy deadline 任务只修正非 independent 队列。默认 DIRECT_HWND 的生产卡顿原因尚无 profile，不能把这项修复描述为已测收益。

W05 以现有 SDK frame_pacer、WebPageImpl 的分数相位和迟帧跳过为基础。SDK 的整数 QPC 商与余数共同决定每个 deadline，避免每次截断造成累积漂移。内核仍只依赖自己的单调时钟；mainFrameRate 的现有 owner 配置把同一个分数间隔交给 commit/composite，完成的 main frame 带 frame id 与目标时间，已按此截止时间接纳的提交不再叠加 record、composite、legacy paint 的独立 lastTime 等待。旧嵌入者未配置此能力时保留兼容节流；隐藏、尺寸和 DPI/刷新率变化重建 phase，恢复不补跑旧帧。

W06 把 record 的资源准入与 pacing 分开。owner 注册资源等待后重查条件；action 发布、完整 action frame 应用与 scene 激活触发合并通知。通知使用独立存活状态和 generation，关闭后任务只回收自身。正常路径不再每 1ms 重跑 commit 或每 8ms 轮询 draw。动作区间、ready-through-action、pending/ready 单槽及 scroll fence/ACK 保持原语义；取消不代表资源就绪。

W07 使用最多两个有尺寸/设备代际的画布槽。呈现、DC/截图读取在锁内取得持有所有权的 lease，锁外调用客户端或 GDI；lease 仍被读取时不可覆写该槽。两槽均被持有时保留 dirty，并在释放后由完成通知重试。不能解锁后保留裸像素。mirror 的默认消费者需求为 true；只有 capability 检查通过且 DIRECT_HWND 无 DC/截图消费者时才跳过镜像。首次请求 DC 强制恢复消费者并从当前画布建立快照。透明窗口、首帧、resize 保留全量处理。保留已有 dirty copy，新增计数不能将其重复算作新优化。

W08 的多 worker、可见资源优先候选默认关闭，仅接受 2–4 个 worker，带任务/估计 raster 字节上限。不能取消已开始工作或把缺失资源标为 ready；在任何候选 dispatch 前先核对整帧受控资源；未知/超限形状只走原 worker0。不得以近似 picture 大小替代引用所有权上界，当前通用 picture 的可用资格域仍未证明，W08 资格门保持开放。可见/预取优先级以 viewport coverage 为依据，不能扩大既有预取范围来隐藏露白。缺 profile 时状态为“待数据决定采用”。

W09 先记录 layout/record、scene 构造与复制、copy bytes、锁持有和客户端 callback 时间，再选择后续缓存。确定性的 scene pending→ready→active 所有权转移可直接消除重复 vector copy；版本、action fence 和 input prediction 不变。未经测量的布局或稳定节点缓存不默认启用，导航、尺寸、DPI/style/geometry 和 scroll epoch 的失效是采用前置条件。

## 所有权与恢复

`WebPageImpl`、`LayerTreeHost` 和 `ActionsFrameGroup` 属于内核；不包含 SDK 头。SDK frame_pacer 继续独立持有 timer 线程，停止后 join 再关闭句柄。跨模块配置/诊断由版本与 structSize 检查的窄接口连接，generation 不可跨页面复用。root 统一维护 wke 适配、公共 ABI、来源补丁、清单和 Draft PR。

任务投递拒绝和分配失败保留 dirty/等待意图，取消票号不得清除更新票号。资源尚未就绪时只等待生产者事件；异常投递失败可在后续 owner 服务入口重试，不恢复固定频率的正常轮询。按工作包回退对应生产修改和回归源码，不混用新头与旧 DLL。

## 验证交接与当前边界

维护者明确授权本阶段只实现代码、回归源码和静态审查；本 agent 不编译、不执行测试、VM、benchmark 或 profiling。验证人员一次性执行可控时钟、早唤醒/拒绝/关闭、通知注册前中后、资源取消、像素/alpha/DC、双槽持有、resize/DPI、scene/action/scroll ACK、低核与多核矩阵，并配对测 trace 开销。源码测试不等于已通过测试，缺数据的采用决定和生产门槛保持未关闭。

## Confirmed view trace identity follow-up

The owner may bind confirmed view/document/geometry metadata without an input dispatch. The setter accepts a zero-input event and stores only identity fields; it neither pushes input TLS nor changes production scenes, action fences, pacing or buffers. New frame layout/record/commit metadata starts from this base. Existing scene and framebuffer lease descriptors keep their captured identity. A newer identity retires only pending processed coverage with explicit stale terminals; bounded diagnostic overflow records causal loss. Late old-generation dispatch returns cannot repopulate coverage. Navigation intent alone is insufficient for this setter. The SOURCE cases cover first non-input frame identity, invalid/stale updates, geometry/document transitions, causal isolation and immutable captured descriptors. No compilation or test execution is authorized.

Presentation progress follows the frame identity captured for the actually painted scene/buffer, never the newest pending main frame. Painting an older active scene cannot retire newer unpresented processed-input coverage; cancellation can still restore that coverage. Unknown or incomplete paint does not advance progress. Scene readiness and action/scroll fences are unchanged.

## Paint/client lifetime and deferred owner close

Canvas ownership does not retain the client or outer host stack. Each view therefore has a cold shared activity gate covering the whole composite draw, legacy retry and owner paint/snapshot entry. Close seals admission and waits asynchronously for active scopes to unwind; raw cross-thread destruction is rejected. A single retained close intent and one cancelable owner-task ticket per view preserve intent on allocation/post rejection. Bounded owner service examines at most 32 live views per call with an address-key cursor reconstructed after every callout, never a retained iterator or reused object identity. Native window-to-view destruction may cascade once; repeated view/window close and reentrant finalization are suppressed by close-callout permits. Broker close seals the same gate and its existing drain predicate additionally requires zero paint activity; authorization and owned permits stay unchanged. Runtime shutdown seals all paint admission and cannot complete while activity, close callouts, close intents or queued task receipts remain. Task disposal and stale tombstones cannot dereference a deleted target. No lock is held across client callbacks, task posting or deletion; activity-scope release is the final operation after the outer stack has finished all host access. Rejected task progress resumes from bounded owner opportunities/finalization retries, without immediate repost loops. Deterministic SOURCE cases cover nested close, worker activity, duplicate/post-rejected/stale tasks and shutdown fences; they are not executed here.
