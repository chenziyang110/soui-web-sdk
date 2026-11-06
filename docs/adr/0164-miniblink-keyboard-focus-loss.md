# ADR-0164：MiniBlink 普通键盘失焦的状态与 IME 边界

状态：私有原型限定验证；27项源码投影合同及两个旧代码失败对照、两架构增量编译/链接与一次 x64 窗口失焦功能条件已实际通过。仓库 vendor/SDK、默认运行库及冻结包未改；真实 IME、完整焦点矩阵及性能未通过。四项链接库为新冻结且历史等同性未知，不继承旧基线的配对性能资格。详见[追加验收](../performance/sdk-preview4-focus-20261106.md)；默认采用仍需关闭下述门槛。

## 背景与已执行证据

当前冻结 SDK `0.7.0-preview.4`、x64 build `18e47fa2-69df-4530-b1f9-365f6b2aec28`、MiniBlink DLL SHA `56d460c0b4136d85d3fcb8d4e4f00c19a04ff56532228031b55368742ef54308` 的普通失焦存在运行行为缺口。

R30 使用既有且后续构建实际通过的 full/manual 消费者，没有新增采集器或改页面。run `8a12e79a243d3336a8eb8bdb9f962a13` 的 SDK/vendor 两源完整、无 loss/fault，两进程正常退出0、Jobs0。view2/document2/geometry2 的 focus66→blur67→focus68 各有唯一 host_receive→admission(accepted)→engine_begin→handler_return 链；required `wkeKillFocus` 调用正常返回。

原 Finish 点击 input91 的 Engine 派发与同因果 bootstrap 请求，将页面 snapshot 限在 QPC `[1396180899116,1396180921850]` 的源/动作区间内。blur67 返回 `1396120948100`，早于该区间约5.995秒；之后接纳并在正常关闭后持久化的页面累计 `lifecycle.blur` 仍为0。启动期 doc1 的 stale focus1 已排除，不把关窗清理事件混入。这个证据不定位运行库内部唯一分支，也不证明 IME 或性能收益。

证据相对于验收归档 `PR129-performance-20261104/`：

- `repair-r30-focus-functional-r1/analysis-r1/actual-focus-chain-r1.json`，SHA `c9aa4fbc436c1ca9811979c552882a8ad0a7ce6ae612a783bdf0690dcf170f0e`。
- 同目录独立收据 `independent-actual-focus-review-r1.json`，SHA `144c57a292c6ac655a07416b557df50dbf930380f9439093a589054991c74f43`。

## 源码事实

正常 SDK 路径已有 `WM_KILLFOCUS`→有序 blur packet→Engine→`wkeKillFocus`。不能把消费者没有处理顶层 `WM_KILLFOCUS` 描述为 SDK 没有路由。

`WebPageImpl::fireKillFocusEvent` 对 `GetFocus()==m_popupHandle` 直接返回，而 popup handle 初始为 NULL；两者均 NULL 不能认证焦点进入本页弹窗。其正常路径又注释了焦点清除。[GetFocus 的官方契约](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getfocus)只查询调用线程的消息队列；它返回 NULL 不表示全局没有键盘焦点。本轮没有读取此 API 的实际值，不声称命中了这个内部条件。

完整 `WebViewImpl::setFocus(false)` 包含更多操作：调用 FocusController、隐藏 popup、确认尚存 composition，并关闭 `m_imeAcceptEvents`。SDK 还独立按原生消息 FIFO 接收 IME update/commit/end，现有 commit 处理不检查 `wkeConfirmComposition` 的返回值。不能把恢复这一调用当成无副作用的两行修复。

## 候选与不可破坏的规则

优先评估仅恢复普通键盘焦点状态的候选，冻结后才能编码：

