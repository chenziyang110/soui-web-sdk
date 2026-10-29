# ADR 0138：主线帧呈现的精确配置与资源唤醒补全

状态：源码集成候选，编译、运行、性能与体积验证交由专人。日期：2026-10-29。

## 背景

本次以 `0cdc0a9ace0cdce664ec4a9b54e5f8f71b97ba48` 主线为底稿。ADR-0124 已实现分数 deadline、统一主帧节拍、真实资源通知、双画布 lease、paint activity 关闭门和 owning legacy retry。本决定保留这些实现；冻结候选中的旧 raw canvas/lifetime 方案不覆盖主线。

## 决定

- interaction API v2 表布局不变，新增 `PRECISE_FRAME_RATE=0x8`、软件档 `ON_DEMAND_UI_MIRROR=0x10`、`RESOURCE_WAKE_STATUS=0x20`。这些属于 interaction 表，与既有 input-ui 表的同值 capability 无关。
- `drawFrameRate` 接受 30–1000 的整数 Hz，以 `1.0 / rate` 配置主线已有分数 deadline。已采用 input-ui fractional-frame-policy 的宿主继续使用该统一主帧路径；新增配置用于兼容能力回退，不叠加节拍。
- `UI_MIRROR_ON_DEMAND=0x2` 必须带 DIRECT_HWND，复用主线的 `setUiMirrorConsumerRequired`。首次 DC 请求使用持有像素所有权的 framebuffer lease 建立当前快照，并永久登记 DC consumer；透明窗口与非独立呈现始终需要镜像。GPU 档不声明且拒绝此标志。
- 资源等待保留主线共享取消状态、注册后重查、action/scene fence 与 owner 通知。额外登记全局 raster pool 完成等待，覆盖被其他 view 的全局 pending 阈值阻塞时自己的 view 没有 raster 完成事件的情况。登记去重；清除等待及析构先同步注销，等待正在借用 Host 的通知返回。pool 通知仅投递 owner 工作，不执行页面或用户回调。
- `endRecordActions` 封闭 action 区间后补合并 draw 和已有等待通知，覆盖最后 producer 早于 seal 被消费的情况，不恢复正常资源轮询。
- 复用共享取消状态记录通知异常。资源任务允许既有一次 100ms 异常重投；最终重投拒绝或通知分配失败锁存 `notification-failed`。legacy owning task 未运行取消、分配失败也保留 damage 并锁存异常。新 capability 下既有 `wkeGetDebugConfig(view,"resourceWakeStatus")` 返回静态字符串 idle/waiting/notification-failed；SDK 明确报告异常。原有 owner 机会重试和关闭 drain 继续存在。
- 8 个 dirty rect 满额时同时考虑新加入的第 9 个区域，以最小增加面积选择合并；不先合并遥远旧区域。DC mirror 的 canvas 分配失败安全返回，同尺寸 alpha 变化重建正确格式。

## 兼容、迁移与备选

不新增导出函数，不修改两张 ABI 表布局，不改变 action/scene/scroll ACK、输入 trace、paint lifetime、两画布槽上限或默认 worker 数。旧 DLL 不声明新能力，宿主保留现有配置。没有 profile 不启用新增 raster 并发、额外 buffers 或推测性布局缓存。回退必须使用匹配的 SDK 和内核源码/二进制身份。

## 验证交接

源码回归直接调用主线生产 deadline/retry、dirty merge 和 completion membership helper，覆盖分数节拍、迟到跳过、早唤醒、新区域选择、重复注册、OOM 与同步注销。专人需编译两架构并执行 native CTest，以及跨 view 全局 raster 等待、seal 无后续 producer、队列拒绝、legacy 取消、DC/alpha/透明/resize/DPI、暂停及 close 回归。本轮只做静态审查，不宣称已运行、提升帧率、通过体积预算或关闭生产资格。
