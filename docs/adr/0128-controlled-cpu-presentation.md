# ADR 0128：受控业务窗口的 CPU 呈现

状态：proposed；2026-10-23 本地候选修复。源码结论不代表真实窗口或生产验收通过。

## 背景

受控 broker 创建视图时无条件关闭 `setNeedAutoDrawToHwnd`，随后直接 `setHandle`。普通 `wkeSetHandle` 会按有效 HWND 打开自动绘制，但 SDK 的受控路径不能绕过 raw-view 保护调用它。SDK 业务 paint 回调只记录绘制事件，不复制像素；引擎 CPU 绘制又受 AutoDraw 开关限制。由此存在明确的呈现契约缺口。此前兼容应用的真实启动超时仍保留；尚未证明该缺口是全部运行故障的原因。

Inspector 使用另一条呈现路径：SDK 的私有 session 从 paint 回调复制像素，再由自己的 WM_PAINT 绘制。若同时开启自动绘制，会引入重复呈现。

## 决定

仅在 broker 内部的 `createEndpointImpl` 增加显式呈现模式：

- 公共 `createEndpoint` 创建绑定已验证所属 HWND 的业务 endpoint，采用 `NativeWindow`，开启引擎原有 CPU HWND 绘制。
- 私有 `stage0CreateFrontendResources` 采用 `CallbackOnly`，继续通过 Inspector 已有像素回调和 WM_PAINT 呈现。
- `retainedControl` 继续仅表示失败清理的强所有权，不用其是否为空推断呈现模式。

配置结构、导出 ABI、reserved 校验、所属进程及窗口验证、raw-view/setHandle 保护、token/receipt 身份和原 pause/detach/drain 顺序保持原契约。模式只在创建时设置，不新增任务、回调或窗口探测。所属 HWND 仍须存活到原 endpoint 退休完成。

## 备选方案

新增公开 flag 会扩大 ABI 和消费者迁移面；现有 EndpointConfig 的 reserved 必须为零，不能当作新字段。现有 interaction 的 `PRESENT_DIRECT_HWND` 只开启独立调度，并不设置 CPU AutoDraw。由 SDK 回调自行 BitBlt 则需要另建像素、线程、窗口生命周期以及透明/裁剪逻辑。本次复用引擎已有呈现路径。

## 兼容性与迁移

公共 broker endpoint 的行为变为所属窗口自动呈现。历史实验 probe 中手动复制同一业务 endpoint 像素的消费者可能出现冗余绘制；其旧性能结果不能沿用。正式 Inspector factory 明确保持 callback-only。GPU direct-present 的正常帧绕过 CPU gate；读回或 CPU fallback 仍需此开关，诊断 readback 可能产生额外 GDI 绘制，不能据此宣称 GPU 已验证。

候选使用新 preview 身份，不覆盖旧包、旧内核或原失败样本。回退须使用原成套内核、SDK 与资源身份。

## 验证

先独立核查两个调用点、窗口/所有权保护和最小差异，再进行受影响内核增量构建、SDK 与应用重新绑定。仅在载荷身份固定后，在同一任务后台虚拟机验证业务 ready、可见内容、输入、Inspector 绘制与严格关闭。旧 DLL 或旧 EXE 的成绩不转挂新源码。全部架构、功能矩阵、循环与真实长测仍按原任务验收。
