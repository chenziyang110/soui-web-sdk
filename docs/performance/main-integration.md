# PR #129 的主线集成

本次以 `main` 的 `0cdc0a9ace0cdce664ec4a9b54e5f8f71b97ba48` 为底稿，选择移植旧冻结候选 `ce996ac8e534c634ced4389dd06f0a8dba0f0def` 中尚未被主线覆盖的改动。不是将 `c17c79bdf470fae681c63324ceaa2a519151afc9` 旧树整体合入。历史提交保留，PR 目标分支为 `main`。

## 保留和移植

| 范围 | 主线保留 | 本轮增量 |
|---|---|---|
| 输入 | mailbox、FIFO ordinal、ordered cancel、geometry-applied fence 和 trace | 使用主线结束/恢复契约，不引入旧候选 ordinary-ticket 接纳模型 |
| 调度 | AVL deadline 索引、合作批次、owner 输入机会、关闭和取消 | 持有的 Win7 threadpool deadline/retry timer，worker 有界续调度 |
| 呈现 | 共享资源取消状态、双画布 lease、paint activity、mirror consumer、native 可见性和 trace | 精确帧率能力、按需镜像协商、资源通知失败诊断、dirty 合并选择 |
| WebView2 | owner guard、强 COM lease、opaque callout 后身份复核、可选输出预算和 trace | 成功 Controller 更新去重及固定 2ms 上限，保留更紧的显式预算 |
| Scratch | SDK 与 Demo 所有权方向、默认 strict 资源策略 | 完整示例及原生显式 opt-in 本地编辑器策略、meter、源码构建与 E/W/S 工具 |
| 发布工具 | #127/#128 ZIP、MSVC 生命周期及拒绝发布恢复 | 独立三项体积门禁及文档交付清单，既有发布代码不回退 |

`VERSION`、发布记录、已有验证证据和固定二进制保持原身份。新增候选不继承任何旧运行通过；不会用未增长的源码文件数代替运行包体积证明。ADR-0137 的 Windows 工具决策保留，调度新决策采用 ADR-0143。原补丁编号保留，性能增量使用 0024/0025。

## 交接边界

本轮只执行源码集成、静态审查、语法与来源身份检查。维护者明确由专人执行编译、测试、窗口、体积和性能验收，入口见 [验证交接](validation-handoff.md)。源清单只证明当前候选源码字节。所有新增运行门槛保持未验证，`releaseReady` 不扩大。

后续打包必须使用独立候选输出及匹配重编的 SDK/内核，不能以相同 `0.6.0` 身份覆盖已冻结安装内容或重新发布旧资产。若准备公开交付，需另行选择新版本身份并完成原发布流程。
