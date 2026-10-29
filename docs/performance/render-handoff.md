# MiniBlink 渲染实施交接

基线：`c17c79bdf470fae681c63324ceaa2a519151afc9`（当前工作树隔离快照）。本文件描述源码候选，未运行构建、测试或 VM，未证明二进制大小或性能收益。

## 已实现

- interaction ABI v2 表布局不变，追加 `PRECISE_FRAME_RATE=0x8`、`ON_DEMAND_UI_MIRROR=0x10` capability；仅新内核声明。`configurePresentation` 的 `UI_MIRROR_ON_DEMAND=0x2` 必须同时带 DIRECT_HWND；旧 DLL 保持原 fallback。
- on-demand mirror capability 仅 CPU 编译档声明；GPU 编译档同时拒绝该配置，不以旧 CPU canvas 冒充 GPU DC 快照。首帧和同尺寸 alpha 切换也重建正确 mirror。
- `drawFrameRate` 接受整数 30–1000Hz，用分数秒配置；record/composite/legacy UI paint 使用单调 deadline 与迟帧跳过。旧毫秒设置保持兼容。
- DIRECT_HWND 按消费者需求省掉每帧 UI mirror；`wkeGetViewDC` 首次/后续读取在 compositor 锁保护下复制当前完整画面。旧 embedder 默认路径不变。
- legacy 未到 deadline 时保留唯一 coalescing slot 延后，不立即 ready 重投。初始/延迟/follow-up 使用实际 owning WebThread::Task；新 payload 接手 slot 后旧 payload 不重复释放，正常结束归还 count。拒绝/取消析构归还 slot 并锁存 notification-failed，不会留下永久 count!=0。
- legacy shared lifetime gate 在 Host close 取得 compositor 锁前撤销地址，任务提交在两把锁外；跨线程取消只退 admission，无 GDI/用户回调/重试。owner run 的 UI 回调返回后重新确认 host 未被重入 close 撤销。gate 明确使用 WTF::RecursiveMutex，允许同一 owner 回调重入 close。
- 现有 paint callback 仍借用 Host/canvas，不能在 callback 内直接同步 delete；要等 callback/EndPlatformPaint 等借用返回后再销毁，SDK 异步关闭可延后。gate 不提供任意同步 reentrant delete 的安全保证。
- 主线程资源等待注册后重查；raster pool 注册去重、完成通知与同步注销；一个 owner 资源通知在 Host close 时失效。全局 raster 阈值、scene/action 背压保留。资源等待不再以 commit 定时轮询。
- 每个 action append 请求合并 draw，action 应用/scene 激活通知主线程，去掉 incomplete draw 的 8ms 常规重试。
- `endRecordActions` 封 endId 后也请求 draw，覆盖最后 append 早于 seal 被消费的竞态；frame 数查询使用 action mutex。
- 资源 owner 通知使用 owning Task；未运行取消/拒绝析构清 coalescing slot。注册 OOM/通知失败锁存 `notification-failed`；`RESOURCE_WAKE_STATUS=0x20` 能力下，既有 `wkeGetDebugConfig(view,"resourceWakeStatus")` 返回 DLL 静态字符串 idle/waiting/notification-failed。宿主须明确诊断/报错，最后一次通知在 OOM 下不承诺自动恢复，不能把该状态写为正常等待或通过。
- 区域数达到 8 时，将 incoming 第 9 个区域加入最小面积成本选择，减少原策略可能产生的大包围盒；保持覆盖和 8 区域上限。
- `m_drawMinInterval`、主帧 deadline 及跨线程 raster pending 计数明确原子访问；commit outstanding 接纳使用 CAS，避免资源线程并发重复排队。

## 宿主接入

父任务唯一维护 `sdk/src/host/soui_miniblink_host.cpp`：先检查上述 capability，再传 `drawFrameRate`，并在成功配置 DIRECT_HWND 时选择按需 mirror。未知旧内核不调用新行为。相关公共动态绑定头和 vendor 定义已对齐。

## 回归来源与后续命令

`tests/native/miniblink_frame_policy_tests.cpp` 直接使用生产的 FrameSchedulingPolicy、DirtyRegionPolicy、CompletionWaiters，覆盖分数帧率相位、迟帧跳过、早唤醒、低于毫秒延后、新区域最小成本选择、重复注册及注销等待正在借用的通知。父任务注册 `sdk.native.miniblink-frame-policy`（20 秒超时）。专人构建后执行该 CTest，并补真实直接/legacy、DC 快照、透明、resize、DPI、IME/滚动、导航/关闭和 DevTools 暂停恢复。

CompletionWaiters 还用拒绝分配器覆盖注册分配失败。owning Task 的未运行销毁、最后通知拒绝的 sticky 失败诊断、scene 注册竞态与 seal 后无后续 producer 的真实内核路径仍需专人故障注入/集成验证，当前未执行。

legacy owning payload 的真实故障注入同样未执行：分别在初始/延迟/follow-up 触发 willExit、TaskPair/heap 分配失败，检查计数归还和失败诊断；pending task 与 close 竞争、UI callback 重入 close 时检查无旧 Host 访问和无取消线程 GDI。未添加复制该实现的 mock 来代替真实 vendor 生命周期验证。

供应链补丁：`third_party/patches/miniblink/0016-frame-presentation-and-resource-wakes.patch`。当前字节/身份映射：`third_party/manifests/miniblink-frame-presentation-20261029.json`。源码改动后必须刷新映射，不能把旧 DLL 当新候选。

## 不作未经证明的改变

默认 worker 数、GPU 默认值和任意新增 buffers 未改变。同步 DC/bitmap 用户 callback 依赖像素有效期，保持锁契约；当前省镜像已减少锁内工作，不宣称所有回调已移到锁外。无 profile 不添加会多画区域的面积阈值、不重写 raster 调度，也不把它们写成已验证收益。后续按统一 profile 决定条件项，大小以共同门禁实测。
