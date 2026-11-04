# ADR-0156：WebView2 输出准备的私有分段观测

状态：实施观测辅助与无窗口 CPU 对照；默认策略保持不变，owner 公平性等待真实输入 trace。

## 背景

WebView2 `push_output` 的 `trace_task` 在准备前分配，`queued_qpc` 在准备完成和 output credit 接纳后赋值。现有 task_queue histogram 只包含实际队列等待，不能用来判断准备耗时。现有 `SOUI_WEBVIEW2_PREPARE_OUTPUT=1` 由调用 push_output 的线程执行，并非统一 worker：普通回复通常是 Bridge worker，拒绝分支是 owner STA，事件来自 sink 调用线程。相同 serialization metric 同时包含预制和 owner fallback。

本决策先描述分段与来源，不新增worker，不改变开关默认值，不改变2ms/16调度预算，不调整COM/source/generation或credit契约。

## 决定

新增私有 `output_timing_policy.h`：只生成已有136字节 trace event，采用已存在的 task_posted、task_begin、task_end、task_cancelled，并按task_id关联原bridge_response。原bridge_response.value仍为raw.size()，不改成私有metadata。保持所有 input/action/frame/coverage 字段为0，不继承 current_context，不新增公开 stage、counter、duration enum 或布局。

私有 value 带固定版本签名及 origin/executor/preparation/bytes 编码；失败结果使用既有reason/outcome，用来区分 WebView2 输出观测与其他任务。origin由真实调用点传入，executor由实际线程位置提供；不将 event_sink 自动称为worker，也不将input_id=0解释成“没有用户输入”。编码未知值拒绝，不与其他模块复用私有位段。

executor字段描述最初准备工作所在线程类别，后续owner事件保留它以便归因；每条event自身的thread_id才是该条观测实际发布线程。未启用预制时的owner serialization仍发生在owner，不能从executor=bridge_worker推断它已转移。bridge_response是投递前观测，因此后续COM失败的task_cancelled可以与它共存，不计为成功交付。

- task_posted：push_output在初始live/generation门槛后、准备前。
- task_begin：准备路径进入。
- task_end：准备结束，含是否缓存成功/因字节上限回退/异常回退/未启用。
- bridge_response：沿用既有 owner 投递前观测点；它不是COM完成、JS执行或屏幕呈现。
- task_cancelled：未交付结果的本地终态；原因区分容量、停止、过期代际、入队异常。只写诊断，不给页面新消息，不改变当前output overflow由页面timeout观察的行为。

`task_end − task_begin` 是准备用时；`bridge_response − task_end` 是准备后到owner观测点，包含接纳锁等待等，不冒称纯队列时间。纯queue仍由原 `queued_qpc` 到owner的 task_queue histogram记录。多个线程的事件按QPC+task关联，时间相等的跨producer顺序保留unknown。完整样本需要完整trace，drop/未注册/饱和不能补算成功或丢失终态。

观测由冷配置的私有诊断开关 `SOUI_WEBVIEW2_OUTPUT_TIMING=1` 显式启用；关闭时helper不构造/发布额外事件，也不额外读QPC，原trace_task与queued_qpc保持。未注册的event producer不得在热路径attach；记录缺失/unknown并拒绝完整链路推断。生产payload、授权、权限与返回值不变。全部COM保持owner STA；output credit仍在Host锁内出队时释放，request lease仍在锁外且COM前释放。

## 无窗口对照与采用边界

对当前 serialize_output 的response/event、UTF-8→UTF-16、raw+wide byte cap做源码投影CPU对照。固定64B、4KiB、接近64KiB的payload，包含大量转义和Unicode；分别记录prepare阶段、模拟owner阶段、总CPU时间、成功缓存比例、fallback、工作字节和结果一致性。先证明投影与当前源码结构和边界一致；它不调用WebView2、COM或GPU，不提供FPS/输入延迟证据。

如果缓存未成立，启用prepare会先尝试编码而owner再次编码，可能增大总成本；如实保留。只有实际Host/同负载证明owner减少且总吞吐/内存/输入无退化后，才考虑改变默认值。持续WM_APP公平性不由此CPU对照决定。

加入纯 `should_prepare(enabled,is_event,raw_bytes,limit)`：当response raw字节数已达到 `limit/sizeof(wchar_t)`，其JSON string envelope不可能变短，必过现有encoded上限，可提前跳过必败试算，owner仍走同一原路径。event不使用该阈值，因为parse/dump可压缩大段空白；回归包含约60KiB空白event仍能缓存、阈值两侧及Unicode wire等价。此guard不打开prepare默认值，也不改变wire/容量/异常/权限行为。

## 兼容性、备选与验证

不选择扩公开duration数组：会影响SDK/vendor/reader ABI；不选择无界文本日志：扰动热路径；不选择按事件触发全局线程池：缺少成本与生命周期证据。私有helper可独立回退，原trace点和安全修复不回退。

测试覆盖每种origin/executor/preparation、编码边界、无因果字段、有效/无效task、已有recorder/reader接受、容量/导航/异常终态；无窗口bench验证两模式wire字节一致和近上限fallback。实际source/COM重入、overflow稳定性、真实输入队列公平仍由受影响Host集成测试验证。本ADR不提升releaseReady或性能资格。
