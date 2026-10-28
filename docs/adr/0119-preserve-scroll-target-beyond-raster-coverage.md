# ADR-0119：在临时光栅覆盖边界保留已接收的滚动目标

- 状态：proposed；方案已选定，生产实现、调度闭环回归和完整内核运行仍待验证，不构成发布批准。
- 日期：2026-10-23。
- 范围：兼容内核的 `ScrollController`、输入 Snapshot 及 `LayerTreeHost` 帧调度；不修改产品业务、原生输入来源或公共交互 ABI。

## 背景

`ScrollNodeSnapshot::advanceInputPrediction()` 和 `ScrollController::MotionAxis::addDelta()` 当前都将目标裁剪到真实内容边界与临时 raster coverage 的交集。方向上还有少量覆盖空间时，整条输入仍可返回已接收；超过临时覆盖的距离没有独立保存。随后同一 scroll epoch 发布更大的覆盖范围，也不能恢复被裁掉的目标。`LayerTreeHost::tickCompositorScroll()` 在新场景激活前计算下一帧需求，激活后的恢复还可能漏掉一次必要调度。

外部回归直接编译生产 Controller 与 Snapshot，初始真实范围为 `[0,1000]`、已准备覆盖为 `[0,300]`，先即时输入 `+250`，再平滑输入 `+100`。第二条之前发布新覆盖时终点为 350；第二条之后才发布时终点为 300。两架构隔离来宾执行均观察到两条已接收、所有采样均在覆盖内，但较晚扩展场景失败。此结果证明该控制器反例中的输入丢失，不代表完整内核、真实滚轮或 GUI 性能已经验收。原始回执须随实现验证整理，不能以截图或像素采样差异代替真实 offset。

源位置主要为 `third_party/sources/miniblink/cc/input/ScrollController.{h,cpp}`、`cc/trees/ScrollTreeSnapshot.cpp`、`cc/trees/LayerTreeHost.cpp`。已有覆盖发布 fence 和浮点采样修复继续保留，见[覆盖维护说明](../maintenance/scroll-coverage-20261023.md)。

## 选定方案

### 逻辑目标与可绘制目标分离

每个 MotionAxis 保留一个逻辑目标，只按真实内容的最小、最大 offset 裁剪。当前可绘制目标由逻辑目标再次裁到已准备的覆盖范围，实际 position、visual offset、绘制和输出采样始终受此范围限制。可以即时计算可绘制目标，不要求重复存储第二份可变目标。

`smooth` 仅决定插值方式。两种输入都累加到同一个逻辑目标，即 `logicalTarget + delta`；非平滑输入可以立即展示当前覆盖允许的位置，但不能用 `position + delta` 无意覆盖此前已接收、等待覆盖的距离。例如逻辑目标 350、实际位置 300 时，再接收非平滑 `+100`，逻辑目标应为 450，而非 400。

真实内容边界仍正常裁剪。新节点身份、新 scroll epoch、成功同步的点击 barrier、明确撤销资格继续取消旧目标，不将目标跨身份转移。反向输入先更新逻辑目标，再按可绘制范围决定实际运动；不能把反向滚轮机械解释为取消全部待完成距离。

### 同 epoch 更新和暂停

同 epoch 的 base、acknowledged total 与真实 bounds 更新继续使用原累计 consumed 坐标系。临时覆盖的缩小或扩大只更新可绘制限制；不得通过 `setBounds()`、`advance()` 的位置裁剪或 `sample()` 的位置校正悄悄抹掉逻辑目标。真实内容缩小可重新裁剪逻辑目标。数值校正与主动取消必须区分，已有浮点 epsilon 保留。

到临时边界且没有可绘制运动时，`needsAnimationFrame()` 为 false。保留目标不等于持续请求帧，不增加忙循环或定时重试。新 ready 场景允许继续运动时，恢复插值。

