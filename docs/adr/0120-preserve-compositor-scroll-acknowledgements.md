# ADR 0120：区分合成滚动确认与外部滚动意图

- 状态：Proposed（源码候选；限定 GUI 位移与同值 CSS 取消已验证，完整配置与性能资格未完成）
- 日期：2026-10-23
- 关联：[ADR 0119](0119-preserve-scroll-target-beyond-raster-coverage.md)

## 触发证据

固定协议页的两轮独立诊断均收到 8 条已接受的滚轮输入，合计请求位移 800；没有 fallback 或日志丢失。即使保留了临时栅格边界之外的逻辑目标，同一 node/generation 的 epoch 仍在普通滚动中多次改变，最终观察到的 compositor visual 分别停在 226 和 203，停止输入后超过两秒仍未继续。跨 epoch 不能计算累计守恒；这些数值不是 DOM offset，也不证明帧率或显示流畅。

源码链为 `WebLayerImpl::setScrollPositionFromCompositor` 写入分数位置，`GraphicsLayer::didScroll` 进入 `ScrollableArea` 的 clamp/floor，随后 Mapping → ScrollingCoordinator 的普通 `setScrollPositionDouble` 回传整数位置。该普通 setter 将不同值解释为外部滚动，增加 epoch 并清除 ACK；下一次 controller activation 因而撤销尚未完成的目标。

单纯忽略小于一像素的差异不成立。真实 CSS、Home 或拖动可能将位置设为相同的整数值，并且 `FrameView`、派生 area、ScrollAnimator 存在早退。必须在来源已知的入口撤销旧目标，不能靠数值差异或 `didScroll` 栈上的临时布尔量识别来源。

## 候选决策

1. `WebLayerScrollClient` 增加同步规范化查询，默认 identity，禁止脚本、客户回调和消息泵。`GraphicsLayer` 按真实路径使用 `minimumScrollPosition + V`、area clamp、实际 `shouldUseIntegerScrollOffset` 的 floor，最后减去 `minimumScrollPositionDouble`。不打开全局 fractional feature，不改现有取整策略。
2. 设原始 compositor visual 为 V、累计 consumed 为 T、Blink 规范化位置为 C。发布 base `B=C` 和 ACK `A=T+(C−V)`，因此 `B−A=V−T`，保留 controller 的累计坐标、逻辑目标和真实边界。按分组加法计算，所有输入、中间差及结果须有限；失败时不修改状态。客户端缺省查询可使用 identity；专门查询失败不能偷偷退回未规范化位置。
3. 用独立生产 helper 保存 epoch、sample、规范化后的 ACK，以及原始 `(epoch,sample,T,V)` receipt。相同 sample 的 barrier 幂等检查针对原始 receipt，沿用原有 0.01 容差；不能拿调整后的 ACK 与原始 barrier 比较。尚无 sample 的 forward barrier 保留原有独立证明：双方 sample 均为零、同 epoch、有限的零 total 与当前 base 按原 0.01 容差匹配；它不伪造 raw receipt。旧 sample/错 epoch/非有限值不写状态。普通外部位置变更、显式撤销及 client 更换清除 receipt 和调整量。
4. 增加明确的 `WebLayer::invalidateCompositorScroll` 默认空实现，由当前 `WebLayerImpl` 增加 epoch、清 ACK/receipt 并请求 commit。`ScrollableArea::setScrollPosition` 在非 Compositor helper 前调用；`userScroll` 在确认输入轴可滚动后调用（零 delta 不取消）；`scrollPositionChanged` 在非 Compositor 动画或 internals 回写前调用。`FrameView` 仅在绕过 base 的相同位置早退分支补调用；CSS `scrollTop`/`scrollLeft` 所经 `DeprecatedPaintLayerScrollableArea::scrollToOffset` 的相等分支也补调用。多个明确入口相遇时允许重复增加 epoch，避免用可漏掉新输入的全局去重状态。Compositor 回写不走撤销路径，外部相同值仍会撤销。
5. snapshot 另存默认零的 `canonicalAdjustment=C−V`。input admission 使用 `base−canonicalAdjustment` 作为最近确认的真实 visual，避免已停在 `max−epsilon` 的位置经 floor 后被误判为尚差近一像素。仍使用现有 1e-6 移动阈值，coverage/resource fence 不放宽，逻辑预测只按真实内容边界限制。快照校验调整量及相减结果有限。
6. 同一 C 内仍需发布新的 sample/ACK，不能以 `positionChanged=false` 跳过 commit。规范化查询的前后 epoch/sample/client 必须一致；查询不执行脚本或泵消息。area 的最大位置 getter 可懒更新尺寸缓存，因此不声称严格无副作用；当前已发布场景由 Coordinator 绑定后读取 `contentsSize()` 清理初始 dirty，`updateAfterLayout` 的 dirty 也立即同步重算。`didScroll` 可能引发重入，调用后不访问可被销毁的 client 或当前对象；外部入口发生的撤销不得被旧 ACK 覆盖。外部撤销沿用原有新 epoch 在下一 ready scene 激活时生效的契约，不额外引入即时跨线程撤销。主线程任务及现有场景发布顺序保持，不增加线程、timer、轮询或忙等待。

