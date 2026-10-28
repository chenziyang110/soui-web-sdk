# ADR 0129：SHostWnd 上的原生诊断菜单

状态：proposed；2026-10-23 本地源码修复，尚未编译或通过真实窗口验证。

## 背景

兼容模板和 Workbench 使用 SHostWnd 承载完整 WebView，不加载 SOUI 的 XML 非客户区。仅调用 SetMenu 并不能建立系统菜单的客户区和命中契约：SHostWnd::OnNcCalcSize 将整个窗口作为客户区，OnWndNcHitTest 和 OnNcActivate 也处理了本应交给系统的消息。Workbench 另有 resize_border_hit，直接返回其结果时同样会覆盖 HTMENU。

已有 WM_SIZE 向基类继续分发的试验仍是失败样本，不能当作此问题的修复。此处不改变 WM_SIZE 路由，也不将菜单修复与引擎像素呈现混为同一结论。

## 决定

保留两个窗口的 SHostWnd 基类，仅对已经成功附加的本窗口诊断菜单启用原生非客户区：

- 保存附加成功的菜单栏句柄；只有可信原生开关开启且 GetMenu 仍等于该句柄时才启用此路径。
- WM_NCCALCSIZE、WM_NCHITTEST、WM_NCACTIVATE、WM_NCPAINT 交给既有 CSimpleWnd::DefWindowProc。其他消息仍走原派生窗口和 SHostWnd 路由；Workbench 无诊断菜单时继续原 resize_border_hit。
- 菜单附加成功后，以不移动、不缩放、不激活窗口的 SWP_FRAMECHANGED 重新计算非客户区。后续 Host 使用真实客户区尺寸。
- 已附加的菜单栏由窗口保有到销毁；附加前失败销毁本地创建的菜单。复用 Workbench 已有菜单栏时不销毁借用的栏。

默认 allow_native_devtools 仍为 false，不增加网页、CLI 或环境授权。原生菜单和窗口内快捷键继续调用同一个带默认拒绝确认框的授权函数；pending、撤销、失败保留、原始关闭预算及严格排空顺序不变。

## 取舍

启用诊断菜单的窗口使用真正的系统标题栏、边框和菜单区，因此其客户区小于相同外框尺寸；WebView 内已有自定义标题区域仍属于业务页面，本次不改页面布局。默认关闭的窗口保持既有非客户区行为。

未选择 CSimpleWnd 迁移。Workbench 的 resident normal-window 策略、完整 GetCurrentMessage 调用栈、SHostWnd 创建/销毁、拖放及其他容器钩子需要成套迁移；为一个菜单命中问题切换基类会扩大业务和生命周期风险。保留 SHostWnd 也使原生菜单的模态默认处理继续被原完整 WindowProc 栈覆盖，controller 仍不得在该栈未退出时释放窗口或 Host。

SOUI 的 SMenu 提供 popup 包装；当前页面没有对应可见原生按钮。SMenuBar 需要 SOUI 控件/XML 布局，不是这两个完整 WebView 宿主上的更小替代。本次不另建浮动窗口、网页菜单或通用菜单框架。

## 验证边界

本轮只进行源码差量和接口审查，不执行编译、产品或 VM。后续集中构建必须绑定新源码和载荷身份，再验证默认关闭、可信 opt-in 的菜单可见和鼠标/键盘命中、调整尺寸/最大化/DPI、原快捷键同一确认、业务 ready、Inspector pending 与结束诊断关闭，以及 Workbench resident 窗口的菜单模态关闭和其他窗口继续工作。普通 WM_COMMAND 记录、菜单句柄存在或截图中的标签均不能替代这些真实动作。旧失败和旧版本成绩不转挂本次源码。