1. 只有非 NULL 的本页 popup handle 与当前查询结果相等，才沿用既有 popup 例外；不引入 AttachThreadInput、跨线程全局焦点轮询或标题猜测。
2. 保留 Engine owner、原 input FIFO/ordinal、context/generation 与 `CHECK_FOR_REENTER`。新失焦回调只评估现有 SDK broker 受控 view；非 broker legacy 路径保留原行为，不把它宣称为修复对象。`CHECK_FOR_REENTER` 本身不保活 `WebPageImpl`：必须另核 broker 的活跃回调销毁屏障，不能以更多 state 检查掩盖 raw 销毁后访问。popup 隐藏或焦点事件可回调 JS，必要调用之间还须复核原 view/页面/文档仍有效。
3. 评估在正常 blur 中使用 `view->page()->focusController().setFocused(false)`，不额外清空 focused frame/element/selection range，保留正常 FocusController 语义；不顺手 `setIsActive(false)` 或重新激活窗口。该调用仍会停止 autoscroll、改变 selection 焦点状态、隐藏 caret、触发样式/拼写检查/secure-entry 更新及同步 DOM blur。键盘焦点转移到同窗原生控件，不等于顶层窗口 deactivate。
4. 该候选不主动 confirm/cancel composition，也不主动改 IME acceptance；原生 IME pipeline 保持 finalize 的所有权。这是嵌入器有意选择的窄路径，不与完整 Widget setter 等价。DOM blur 回调自己触发的编辑、选区改变、导航或 focus 操作仍按既有契约生效，其中选区失效可能间接取消 composition。popup hide 可进入 client 回调；必要调用前后须检查 owner/state/view/page，最后的焦点回调之后不无条件清旧 popup 或重写旧 focus 状态。不能宣称 selection 或 IME 不受影响；modal defersLoading/plugin 等既有特殊语义仍单独保留。
5. `cancelInputState` 继续只用于既有恢复/取消语义，不拿它代替正常 blur；不丢弃或合并键盘、IME、按钮边沿或 pointer move。

## 备选方案

- 保持现状：不采用为修复；R30 的运行行为缺口仍存在。
- 直接恢复完整 `WebViewImpl::setFocus(false)`：须先证明其自动 composition 确认、晚到 native commit/end、重新聚焦与会话切换不会丢字、重复或错投。没有证据时不作为默认实现。
- 普通 blur 调用 `cancelInputState`：拒绝。它清空输入状态并取消 composition，会改变合法文本输入及恢复边界。
- 改业务页补发 DOM blur：拒绝。普通 SDK 应用不能靠业务补丁掩盖 provider 状态错误。
- 新 provider/public API：目前不决定；只有现有内部原语无法正确保持 IME/生命周期契约时才另行评审，不为绕过验证引入 ABI 扩展。

## 验证与采用门槛

- 使用实际生产控制流的针对性反例：SDK broker 的 NULL/NULL 普通失焦、非 NULL 本页 popup 保留、普通焦点 true→false→true、重复失焦、popup hide 导航/关闭/替换 view、blur 回调重入重新聚焦，以及 legacy 行为保留和 broker 内 raw 销毁拒绝。模型/源码片段测试不得冒充完整 Blink 或 OS IME 运行。
- 明确测试 composition update→blur→native commit/end，以及 blur→regain→迟到 commit/end；检查文本最终值/事件次数和原 ordinal/终态，不只统计 API 返回。旧 Widget 自动确认与新候选的行为差异必须可解释。
- 私有候选用独立输出、真实源码/编译/link-input 身份，不能覆盖冻结 DLL、SDK 包或旧收据。复用现成消费者作限定 old/new 功能检查；不新增采集器或用短功能样本宣称 FPS/物理输入延迟收益。
- vendor/ABI 影响按原工具链完成 x86/x64 适用编译与合同；缺少可追溯增量缓存时不能把未知旧对象当当前基线。MSVC/C++17/SDK19041 及原最低系统要求不提高。
- 只有候选验证完成后，才决定导入默认运行库；导入时分配新的 SDK 开发版本，同步来源补丁、manifest、文档与体积，不向已有 `preview.4` 包写入新字节。

## 兼容性、迁移和回退

候选不新增线程、队列或公共数据布局，不改变帧策略、Bridge 授权和调度默认值。若采用，应用会重新收到本应发生的 DOM blur/focus 及相关回调，可能显露此前未运行的应用事件处理；需在变更记录说明。

回退使用旧 SDK 与完整匹配运行库，不单独覆盖 DLL。R30 原缺口与旧性能失败始终保留；任何限定焦点修复均不自动关闭完整 IME、输入到可见反馈、真实帧率或生产门槛。
