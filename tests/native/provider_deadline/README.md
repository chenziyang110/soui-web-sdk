# Provider 截止原字节回归

`generate.cjs` 从当前生产源码逐字节投影 InputUiApi 的旧/新入口和真实 API 表、CWebView 的扫描/关闭准备方法及现有 cursor。投影含原文件、范围与片段 SHA；所选片段必须是 ASCII，不转码或复制改写实现。

夹具显式替代平台、QPC、scheduler 接收端、view/policy/绘制接收端；它验证接口和真实扫描控制流、最低关闭份额及有限续接，不能证明真实 Blink scheduler、完整 TU、内核、资源 fence 或物理性能。scheduler 与 Engine 的真实源回归由各自夹具负责。

每个 CTest 用独立进程隔离静态 sweep/cursor。覆盖校验失败不做工作、过期 D 下两条 lane 都能服务、32 次有限 sweep、reader 等待不忙转及外部恢复、删除/重入、旧入口及 shutdown 不受新截止截断、部分失败不冒充可安全回退，以及实际表尾/原 ABI 前缀。
