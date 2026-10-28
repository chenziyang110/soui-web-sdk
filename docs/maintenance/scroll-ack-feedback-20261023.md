# 合成滚动确认被整数回传误判为外部滚动

核对日期：2026-10-23。**源码候选已冻结并完成独立静态复核。隔离来宾中两种架构各通过 25+2+19+13=59 项；旧方法对照各通过 3/10、退出 1，失败保持。完整 x86 内核已自然构建并获独立 build-only 验收。真实协议页的 8 条已接受请求累计 800，最终 visual 为 799.9999990293004；独立 DOM 夹具的同值 CSS 赋值产生新 epoch，随后 110 个样本保持 799、未回放旧目标。** 此为限定范围的位移与取消验证，不代表整体流畅度、固定 SDK 或发布验收。

入口为 [ADR 0120](../adr/0120-preserve-compositor-scroll-acknowledgements.md)、[原生回归说明](../../tests/native/scroll_ack_feedback/README.md)和[验证摘要](../verification/scroll-ack-feedback-20261023.json)。上一阶段的结果保留在[目标保留记录](scroll-target-retention-20261023.md)，不将其 Controller 局部通过继承为本轮结果。

## 触发条件与源码链

上一阶段真实宿主中，两轮各收到 8 条已接受的滚轮请求，转换后 y 合计为 800，无 fallback 或日志丢弃。但同一节点的 epoch 分别从 1 变为 7、从 1 变为 6；尾部 compositor visual 最后分别为 226、203，consumed 为零。跨 epoch 的这些数值不能相减推算丢失距离，也不证明实际逻辑目标完成。

源码路径揭示了一个可导致该现象的反馈环：

1. `cc/blink/WebLayerImpl.cpp` 接受 compositor 的分数 visual 和累计 consumed，并通知 `WebLayerScrollClient`。
2. `platform/graphics/GraphicsLayer.cpp` 把 visual 加上 area 的 minimum，交给 `ScrollableArea::setScrollPosition(..., CompositorScroll)`。
3. `platform/scroll/ScrollableArea.cpp` 按现有整数策略 floor，派生 area 保存整数位置并安排几何更新。
4. Mapping → ScrollingCoordinator 将这个整数通过普通 `WebLayer::setScrollPositionDouble` 回传；它与此前保存的分数值不同，旧 setter 增加 epoch 并清除 ACK。
5. `ScrollController::activateScene` 将新 epoch 作为真正的外部取消，重置尚未完成的目标。

这些是源码定位与旧运行证据的对应关系。后续 GUI21/22 证明下述默认整数元素滚动样本已不再因普通 ACK 反复增加 epoch，并保留同值 CSS 的取消语义；不推广到未测配置。

## 候选修复与维护位置

`cc/input/CompositorScrollAcknowledgement.h` 是主线程 ACK 状态的唯一实现。原始 visual 为 V、累计 consumed 为 T、Blink 规范化位置为 C，发布 base `C` 和 ACK `T+(C−V)`，保持 `base−ACK=V−T`。另外保存原始 receipt，供相同 sample 的 barrier 幂等核对；有限数、溢出、epoch/sample 边界在写状态前检查。零 sample 的 forward barrier 保留原有独立证明，不伪造 receipt。

`WebLayerScrollClient` 增加默认 identity 的同步规范化查询；`GraphicsLayer` 复用现有 minimum/clamp/floor 策略。禁止在查询内执行脚本、客户回调或泵消息。最大位置 getter 可以懒更新尺寸缓存，不能把查询写成严格无副作用；当前几何发布路径会先稳定这些缓存。相同 canonical 像素内的新 sample 仍会请求 commit。

真正外部意图通过 `WebLayer::invalidateCompositorScroll` 明确撤销。来源入口包括 base `setScrollPosition`、非零且轴可滚动的 `userScroll`、非 Compositor 的动画/internals 回写，以及 `FrameView` 相同值早退和 `DeprecatedPaintLayerScrollableArea::scrollToOffset` 的相等分支。后者是 CSS `scrollTop=current`/`scrollLeft=current` 不经过 base helper 的路径。撤销仍在下一 ready scene 激活时由新 epoch 生效，没有新增即时跨线程取消机制。

快照保存 `canonicalAdjustment=C−V`，admission 使用 `base−canonicalAdjustment` 判断最近真实 visual。否则实际已停在 `max−epsilon`，floor 后 base 却可能小近一个像素，会再次误认为仍有未完成运动。移动容差仍为原来的 `1e-6`，真实内容边界及已准备 coverage 不扩大。

新增 virtual 的默认实现只保持源码兼容，不能与旧二进制 C++ client 混装；完整内核和相关 client 必须一起重编。WKE 导出 ABI、业务、计时器参数、页面样式和预编译发布包没有因此变更。

