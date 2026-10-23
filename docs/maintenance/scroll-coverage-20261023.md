# 修复 CPU 滚动覆盖判定时序与浮点误判

滚动进入新的预取区域时，场景快照可能先于本帧的 tile 准备过程记录“不完整覆盖”，即使对应 raster action 随后已准备并提交，快照仍保留 `coverageComplete=false` 和原因位 `128`。另外，在非零滚动位置进行平滑滚动时，浮点加减的舍入误差可能被误判为真实边界限制，第一帧采样后就清掉尚未完成的动画目标与速度。本补丁分别修正这两个内部状态问题。

## 触发条件与修改

### 本帧准备后的覆盖状态

原 `LayerTreeHost::recordDraw` 先通过 `updateLayersDrawProperties` 生成滚动快照并检查覆盖，再调用 `recordDrawChildren` 准备 tile。后者会消费预取请求、清除 dirty 状态、分配逻辑 bitmap 并创建 raster action，原快照却不会据此刷新。

现在保留两阶段处理：

1. 第一遍仍生成几何、命中区域、滚动元数据并提交预取请求，同时保存该节点的精确 raster 矩形、offset 窗口、身份及 epoch。
2. 在 `recordDrawChildren` / `endPostRasterTask` 后，再查询保存矩形的实际准备状态，只刷新同一快照中的覆盖字段及 `ScrollMainThreadReasonIncompleteCoverage` 位。

刷新不重新计算预取窗口、不再次请求 raster，也不创建新 scene version。其他回退原因、geometry、命中顺序、节点代际、scroll epoch、acknowledgement 和滚动条数据保持原值。已准备覆盖的无 raster 帧无需额外等待，仍缺 tile 或 dirty 的节点继续回退。

### 浮点误差不再终止动画

`ScrollController::sample` 原先以 `m_consumedTotal != desired` 判断是否需要重置动画。对于 `base=300`、`delta=90` 的用例，第一步 desired 约为 `9.802247435626398`，经 `(base + desired) - base` 得到约 `9.802247435626384`，约 `1.42e-14` 的误差也会触发 reset。

两轴现改为 `fabs(m_consumedTotal - desired) >= kMovementEpsilon`，复用既有 `1e-6` 移动阈值。真正达到或超出边界的 clamp 仍由 `MotionAxis::setBounds`、`addDelta`、`advance` 和 `reconciledVisualOffset` 执行；有实际位移差异时仍重置。未改变阻尼参数、输入目标、预取预算或业务流程。

## 资源提交顺序保持

逻辑 tile 已准备不等于像素已可显示。新的“不完整→完整”覆盖提升只允许用于带既有资源 fence 的场景，`resourcesReady = noRasterCommit || !hasActions` 与 action ID 范围均未改变。

生产代码顺序为：tile 准备后分配 raster action ID；每个 raster task 完成后释放 group 引用；最终 group 才追加全部 action；完整且封口的 action frame 应用后通知 `onActionFrameApplied`；`SceneCoordinator` 收到达到最后所需 ID 的回调后才允许激活。正常主帧先在 `recordDraw` 内提交 scene transaction，再由 `AutoRecordActions::leave` 调用 `endRecordActions` 封口。静态复核未发现本补丁绕过该提交顺序。

## 已执行验证

独立回归直接编译生产 `ScrollTreeSnapshot.cpp` 和 `ScrollController.cpp`。使用 MSVC 19.44、Windows SDK 10.0.19041.0、Release、静态 CRT，测试可单独复制到隔离环境。编译与执行入口见 [测试说明](../../tests/native/scroll_coverage/README.md)。

2026-10-23 的隔离 Windows 来宾执行结果：

| 对照 | x86 | x64 |
|---|---|---|
| 修复版 | 13 通过，0 失败，退出码 0 | 13 通过，0 失败，退出码 0 |
| 保存的旧 controller | 7 通过，6 失败，退出码 1 | 7 通过，6 失败，退出码 1 |

两组使用相同测试及相同的新 coverage helper，仅替换 `ScrollController.cpp`。旧源的六项运动用例均失败；第一帧 sample 有效且位移相同，但 `animation_active` 在旧源为 `0`、修复版为 `1`，两架构一致。通过项覆盖资源 fence、缺失/dirty 覆盖、其他原因位、身份/epoch、空帧，以及正反方向、横纵轴、多帧运动、反向输入、真实覆盖边界和同 epoch 缩小边界。

独立核对确认四项进程退出完整，70 条提取事件均存在于保留的原始日志，四个启动散列与清单匹配。脱敏数据见 [验证摘要](../verification/scroll-coverage-20261023.json)。早期发现浮点问题的失败结果保留，未放宽原运动断言，也未把编译成功当成运行成功。

## 当前验证边界

完整 x86 CPU 内核已使用冻结源码重新编译，构建退出 0；五个修改文件与冻结清单一致，23,545 个内核源文件的构建前后快照字节相同。DLL 为 28,116,992 字节，SHA-256 `6E293A3C8BF6F4F4CF32162B8BD01B165017C4BF7F34AF18A9B481A3E6B81185`。没有替换仓库固定预编译包，完整 x64 浏览器内核尚未构建。

Windows 10（10.0.19045）隔离来宾中的真实 HTML 协议页已分别运行旧、新 x86 内核；两包的界面、载荷、诊断插件及其余运行时成员相同。两轮均完成单格滚轮、连续正反向滚轮、14px 滑块短拖动、返回和确认退出，退出码均为取消安装的 1。正文固定 ROI 的 4096 个 RGB 采样点全部有效，无丢记录；诊断探针总耗时约 0.03ms 中位，退出时才写盘。

连续下滚样本的正文变化最大间隔为旧版 108.169ms、新版 33.832ms；上滚分别为 92.936ms、46.966ms。但实际原生输入跨度不完全相同，且只有各一轮，不能据此承诺整体流畅度或首响应全面改善。孤立单格仍是一次正文变化，符合既有首格非平滑策略。采样不是屏幕呈现计数，不能换算 FPS。滑块短拖动证明实际位移和后续按钮可用，不能外推长距离连续拖动的流畅性。

单元用例以受控布尔值提供 tile readiness，不运行 `TileGrid`、raster workers 或 `LayerTreeHost::recordDraw`。因此，单元结果证明 snapshot/controller 的行为契约和浮点修复；真实应用试运行补充集成行为证据，仍未覆盖完整系统矩阵。剩余滚动节拍和无诊断最终包验收单独跟踪，当前不宣称所有 GUI 卡顿已消除。

本补丁没有改变 pending scene 策略、prefetch 范围、主线程 scroll epoch 规则、鼠标屏障策略或 raster 分配失败处理。相关既有问题应分别记录，不与本次覆盖状态或浮点误判混为同一结论。