覆盖等待期间的墙钟时间不能当作动画运行时间积分。采用内部重锚标记：只有“因覆盖暂停 → 新覆盖允许继续”的转换置位，下一次 `advance()` 记录新的时间基点、不积分等待时间；后续帧正常推进。不能在每个同 epoch ACK 或场景激活时重置时间，否则连续提交可能阻止动画前进。

### 接纳预测仍保留真实边界和身份规则

Snapshot 的 `inputPrediction` 表示逻辑目标，不是已经显示的位置；它只按真实内容 bounds 限制。当前 owner 的 base、实际 visual 与资源 coverage 继续各司其职。`refreshCoverage()` 以及同 epoch 复制预测时不得再次按临时 coverage 截短逻辑目标。

接纳仍要求有效身份、epoch、有限输入、支持的轴、可命中的正尺寸 viewport，以及已有资格和资源 fence。目标轴在真实内容中既无可改变的目标、也无待完成目标时，继续允许原有主线程回退；不能为了保存距离绕过 wheel handler、复杂变换等 main-thread reason。

防止“接收后完全没有可移动内容、也没有任何 producer 推进”的余量检查，应相对于 base 与候选逻辑目标判断，而非将逻辑目标强制限制在 coverage 内：

- 候选目标位于 base 的某一侧，已准备覆盖须在该侧有实际余量。零 viewport、空 raster 或该方向零余量保留拒绝，不能接收后无限等待。
- `base=0`、coverage `[0,300]`、旧逻辑目标 350 时，`-100` 得到目标 250，仍在 base 的正方向；不能因为 raw delta 为负、base 左侧没有覆盖而错误回退。
- 候选目标等于 base 且改变了旧逻辑目标时，属于收回未完成目标，可以接收，不需要申请另一侧资源。
- 逻辑目标已到真实末端、但 base 尚未到达时，同方向的后续输入可以保持原目标并推进输入序号，不能因目标饱和触发回退而取消旧距离。只有 base 与逻辑目标均处于真实边界、该方向没有可完成位移时，才保持原有边界回退。

这些条件需逐轴验证；零 delta、非法序号和无可用轴继续拒绝。输入返回已接收后，其目标必须留在当前身份的账本中，或由已有明确取消规则终止；不得把临时资源限制当作隐式消费。

上述方向、覆盖余量和仍在途的比较统一使用既有 `kMovementEpsilon = 1e-6`，与 `sample()` 及 `isActive()` 的静止定义一致。候选与 base 的差、方向上 coverage 与 base 的差达到该阈值才视为可推进；候选回到 base 附近时，旧预测与 base 的差也达到该阈值才属于收回在途目标。不能用严格不等把真实末端约 `7e-7` 的累计舍入残差长期认作未完成运动。此容差不扩大真实内容或 raster bounds，不取消资源 fence。

### Producer 与帧唤醒闭环

正常正尺寸 viewport 下，向临时边界移动会产生真实 sample。现有链路已经将其合并通知主线程：

1. `tickCompositorScroll()` → `postCompositorScrollUpdateToMainThread()` → `dispatchCompositorScrollOnMainThread()`。
2. `WebLayerImpl::setScrollPositionFromCompositor()` 更新 base/ACK，并调用 `setNeedsCommit()`；随后 `LayerTreeHost::setLayerTreeDirty()` → `WebPageImpl::onLayerTreeDirty()` → 单个合并的 `CommitTask`。
3. `beginMainFrame()` 的 `AutoRecordActions::leave()` 调用 `recordDraw()`，以新的 base 计算有界 coverage。
4. 无需等待的 ready transaction，或 raster action 完成后的 `onActionFrameApplied()`，通过原有 resource fence 请求绘制。
5. `tickCompositorScroll()` 激活 ready scene 后重新检查全部活动 controller 的下一帧需求，确保恢复后的动画还有后续帧。

