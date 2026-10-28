# 输入到可见反馈：渲染线源码交接

日期：2026-10-26。基线 `44eff565bf9ab7778a8b9fdaa5403cc3b6f568ed`，继承未合并的 Draft PR110/109。本文记录源码和静态审查范围，**没有编译、测试执行、VM、benchmark 或 profile 结果**；生产资格不变。

## 已实现源码与验证入口

| 工作包 | 生产行为 | 回归源码 | 当前边界 |
|---|---|---|---|
| W00 | 一个持有取消状态的 legacy deadline Task；dirty 合并；早唤醒按真实剩余时间重排；拒绝保留 dirty；resize/destroy 失效票号；序号耗尽 fail closed | `tests/native/render_deadline_policy_tests.cpp` | 默认 DIRECT_HWND/independent 路由未因 legacy 修复改变；静态推导不是已复现默认卡顿 |
| W05 | SDK QPC 商/余数相位；内核分数秒相位/迟帧直接跳过；record 仅资源准入；同帧 deadline 与身份贯穿阶段；隐藏/resize 重建相位 | `tests/native/fractional_frame_phase_tests.cpp` | 实际帧间隔、高刷、恢复和跨屏仍需专人执行 |
| W06 | 注册等待再重查；资源/action/scene 完成合并通知；正常路径移除 1ms commit/8ms draw 轮询；一次异常 100ms 拒绝重试 | `tests/native/resource_notification_policy_tests.cpp` | 第二次拒绝或分配失败仍保留意图，由后续 owner 服务或真实生产者事件重试；不制造永久异常轮询 |
| W07 | 两槽、256MiB 总池的画布 lease；客户端/GDI/bit 读取在锁外持有实际所有权；mirror 明确消费者契约，DC 请求恢复当前镜像 | `tests/native/frame_buffer_lease_policy_tests.cpp` | mutable CC 树绘制仍保留必要锁；未将其描述为已完全消除长锁；GPU 诊断路径单独资格 |
| W01 接入 | 32 个不连续 processed-input 区间、16 层 dispatch context；frame/action/scene 关联；真实 native submit、layout/record、copy/raster/锁/callback 计量 | `tests/native/input_frame_coverage_tests.cpp` | 无 T3b/物理可见/marker 冒充；溢出显式 causal loss，不能用于完整因果结论 |
| W08 | 默认一 worker；2–4 worker 资格/准入基础设施、可见/预取分配策略、取消 receipt/资源释放与全量重光栅恢复 | `tests/native/raster_scheduling_policy_tests.cpp`、`tests/native/render_scene_transaction_tests.cpp` | 资格门仍开放：通用 SkPicture 引用所有权无法精确界定，当前在任何候选 dispatch 前回退 worker0；不得宣称多 worker 候选已可采用或已完成 W08 |
| W09 | scene 构造→pending→ready→active 使用所有权移动；不动 action 区间、coverage/fence/ACK | `tests/native/render_scene_transaction_tests.cpp` 与既有 `tests/native/scroll_coverage/` | layout/几何缓存、immutable display-list 候选待成本数据；没有数据不构成不采用证据 |

trace producer 的 attach/detach 统一由实际 WebThreadImpl owner loop 与主 Runtime 配置持有，B 不按页面或 GC wrapper 重复 detach。所有新增回归用例使用显式失败返回值，Release/NDEBUG 下仍执行断言逻辑。root 统一接入共享 CMake、清单、来源补丁和 Draft PR；本线未修改 DLL/LIB、发布状态或验收证据。

## 所有权、失败与兼容

软件画布池最多两个槽、总计 256MiB。超过当前 byte budget 或两个槽仍被读取时，保留 dirty，等待 lease 释放或下一次 owner 服务；不复用读者持有的旧尺寸/设备代际。镜像另保留一个兼容 canvas，其尺寸受已接收画布尺寸约束。第一次/resize/alpha 必须全量处理，之后沿用原有 dirty copy。借用 DC 在获取时刷新为当前快照，下一次 owner paint/resize 可更新内容；调用者不得跨这些操作保存裸 HDC。旧的环境 mirror 实验不能覆盖显式 DC 消费需求。

