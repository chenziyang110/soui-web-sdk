# 主线集成的静态审查记录

本轮只有源码审查、语法和身份检查，没有编译、运行单元测试、VM/GUI 或性能采样。最终文件身份由同一候选的 `source-manifest.json` 绑定；本记录不能挂到旧 DLL 或后来修改的代码。

## 独立审查范围

旧冻结候选 `ce996ac8e534c634ced4389dd06f0a8dba0f0def` 的 `review_core` / `close_raw_webview` 独立审查仅对应当时源码。当前从 `main` 的 `0cdc0a9ace0cdce664ec4a9b54e5f8f71b97ba48` 重新集成，按 [集成取舍](main-integration.md) 保留主线 mailbox/FIFO、AVL、共享资源取消状态、画布 lease、trace、DevTools 和发布工具实现。旧候选审查不直接视为当前集成通过。

历史候选审查及修正涉及以下路径；当前仅选择未被主线方案覆盖的增量：

- ready/deadline 的取消漏醒、WM_TIMER 旧票号、WM_QUIT 饥饿、跨线程 HWND timer、一次性 timer 复用，采用 threadpool timer 和 owner 代际消息。
- 资源等待先注册后重查、pool 通知注销与借用、seal 后绘制通知、通知 payload 的未运行析构、GPU 不宣告 CPU 镜像能力、透明/DC 失效及 sticky 故障。
- 输入开始的结束容量、Engine epoch 原子核对、同 FIFO、拒绝捕获锁外析构；resize 不静默丢同文档文本/终止边沿，旧 view/document 仍拒绝。
- WebView2 保留 state/Controller/core 强引用；初始 mapping/Bounds 可重入取消后必须仍 prepared 且 COM 身份一致；每个回复 COM 边界后复核文档代际。
- 计量不再丢掉首尾无 rAF 或零帧窗口；旧 cadence 含义保留，整窗 rate 与 censored 分位数资格分开；重叠计数租约到最后一位 observer 才释放。
- raw W 保留实际异步回调计数及同步活跃回调保护，Close 后继续排空；路径用现有父目录 canonical，输出/profile 与发布资源互斥；完整资源库存绑定 SHA256，失败不自动重采。
- ZIP 解压内容逐文件绑定运行库存，不能用旧 ZIP 搭配新目录；Scratch 构建保留整个对应源码与固定 lock，使用 Windows 可执行的上游 workspace 命令。
- legacy 延迟绘制改为真正 owning Task；拒绝/取消只退 slot 和显式故障，不执行 GDI；提交必须在真实外层 compositor 锁释放之后，避免 lifetime/compositor 锁反转。

## 静态检查与实际门槛

当前集成重新检查 timer 发布与关闭排空、能力位兼容、跨 view 资源背压唤醒、opaque COM 边界与文档身份、显式本地编辑器范围及构建入口复用主线 MSVC 生命周期。只对修改的 JS 执行 `node --check`，对 PowerShell 入口执行 parser 检查；执行 `git diff --check`，重新绑定 MiniBlink 两份原始字节清单与补丁、worker source-map、最终源清单。最终执行数量及独立审查发现见 PR #129 当前提交说明。这些命令只证明对应的语法/字节条件。

本次独立交叉审查：WebView2/Scratch 作者只读复核 render 增量；render 作者只读复核 scheduler、WebView2 资源策略及 SourceVendor 打包。发现的 30/60Hz timer 精度申请缺口和 SourceVendor 包缺少完整对应源码/身份已由各自作者修复，复审未发现剩余源码阻断。主线输入/画布/COM/发布工具保留项及取舍均按实际差异检查。该结论不替代任何编译或运行结果。

尚未执行的门槛见 [验证交接](validation-handoff.md)：匹配内核/SDK 的两架构构建、真实输入/IME/像素/DC/关闭/暂停、Win7 导入、故障注入、三项体积比较、受控同负载 E/W/S 与 CPU/GPU/内存/启动。静态未发现阻断不等于这些门槛通过，当前 `releaseReady` 和性能通过状态不扩大。
