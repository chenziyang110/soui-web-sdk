# ADR-0016：运行档位与可选现代渲染后端

- 日期：2026-10-08
- 状态：preview 实施中；正式发布仍需维护者评审及目标环境验收
- 关联：[0.4 路线与执行清单](../roadmap/0.4.0.md)

## 背景

0.3.0-preview.10 已提供公共宿主接口、动作契约和 GUI 语义。WebView2 仍在独立实验中，默认 Miniblink 使用软件呈现；GPU 关闭、长稳和跨系统性能门槛尚未关闭。用户要求完成下一版的流畅渲染、现代前端、Win7 以上及 PE 兼容。

## 决定

1. 公共 SDK 增加无浏览器依赖的运行环境探测与档位选择。探测结果描述事实和候选后端，不等于该环境已通过验收。未知环境失败关闭；明确请求的现代能力不得静默降级。
2. 现代 WebView2 作为可选 `Soui::WebView2` CMake 组件，独立于基础 `Soui::Sdk`。复用实验中的安全边界与 Bridge，移入 SDK 所有权；浏览器运行时独立部署。核心包默认仍可在旧系统启动。
3. 现代后端使用 UI STA、异步初始化、可重试关闭；本地受控资源、会话轮换、限额和原生授权保持一致。初始化必须在成功及失败时完成通知；不得把 Demo DOM 当作公共接口。
4. 兼容档保持 Chrome 49 构建目标；现代档使用明确的较新目标。构建检查和实际运行能力探测分开，构建成功不能证明浏览器 API 可用。
5. PE 单独识别，检查原生架构一致性，以无 GPU 要求、离线、自带必要运行依赖为目标。不会假设 PE 包含 Explorer、安装服务、WebView2、完整字体或跨架构兼容层。
6. 帧调度使用有界通知和单调时间；旧系统后备计时器改为按下一帧期限唤醒，避免固定 2 ms 轮询。隐藏窗口停止宿主帧脉冲，恢复时重建期限，禁止补发历史帧。

## 备选与约束

单一旧内核无法通过转译获得全部现代 CSS/Web API；统一捆绑最新 Chromium 不能直接覆盖 Win7。WebView2 109 已停止接收 Win7 功能及安全更新，不作为现代档。现代内核向旧系统移植是独立研发及维护项目。

## 兼容与迁移

现有 Miniblink 应用和公共宿主 API 保留。现代后端显式选装；业务通过相同 Web SDK、动作与 GUI 契约接入。0.4 的新 SDK 内容使用新版本标识，旧发布包保持不变。开发机工具链和最终用户运行要求分开记录。

## 验证

纯策略边界、原生环境探测、计时与关闭测试；x86/x64 构建及真实启动；安装后的独立现代消费者；现代与兼容前端夹具；Win7/PE 实机或来宾内采集；1,000 轮生命周期、8 小时资源趋势以及真实输入/呈现测量。未执行项保持未验证，不能修改成通过。

## 外部依据

- [WebView2 系统支持](https://learn.microsoft.com/en-us/microsoft-edge/webview2/)
- [Win7/8/8.1 WebView2 支持终止](https://blogs.windows.com/msedgedev/2022/12/09/microsoft-edge-and-webview2-ending-support-for-windows-7-and-windows-8-8-1/)
- [WinPE 限制](https://learn.microsoft.com/en-us/windows-hardware/manufacture/desktop/winpe-intro?view=windows-11)