## 实现与兼容边界

修改范围限于 WebLayer/ScrollClient 接口、GraphicsLayer 的规范化、ScrollableArea/FrameView/DeprecatedPaintLayerScrollableArea 的来源入口、WebLayerImpl ACK 状态、LayerTreeHost 快照/幂等检查和 ScrollNodeSnapshot admission。独立生产 helper 为原生回归提供真实调用边界；测试不能复制一套数学后声称验证了生产链。

新增 virtual 默认实现只保留源码兼容，**不是二进制 ABI 兼容**。完整内核及相关 C++ client 需要重编；不改 WKE 导出 ABI，不向已有冻结包替换零散对象或 DLL。历史候选和既有 ADR 0119 回归保持原字节。本变更不改变计时器精度、页面样式、业务流程或发布默认值。

## 必须验证

- helper：identity/floor、负数与非零 origin、clamp、非有限和溢出拒绝、epoch/sample fence、raw duplicate 接受及冲突拒绝、client 更换失效。
- 真实 Controller：分数 sample → canonical ACK → 同 epoch activation 后保留逻辑目标；反向及临时 coverage 扩展；积分停止于 `max−epsilon` 后的外向输入正确拒绝。保留资源限制与真实内容边界。
- 来源链：Home 的 animator 早退、FrameView 相同位置早退、CSS `scrollTop=current`、拖动及外部动画都能撤销，普通 Compositor ACK 不撤销。helper 测试不能替代这些 Blink/DOM 链路的实际 GUI 验证。
- 完整内核、旧原生回归与新增回归；同输入的 GUI actual visual/epoch/accepted 日志确认目标最终守恒，再验返回、关闭与取消。计时器 0/1ms 的画面采样节奏另报，不用其代替守恒结论。

当前核验范围为默认 `fractionalScrollOffsetsEnabled=false` 的元素滚动；`fractional=true` 同时存在全页 non-layer fixed-object reason 时，Coordinator 另有强制整数回传路径，未在本轮实现或验收。viewport 的既有独立路由同样不推广为已覆盖。

现阶段仍是提案与候选修复。隔离来宾两架构各 25 项 ACK 加 2/19/13 项既有回归通过，旧方法对照各 3/10、退出 1；完整 x86 内核自然构建并获独立 build-only 验收。GUI21 协议页 8 条已接受请求累计 800，单一 epoch 的最终 visual=799.9999990293004；GUI22 同值 CSS 赋值产生新 epoch，随后 110 个有效样本保持 799。低频采样未直接测得 setter 瞬时 target/velocity，取消结论限定为真实赋值及后续不回放。证据与限制见[维护记录](../maintenance/scroll-ack-feedback-20261023.md)。完整 x64 内核、整体流畅度与发布资格仍未完成；coverage 收缩的已知独立问题、输入工具实际节拍、取样与真正呈现的区别继续保留，不将本 ADR 写成全场景通过。
