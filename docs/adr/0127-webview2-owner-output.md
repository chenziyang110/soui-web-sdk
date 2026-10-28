# ADR-0127：WebView2 owner 输出与重入边界

日期：2026-10-26。状态：proposed。源码完成待编译；预算/预序列化候选默认关闭，采用待 profile。

大量 Bridge 输出在 owner 上的 JSON/UTF16 转换与 COM 调用可以扩大输入等待。保持既有最多16条、lane/容量/来源/会话/导航撤销契约，先提供成本计量与有界候选，不改变外部 WebView2 Runtime 内部调度。

- `SOUI_WEBVIEW2_OWNER_BUDGET_US=100..5000` 默认0/off。启用时最多16条，并保证至少一个合格输出机会；不能抢占单个 COM 调用。
- `SOUI_WEBVIEW2_PREPARE_OUTPUT=1` 默认off。纯 JSON/UTF16 准备在 worker、queue mutex 外；真正 COM 仍在 owner。raw与prepared合计受同item原64KiB credit及原lane预算约束，无法缓存/OOM沿用原serializer。
- owner 用局部 ComPtr 固定实际 target；opaque get_Source 前后复核 phase/core/generation，delivery 前再复核来源/会话/文档。诊断 ExecuteScript 和完成回调同样核对，旧调用不能修改新文档 flags。
- pump guard覆盖整个活跃owner entry，COM回调不能递归开启第二批、越过16项或颠倒后续输出。
- trace冷注册、hot borrow与owner/serialization直方图复用W01；不在热调用中自动attach或写文件。

源码为 `sdk/renderers/webview2/{webview2_host.cpp,owner_output_policy.h,owner_callout_policy.h}`；`tests/native/webview2_owner_policy` 定义 `soui_webview2_owner_policy_tests` / `sdk.native.webview2-owner-policy`。纯policy/fake SOURCE覆盖预算、解析、credit、目标存活、get_Source关闭/导航重入与nested pump。真实 COM、进展、CPU/尾延迟仍待专人；本阶段未配置、编译、运行测试或应用。

独立静态审查已修正诊断getter返回后core可能关闭的缺口。最终集成文件与作者已审版本逐字节核对；新目标按MD/U_DEBUG与有限超时配置。匹配SDK/内核产物身份必须由专人冻结，固定prebuilt未替换。

专人一次执行policy与既有容量/导航/准备生命周期集合，再用冻结实际Runtime版本、架构、页面、响应负载和输入轨迹profile；分别记录owner时间、serialization、COM、CPU、接受/拒绝/晚到结果和后台进展。没有成本或正确性证据不默认采用。候选造成错误或尾延迟/资源退化时保持两个开关关闭，恢复原表示/16项策略；保留重入与来源修正。
