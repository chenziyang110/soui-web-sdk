# MiniBlink 渲染主线集成交接

集成底稿：`0cdc0a9ace0cdce664ec4a9b54e5f8f71b97ba48`。状态：源码候选；本轮不编译、不执行测试、VM 或性能采样，没有帧率或体积通过结论。

## 主线保留与新增范围

主线 ADR-0124 的统一分数 deadline、真实 action/scene 通知、双 framebuffer lease、输入 trace、paint activity 关闭门、sticky DC consumer 和 owning legacy retry 均保留。冻结候选中针对旧底稿的 raw canvas、legacy lifetime gate 及重复调度实现已由这些主线能力覆盖，不重新引入。

新增 interaction ABI v2 capability `PRECISE_FRAME_RATE=0x8`、软件档 `ON_DEMAND_UI_MIRROR=0x10`、`RESOURCE_WAKE_STATUS=0x20`，表布局保持。input-ui ABI 的同值位属于另一张能力表。`drawFrameRate` 支持整数 30–1000Hz 的精确分数配置；已采用主线 input-ui fractional-frame-policy 的宿主继续使用统一主帧节拍。按需 mirror 标志要求 DIRECT_HWND，复用主线显式 consumer；DC 请求永久恢复消费者，透明/非独立呈现保持镜像，GPU 档拒绝该标志。

record 的阈值使用全局 raster pending 数，自己的 view 可能没有任何 raster task。因此资源等待额外登记 pool membership，跨 view pending 从 11 降到 10 时发布合并 owner 通知；不是每个 tile 都锁 membership。登记后沿用主线重查，清除等待及析构同步注销并等待正在借用 Host 的通知返回。回调只投递共享取消状态的 owner task，不执行页面/客户端回调。

`endRecordActions` 封闭 endId 后补合并 draw 和已有资源等待通知，覆盖最后 append 在 seal 前被消费的情况。正常资源等待继续由生产者通知，不新增周期 draw/commit。资源任务保留主线一次 100ms 异常重投，最终重投拒绝或 OOM 锁存 `notification-failed`；legacy 任务拒绝/未运行取消也锁存异常。`wkeGetDebugConfig(view,"resourceWakeStatus")` 返回静态字符串 idle/waiting/notification-failed，SDK 做低频健康诊断，关闭仍遵循主线实际 drain。

8 区域满额的 dirty 合并把 incoming 第 9 个区域加入最小面积成本选择，保留覆盖和区域上限。mirror 分配失败安全返回，同尺寸 alpha 变化重建正确格式；像素读取继续持有 framebuffer lease。

## 专人验证

`tests/native/miniblink_frame_policy_tests.cpp` 直接使用主线生产 `FrameDeadlinePolicy`、`LegacyPaintRetryPolicy`、新 `DirtyRegionPolicy` 和 `CompletionWaiters`，覆盖分数相位、迟到跳过、早唤醒、亚毫秒延后、incoming 选择、重复登记、拒绝分配以及注销等待正在借用的通知。CTest 名为 `sdk.native.miniblink-frame-policy`；本轮未执行。

真实内核还需验证跨 view 全局 raster 阈值等待、注册/重查竞态、seal 后无后续 producer、资源与 legacy 队列拒绝/分配失败、停止期间取消、DC 首次及后续读取、alpha/透明、resize/DPI、滚动/IME、导航、DevTools 暂停与关闭。不得使用旧 DLL 或旧 EXE 的结果证明当前源码。

来源增量由 `third_party/patches/miniblink/0024-main-integrated-frame-presentation.patch` 和对应 `third_party/manifests/miniblink-frame-presentation-20261029.json` 记录。默认 worker、GPU 和画布槽上限沿用主线。所有运行与尺寸预算仍待专人验证；未测量条件下不声称已对齐 Electron。
