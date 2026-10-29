# ADR 0138：MiniBlink 精确帧节拍与按需 UI 镜像

状态：本地实现候选，待集中编译、视觉与生命周期验证。日期：2026-10-29。

## 背景

直接 HWND 呈现仍维护默认用于 `wkeGetViewDC` 的第二份 UI canvas。CPU 绘制间隔又经整数毫秒配置，连续帧以实际唤醒时刻为相位，迟到会累计节拍漂移；兼容 UI paint 节流在未到 deadline 时立即重投。

## 决定

- 现有 interaction API v2 增加 capability 位，结构与函数表不变。支持精确 Hz 配置和直接呈现下按需 UI mirror 的内核才公布对应位；旧 DLL 保持原路径。
- `drawFrameRate` 仅接受 30–1000 的整数 Hz，内核使用 `1.0 / rate`。合成与 legacy UI paint 使用单调 deadline、分数相位和迟帧跳过；不忙等、不补跑积压帧。
- `configurePresentation` 的 on-demand mirror 标志必须与 DIRECT_HWND 同时启用。合成回调不持续复制镜像；`wkeGetViewDC` 在 compositor 锁保护下取得当前完整快照。旧 embedder 默认持续镜像，透明/resize 仍走完整正确复制。
- GPU 编译档不宣告且拒绝 on-demand mirror，避免把不含持续 readback 的 CPU canvas 当成最新 GPU 画面；此能力首先限定软件档。
- legacy UI paint 的唯一 coalescing slot 保留到最早 deadline，不立即 ready 重投。初始、延迟及 follow-up 都使用真正 owning `WebThread::Task`；转交给新 payload 后旧 payload 不再拥有 slot，避免同一个 Task 被两次拥有。拒绝/取消会退回计数并锁存 `notification-failed`，不能永久占位冻结后续绘制。
- legacy payload 以独立共享 lifetime gate 借用 Host；close 在取得 compositor 锁前撤销 host 指针，迟到 payload 只释放自身。取消析构可以发生在 producer 线程，只退 admission，不触发 GDI、用户回调或重试。正常 run 位于 owner 线程，gate 明确使用 WTF::RecursiveMutex；跨 UI paint 回调返回后复核 gate，避免重入 close 后继续解引用 Host。实际原生回调重入仍需专人验证。
- lifetime gate 不改变现有 paint callback 的借用契约：callback 内不能同步直接删除仍被 Locker、EndPlatformPaint 等借用的 Host/canvas，须待 callback 返回后销毁；SDK 异步 close 可延后。此补丁不宣称任意同步 reentrant delete 安全。
- record 受资源阻塞时先注册、再重查；全局 raster 计数完成、action 应用和 scene 激活发出合并的 owner 通知。Host 注销等待所有正在借用其地址的 pool 通知后才销毁，owner 通知在 close 时失效。正常资源等待不再重复提交 commit。
- 通知由实际 owning Task 承载，拒绝/取消时析构清空 coalescing slot。注册 OOM、通知分配失败和未运行拒绝均锁存 `notification-failed`；声明 `RESOURCE_WAKE_STATUS` 能力的内核通过既有 `wkeGetDebugConfig(view,"resourceWakeStatus")` 提供 idle/waiting/notification-failed，宿主必须报告失败，不能将它描述为正常等待。最后一次通知在 OOM 下不承诺自动恢复，后续 producer 可重投；这是明确异常终态，不是性能通过。
- 每个 action producer 的 append 均请求合并 draw，覆盖 incomplete frame 的后续唤醒，从而移除 8ms 正常重试。pacing 受阻仍按真实 deadline 定时。
- 8 区域上限下，将新区域也纳入最小合并面积代价选择，不先合并遥远旧区域。保留已有像素清除、透明和层次语义，不引入推测阈值。
- 保留 DC callback 的锁内同步契约；按需镜像减少锁内复制，不能为缩锁裸解锁仍被使用的 canvas。raster 并发、新缓冲及外部 callback 异步化需另有测量与所有权证明。

## 备选与兼容

全局环境变量无法表达每 view 消费者需求，且可能令 DC 读取过期，保留为诊断行为。新增独立 API/export 会增加部署复杂度，改用现有 versioned capability。未声明新能力的内核不启用新行为。

## 验证与回退

源码回归覆盖分数 cadence、迟到跳过、早唤醒、非法 rate、旧能力回退及 lazy DC 首次读取。legacy owning payload 需补真实内核故障注入：初始/延迟/follow-up 的队列关闭、TaskPair/heap 分配失败，以及 pending payload 与 close 竞态；确认计数不悬挂、sticky 错误可见、取消线程无 GDI/回调。当前仅完成静态所有权核查，没有用复制实现的 mock 代替这些未执行项目。专人验证直接/legacy、透明、截图、resize、DPI、导航/关闭、暂停恢复。比较 Release 包和必要运行文件字节，超基线不采用。所有当前未执行项目保持未验证；关闭新 capability 使用原兼容配置。