资源取消用带原 action id 的 blend receipt 补齐动作区间，但不写入任何 ready tile。失败 scene 从 pending/ready 撤销，active scene 和上一张画布继续保留；在 owner 对所有 live layer 完整 invalidate 后才允许替代 scene。替代 scene 仍等自己的完整资源 fence。取消前尚未呈现的 processed-input 覆盖重新并入替代提交；仅 trace 容量不足时报告关联丢失，不丢输入事件。关闭后的通知只使用独立存活状态，不访问已销毁 host。

`drawMinInterval` 兼容显式有限非负值；长间隔以最多 1000ms 的 deadline 分段等待，不因 2 秒配置永久保留 dirty。正常 SDK 配置由匹配能力的 `mainFrameRate` 提供分数间隔，显式实验覆盖仍可回退。没有用配置 0、无界任务、丢输入或绕过 fence 作为优化。

## W08 资格基础设施与未关闭缺口

显式请求 2–4 worker 时，资格检查最多检查 128 个 CC layer、1024 个 resident tile、64 个任务和 64MiB 受控字节；检查使用实际 CompositingTile 的 bitmap stride×height，并拒绝未知 image layer owner。帧尺寸 generation 改变即使资格失效。任务与 group 在任何并发 dispatch 前必须有完整的 picture/滤镜/新 tile 及完成后 action 载荷所有权预算。

当前 `SkPicture::approximateBytesUsed()` 明确不包含大型引用对象；单纯 playback 到分析 canvas 还可能跳过 clipped 操作，因此不能证明引用图片、shader、path、字体等的驻留上界。本次保留 `UnknownPictureOwner` 明确资格拒绝，实际任务在 worker0 运行；不在录制事务中同步执行溢出 raster，也不通过取消 tile、skip fence 或扩大缓存强行准入。`candidateDiagnostics()` 提供 qualified/fallback/lastReason，详细 trace 的 resource_wait.value 是资格失败枚举，related_id 是 byte budget。现有全局资产缓存与画布池各有自己的所有者，不能把此 budget 叫作整个进程内存上限。

W08 未完成的具体工作是为真实非空 picture 建立可核对的有界资源所有权证明及可用资格域，再由专人做集中 profile 决定采用。当前的资格守卫、准入策略和取消恢复源码不能替代这项证明；既不是已采用，也不是“不采用”的证据。

## W09 后续候选的具体设计

锁内的 mutable CC tree walk 尚无成本证据。后续 immutable display-list 候选必须先在 owner/compositor 持锁期间取得完整 scene/action/scroll fence 对应的不可变命令与资源引用，再在锁外持有写入 lease 绘图，最终只在 size/device/document/scene generation 仍匹配时交换已完成帧。最多一个正在生成的 display list 和一个等待提交的 list，二者合计字节有硬上限；满额时保留 dirty，不能排第三份快照或解除资源引用。旧 visible scene 仍持有的 tile 不可被驱逐。

导航、resize/DPI/device、style/geometry、scroll epoch 和显式 CSS/Home 外部取消均使相应元数据失效。输入 prediction snapshot 是独立的可变副本，不能为了减少 copy 与 active scene 共用可变节点；现有那一次必要复制保留。未知 shader、图片、字体或其他引用资源的所有权/字节数不可用近似 picture 大小冒充。

采用门槛由验证人员冻结：先比较 layout/record、scene 构造/复制、copy bytes、锁等待/持有、client callback 的占比；只在成本显著且像素/输入/关闭契约全通过后选择候选。详细 trace 开关需先做配对开销预检，存在 dropped/causal_lost 的轮次不能用于完整逐输入结论。

## 一次性执行清单

验证人员按 root 的交付清单构建匹配的 SDK/内核，再执行新增 policy/coverage/scene 源码目标和既有 scheduler/input/scroll regressions。之后用同硬件、同载荷、同窗口/DPI 的基线与候选运行一次集中 profile，覆盖 default DIRECT_HWND、legacy、DC/截图、透明窗口、callback 重入、持有 lease 时 resize/close、资源永不完成/拒绝以及低核/多核。记录 trace 开关扰动、输入到提交/桌面 ACK 的分布、帧间隔、CPU/wake、copy/raster bytes、锁 P99、队列最老年龄、后台进展和拒绝/取消计数。桌面 ACK 不代表屏幕发光；未测物理阶段继续未知。