本轮先复用这条链路，不增加独立 producer 队列，不扩大预取面积，不让未 raster 完成的场景提前激活。主线程未调度、页面销毁、明确资格撤销、真实内容变化等条件不属于能够保证持续推进的前提。

只读链路存在不等于端到端闭环已验证。必须以回归证明最后一个真实 sample 已触发新场景，并且不再输入、不再轮询时也能恢复。若出现已接收、零可绘制位移且没有 pending producer 的合法反例，应先补充有界、按身份去重的 producer 请求设计；不得临时以永远返回 `needsAnimationFrame=true` 掩盖。任何新增请求要复用现有生命周期清理，不因相同未改善 coverage 的新 sceneVersion 形成自激循环。

## API 与调用点

预计保持 `ScrollController::enqueueDelta()`、`sample()`、`activateScene()`、`captureBarrier()`、`synchronizeBarrier()` 的公共签名，以及 WKE interaction ABI、Bridge schema 不变。逻辑/可绘制 bounds 和恢复时间标记在私有 MotionAxis 中维护。

- `activateScene()` 的同 epoch 分支更新 bounds 并保留逻辑目标；新 epoch 分支使用完整 reset。
- 新 epoch 先安装新 bounds，再 reset 到新身份的已确认位置，避免旧节点 bounds 裁剪新身份的初值。
- `enqueueDelta()` 两种 smooth 模式累加逻辑目标，仍只处理合法递增输入序号。
- 私有 `MotionAxis::correctPosition(position, time)` 可用于 `sample()` 的实际位置校正：修正位置并清速度，保留仅受真实内容 bounds 限制的逻辑目标；它与主动取消使用的完整 `reset()` 分开。
- `needsAnimationFrame()` 只描述可以实际绘制的未完成运动，不能仅因逻辑目标在 coverage 外而返回 true。
- `synchronizeBarrier()`、`revokeEligibility()` 和新身份初始化使用完整取消，不能留下随后复活的旧目标。
- `LayerTreeHost::tickCompositorScroll()` 必须在处理 pending ready scene 后重新汇总需求；资源 ready 通知仍是恢复入口。

如实现需要观察“等待 coverage”的状态，可增加内部只读查询供 controller/LTH 回归使用；这不应成为新的 WKE 导出。若需要新增 producer API，则超出上述最小闭环，必须先补充本 ADR 的请求所有权、去重及失败处理。

## 备选方案

- 整条超出临时 coverage 时拒绝并交给 Blink：修改较小，但当前滚轮回退没有先同步未完成目标；普通 Blink scroll 可提升 epoch，导致先前已接收的动画尾部丢失，因此不能单独作为完整守恒修复。
- 去掉实际绘制的 coverage 限制：可能展示未完成的 raster 内容，不采用。
- 增大预取或持续请求空帧：掩盖边界，增加内存或 CPU，不能保证距离守恒，不采用。
- 在完整帧等待中直接保留墙钟积分：恢复首帧可能按最大积分步长跳至目标，影响暂停后的插值，不采用。
- 本轮同时修改预取方向、Windows timer 或绘制间隔：与目标丢失正交，保持单独诊断，不纳入此次修复。

## 兼容性、迁移与回退

无新增线程、输入注入、用户步骤或授权条件，不改变最低 Windows 与编译工具链。非平滑输入由“基于当前实际位置重设目标”改为“保留已接收逻辑目标后累加”，属于此缺陷所需的行为修正，须覆盖混合 smooth 序列并在变更说明中明确。

目标可跨多个临时覆盖场景继续，可能延长一条已接收动作的实际完成时间；不据此声称帧率或输入延迟已经改善。新候选须重新构建并绑定来源与散列，不覆盖既有冻结 SDK 或历史验收。回退使用原冻结载荷，保留失败对照；不得替换同版本二进制后复用旧回执。

## 验证与当前未完成项

### 首次边界回归暴露的浮点接纳问题

