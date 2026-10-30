# MiniBlink 显式 provider 共享截止

基线：`5dab3c948fbe35e725789c0b061af0a50358ae91`。设计见 [ADR-0147](../adr/0147-provider-shared-deadline.md)。

本批把 Engine 已有的显式共享预算传入 provider，并分别保证 scheduler 与 deferred-close/retry 的最低进展。默认 `SOUI_ENGINE_SHARED_TURN_BUDGET_US=0`，不选择新的默认策略或预算值。

实施分为 SDK 的内部双 callback 安装与 duration/QPC 桥、SharedTimer/scheduler 同截止传递、provider 校验及有限关闭扫描。旧 ABI 前缀与无参入口继续支持；新尾字段必须经 version/size/capability/pointer 检查。发生部分工作后不得回退重复服务。

## 当前验证边界

源码实现已完成。x86/x64 实际 Engine/mailbox/clock/ABI/fallback 各 1/1；scheduler worker 各 43/43、SharedTimer continuation 各 3/3；实际 InputUiApi/CWebView 方法投影各 9/9、166 断言。x64 实际 SDK 静态库与 mailbox 增量编译和 CTest 通过，143 选定输入执行前后固定。SOURCE 80/80、八文件 0028 原字节补丁正反向重放通过。具体范围及保留失败见 ADR-0147 和来源清单。

修复了独立审查发现的重入替换 callable 时 active capture 可能提前析构的问题：调用前交换到局部，替换后的旧 callback 不恢复、不用旧 continuation 唤醒。旧 setter 清两种 callback，销毁在锁外。

用户要求减少耗时测试，本轮复用未变输入的既有成功，不启动每架构约 80 分钟的完整内核重建。完整新 kernel、匹配消费者、实际加载身份、二进制/运行目录/ZIP 增量、真实性能与物理输入仍待。正常 Scratch r2 保持 a006，用户未导出的 WebView2 69FPS 是人工观察，不能证明本 MiniBlink 变更的效果。

不启动或操作 Electron，不自动化用户窗口。完整 S/W/E 验收继续保留为未完成。

## 回退

以本批独立提交为单位回退；此前稳定时钟与绝对任务 deadline 保持各自提交身份。禁用共享预算继续走旧 provider 调用，但不能把关闭配置当作本批完整内核验证。旧预编译 DLL、已发布交付包和既有安装没有替换。