## 构建阶段与测试边界

本阶段冻结回执绑定 17 个核心文件，包含本轮修改及沿用的依赖；整个内核源码为 23,546 个文件。静态来源表单独记录 14 个生产修改文件及 ADR。完整文件清单核验、内核 DLL/链接 map/依赖与实际运行必须使用本轮后续回执，不能沿用上一阶段 DLL 的通过状态。

原生回归直接编译生产 ACK helper、Controller 和 Snapshot，测试源码直接调用公开 client 的默认 identity 查询。旧版没有 helper，基线采用机械提取的两个旧 `WebLayerImpl` 方法体，加可控 callback/commit 替身；不能称作完整 Blink 回归。

| 编译目标 | 用例定义 | 编译 | 实际运行 |
| --- | ---: | --- | --- |
| 旧方法对照 x86 / x64 | 各 10 项共同核心 oracle | 均成功 | 各 3/10，退出 1，真实失败 |
| 新 helper / Controller x86 / x64 | 各 25 项 | 均成功 | 各 25/25，退出 0 |
| 既有目标 / 边界 / 覆盖 x86 / x64 | 各 2 / 19 / 13 项 | 均成功 | 六个程序全部通过并退出 0 |

八个 configure/build 步骤均退出 0，构建前后的输入散列一致，四个 EXE 的当前字节与回执一致。使用 MSVC v143、Windows SDK 10.0.19041.0、Release 静态 CRT；这些是独立测试程序的编译记录，不是完整内核 LLVM 构建记录。

首份编译完成后，独立复核发现两个 invalid-snapshot 用例使用旧 sceneVersion，可能因旧场景 fence 提前拒绝而误满足断言。随后将场景改为更新版本，并直接断言 `!isValid()`；生产代码不变。首份编译证据保持为 compiled-only，没有被用于本次运行结论。第二次独立输出完成加强后 4 个 ACK 程序的编译，输入前后散列一致；既有 2/19/13 回归也在同一冻结生产源码下重编为 6 个程序。十个新 EXE 的字节、散列及构建输出均已重新核对，随后在 Windows 10 19045 的隔离来宾执行。

本次完整 suite 退出 1，`allProgramsPassed=false`，原因是两个旧基线各有 7 项真实失败；`currentProgramsPassed=true` 仅指当前八个程序。没有把非零退出倒置为通过。20 份 stdout/stderr 的字节数和 SHA-256 均与来宾传出的记录匹配，源码绑定重新核对了冻结的 17 个生产文件、加强后的夹具以及十个 EXE。恢复验证摘要 SHA-256 为 `5664B5494E2C6DC55F1F6E82B52A379F621B964B75286B1A9ACB4299BBAF8D1E`，源码绑定摘要为 `065B06EFCDEF849C9D63A26F699B831B7473C7F84AFA18242CDE7C601D0D3AEA`；原始外部证据保持原字节，公开摘要不包含机器路径。

新增用例覆盖原始 receipt、零 sample、非有限/溢出拒绝、外部失效、真实 Controller 积分得到的末端尾差、连续 ACK、反向及临时覆盖扩展。受控 canonical 值不会执行真实 GraphicsLayer、CSS、Home 或滚动条入口，不能用 helper 通过替代 DOM 验证。既有 2 项目标 oracle、19 项边界和 13 项覆盖回归已在本轮源码上重编并分别执行通过；旧测试文件与历史结果保持不动。

完整 x86 源码构建自然退出 0，耗时 1,134,601 ms（约 18.9 分钟），没有终止 owned job，最终 closureConfirmed=true、activeProcesses=0。独立 build-only 验收绑定 17 个冻结文件、23,546 个源码文件、40 个编译输入 pin、11 个依赖库以及 DLL/map；源码 before/after 回执原字节一致。node.dll 为 28,119,552 字节，PE machine=0x014c 且含 DLL 标志，SHA-256 `F919E3BF6FAC50B8FBA03F0C184F737E4539A90B6936B73DAEFC68F054657BD7`。验收摘要 SHA-256 为 `3249F487EBF068FE479897ECBCF8E7FCD9E8AFE9ABAADB8D658D4417B7E1E841`。这只是本轮完整 x86 构建及来源结论：whole-job/compiler-execution attestation 仍为空、toolchainQualified=false，不能改写为完整工具链资格或发布验收。

## 实际 GUI21：协议页位移守恒

完整 x86 候选使用同一诊断插件，关闭强制平滑和计时器精度实验。准备阶段对同一协议滚动目标执行 End，实际观察到 visual=4130，随后 Home 回到 0；4130 仅是当时观察到的可达距离下界，不是真实内容最大值，也不跨 setup epoch 相减 consumed。

