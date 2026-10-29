# 本地源码静态审查记录

本轮只有源码审查、语法和身份检查，没有编译、运行单元测试、VM/GUI 或性能采样。最终文件身份由同一候选的 `source-manifest.json` 绑定；本记录不能挂到旧 DLL 或后来修改的代码。

## 独立审查范围

集成负责人以外的 `review_core` 独立读取 MiniBlink 调度/呈现、SDK 输入接纳与 geometry 策略、WebView2/meter、ZIP 读取器及 Scratch 源构建。raw W 作者 `close_raw_webview` 另行复核 WebView2/meter/Electron 消费端，其没有编写这些生产文件。raw W 由集成负责人和 `review_core` 审查。作者自己的复查不计作独立审查。

审查及修正涉及以下具体路径：

- ready/deadline 的取消漏醒、WM_TIMER 旧票号、WM_QUIT 饥饿、跨线程 HWND timer、一次性 timer 复用，采用 threadpool timer 和 owner 代际消息。
- 资源等待先注册后重查、pool 通知注销与借用、seal 后绘制通知、通知 payload 的未运行析构、GPU 不宣告 CPU 镜像能力、透明/DC 失效及 sticky 故障。
- 输入开始的结束容量、Engine epoch 原子核对、同 FIFO、拒绝捕获锁外析构；resize 不静默丢同文档文本/终止边沿，旧 view/document 仍拒绝。
- WebView2 保留 state/Controller/core 强引用；初始 mapping/Bounds 可重入取消后必须仍 prepared 且 COM 身份一致；每个回复 COM 边界后复核文档代际。
- 计量不再丢掉首尾无 rAF 或零帧窗口；旧 cadence 含义保留，整窗 rate 与 censored 分位数资格分开；重叠计数租约到最后一位 observer 才释放。
- raw W 保留实际异步回调计数及同步活跃回调保护，Close 后继续排空；路径用现有父目录 canonical，输出/profile 与发布资源互斥；完整资源库存绑定 SHA256，失败不自动重采。
- ZIP 解压内容逐文件绑定运行库存，不能用旧 ZIP 搭配新目录；Scratch 构建保留整个对应源码与固定 lock，使用 Windows 可执行的上游 workspace 命令。
- legacy 延迟绘制改为真正 owning Task；拒绝/取消只退 slot 和显式故障，不执行 GDI；提交必须在真实外层 compositor 锁释放之后，避免 lifetime/compositor 锁反转。

## 静态检查与实际门槛

对修改的 JS/构建驱动和回归源码执行 `node --check`，对修改的 PowerShell 入口执行 parser 检查；执行 `git diff --check`，核对 MiniBlink 两份原始字节清单与补丁身份、worker source-map、最终源清单。这些命令只证明对应的语法/字节条件。

尚未执行的门槛见 [验证交接](validation-handoff.md)：匹配内核/SDK 的两架构构建、真实输入/IME/像素/DC/关闭/暂停、Win7 导入、故障注入、三项体积比较、受控同负载 E/W/S 与 CPU/GPU/内存/启动。静态未发现阻断不等于这些门槛通过，当前 `releaseReady` 和性能通过状态不扩大。