Native01 隔离来宾回归中，x86 与 x64 的原始早/晚覆盖反例均为 2/2 通过，既有覆盖及浮点回归均为 13/13 通过；新增边界套件均为 17/19，`real-content-bounds` 和 `saturated-target-noop` 真实失败。已静止时 base 仍可能比真实 maximum 小约 `7e-7`，而逻辑 prediction 已等于 maximum。首版接纳使用严格大小和不等比较，因此把这段不可采样的舍入残差误认作仍在途，末端外向输入没有按原边界规则回退。

保留该失败结果与中止的完整内核构建证据；没有修改断言或将失败倒置。后续最小修正仅让接纳的方向、余量和残留目标判断采用既有移动 epsilon，独立静态复核未发现阻断。截至本段记录，修正后的两架构运行和完整内核验证仍待完成；不能以静态复核或编译代替通过记录。

### 已发现的既有覆盖收缩限制

本轮只读复核另外发现，同 epoch 的新 coverage 若排除当前 visual，原有 `activateScene()` 会裁剪 visual 与 MotionAxis position，却保留此前的 `consumedTotal`。随后扩大 coverage，场景激活可能先按旧累计值重放位置，再在下一次 sample 中反向校正。例如 base/ACK 均为 0、累计位置已到 250 时，将覆盖上限从 300 缩到 200，再扩到 400，静态推导会在未采样前重新显示 250，而非保持 200 后继续插值。此行为在目标保留补丁之前已存在，不能将它归为本次新增或声称已经修复。

固定尺寸安装器本轮尚未观察到该收缩触发条件。本次不扩大到场景累计坐标校正或新的 producer 请求设计；也不声明任意 viewport resize、临时覆盖收缩与再次扩大均已验证。反例保留在 `tests/native/scroll_target_retention/target_retention_boundary_tests.cpp`，由默认关闭的 `SOUI_BUILD_SCROLL_CONTRACTION_DIAGNOSTIC` 单独构建 `soui_scroll_target_coverage_contraction_tests`。该诊断保留真实断言与非零失败结果，不使用 `WILL_FAIL` 倒置；应与正常目标保留和其他边界结果分别报告。截至本段记录，诊断尚未实际执行，预期失败仅来自源码推导。

已有外部 RED 只覆盖生产 Controller/Snapshot 的两种扩展时序。仓库回归入口拟维护在 `tests/native/scroll_target_retention/`，保留同一反例 oracle，并增加边界用例。接下来至少验证：

1. 早、晚 coverage 扩展都最终到 350；扩展前所有 visual 在 coverage 内，停住时无动画帧需求，扩展后重新有需求。
2. 重复 `+100` 在不同输入、采样和 ACK 时序下保留相同逻辑终点；覆盖 x/y、正负方向、同 epoch base/ACK 变化、零余量、真实上下边界和内容缩小。
3. parked 状态下再接收 smooth=false 仍累加旧逻辑目标；反向减少未完成目标，以及真实末端已饱和但仍在途时追加输入，均不意外回退。
4. 长时间 coverage 暂停后首次恢复仅重锚时间；后续有限帧继续并最终静止。连续同 epoch 提交不会反复重锚而饿死运动。
5. 未完成资源不能激活；最后一个 sample → producer → resource-ready → 场景激活后下一帧的真实调度闭环，不依靠测试主动反复调用 `sample()` 伪造唤醒。
6. 点击 barrier、新 epoch、节点销毁、明确资格撤销及关闭不会恢复旧目标；输入序号和 consumed/ACK 保持真实含义。
7. 两架构直接编译生产源码的回归、完整内核重编和隔离 GUI 滚轮/滑块/点击/关闭。端点应记录实际 offset；像素 hash 只能辅助判断内容变化，不能作为距离或 presentation FPS。

在上述结果落地前，本 ADR 保持 proposed。现有已知问题与生产发布门槛不因这份提案扩大通过范围。