测量段包含 8 条真实 native wheel，raw 合计 -960，转换后的已接受请求合计 +800，无 fallback、丢弃或段内外部输入；输入跨度 646.6866 ms。26 个 actual 样本全部有效，同一节点/generation/epoch，visual 从 0 到 799.9999990293004，与 800 相差 9.706996e-7，小于预定的 1e-5 容差。尾部 15 个样本的静止观察跨度为 1749.3108 ms。该样本中的 consumed 数值变化也相同，但 consumed 是累计 ACK 坐标，不能把“必须等于请求总量”推广到跨 epoch 或不同 canonical 原点的记录。

第二组输入另验 Home：按键到达旧 epoch 的运动期间，邻近样本仍为 682.0869667452799，随后新 epoch 回到 0 并保持；这组有明确取消，不计入 800 守恒统计。短 thumb 拖动实际包含 5 条按住移动、位移 13 像素，后续 actual 变化，返回及关闭可用。100 ms 探针不能据此判断拖动帧率或流畅度。

## 实际 GUI22：同值 CSS 取消

独立外部 DOM 夹具使用同一元素，Normal/Arm 两种模式；没有把夹具注入产品源码。Normal 的 8 条已接受请求累计 +800，单一 epoch 最终 visual=799.9999994464031。Arm 也接收 8 条请求累计 +800，输入跨度 628.0206 ms；夹具在首次正向 scroll 后延迟至少 900 ms，执行一次真实 `scrollTop = scrollTop`。可见遥测记录 before=assigned=after=799、整数值、resetCount=1、实测延迟 907 ms。

QPC 轨迹中，最后两个旧 epoch 样本分别为 797.7739928819221 和 799.9540818365107；随后 epoch 6→7，visual=799、consumed=0。新 epoch 的 110 个有效样本在 13626.626 ms 的起点至起点跨度内均保持 799，旧目标没有回放。最后一条 wheel 到下一条原生输入间隔 14146.3581 ms，无后续 wheel 干扰。跨 epoch 的 consumed 不相减，通用分析器也明确拒绝将该段端点用于守恒比较。

DOM 时钟记录首次 scroll 为 12:59:03.283 UTC、赋值为 12:59:04.190 UTC；没有同步的 UTC→QPC 变换，因此不声称探针在 setter 的精确时刻直接测到了 target/velocity。最小 900 ms 延迟与 628.0206 ms 输入跨度足以说明 setter 晚于最后 wheel；邻近旧 epoch 样本尚差 0.045918 像素，加上源码及输入历史支持在途取消。结论限定为真实同值 DOM 赋值、新 epoch 与后续停止，不扩大到所有外部来源。

GUI21/22 原始记录均无丢弃，并与来宾导出散列核对；独立复核重新计算了主位移及 CSS 停止区间。完整散列和数值见[验证摘要](../verification/scroll-ack-feedback-20261023.json)。原始产品截图和日志保留在外部，公开仓库只保存脱敏结论。

## 继续验证的范围

先保持计时器精度、smooth 策略、插件和页面相同，只更换已绑定来源的新内核。使用真实 native 输入记录确认条数、方向、间隔、accepted/fallback 和转换后的总量；仅在日志实际证明 8 条已接受请求合计 800、内容仍有足够空间时，才以 800 为预期位移。100 ms 的只读 actual 探针记录 visual/consumed、身份、epoch、sample 和采样耗时；连续性破坏、invalid 或丢日志必须明确标记，不用终点截图或采样静止替代守恒。

继续扩展其他 DOM 来源、viewport 和拖动负载时，应保留同样的输入、身份与取消证据。必要时另加 setter 同时刻的只读观察，不能把本次低频采样写成瞬时 pending 证明。独立夹具结果与真实安装页面结果分开，不将测试 HTML 放入产品。

采样 hash 只证明采样位置的内容变化，actual visual 是 compositor 状态，不等于 DOM offset 或真正 presentation FPS。计时器 0/1ms 的节奏实验单独记录，不进入本修复的默认行为。

## 已知限制

- 当前修复与验收目标限定默认 `fractionalScrollOffsetsEnabled=false` 的元素滚动。非默认 fractional 模式配合全页 non-layer fixed-object reason 时还有另一条整数回传策略，本轮不声明覆盖。
- viewport 的独立路由、任意 resize、同 epoch coverage 收缩及再扩大不是本轮完成项；既有收缩 RED 保持。
- 来源查询的缓存前提和外部撤销在 ready scene 生效的窗口保持原有生命周期约束。
- 真实内核运行与 producer 完成已有 GUI21 的限定样本，同值 CSS 取消已有 GUI22 的限定样本；整体流畅度、所有 DOM 来源、完整 x64 内核与发布资格没有通过结论。
