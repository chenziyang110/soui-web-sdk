# ADR-0158：MiniBlink 原生接纳复用同请求的同步解析视图

状态：私有helper及Host接线已实现，非作者源码窄审无P1/P2；candidate-owner-admission-sdk-r2 两架构实际编译、各8项针对性CTest及独立SDK安装通过，其中本夹具各执行33个输入及4个所有权例。匹配LLVM消费者在私有非输入desktop完成真实bootstrap/probe、32次预热及1024次4KiB异步echo、保存和正常关闭。该正常路径不代表真实导航重入/完整错误路径、物理性能或生产资格。

## 背景

`native_post_message` 在 Engine owner 复制 Miniblink 临时字符串为 owned request，执行原有 context 检查后完整解析 JSON，读取 requestId、method、timeout。普通异步请求随后通过 `jsEval` 建立页面 resolver，再调用 `try_enqueue_bridge_message`；后者完整解析同一 request，再读取相同字段并接纳入队。

ADR0157 仅移除了 queued worker 为 Host bootstrap/probe bookkeeping 重复提取 method 的解析，未处理这两次 Engine 接纳解析。本次只消除第二次纯重复工作，不将其解释为已定位帧率或吞吐差异的根因。

## 决定

增加仅在实现目录使用的 `detail::miniblink_admission_metadata`。它绑定一份本地 owned request，并借用首次解析后独立拥有的 requestId、method 字符串视图，保存 timeout 和四态 route；不保存 JSON DOM、session/token 授权或跨请求缓存。该对象禁止复制和移动，只在同步调用栈存活。requestId/method 必须独立于 raw request 缓冲，并在 helper/impl 返回前保持有效、不改写。

Host 公共 enqueue 重载保持签名，仍走原完整解析。新增 private 非 virtual impl 和仅实现使用的 friend shim；公共头仅前置声明内部类型，不暴露 JSON 类型，不增加对象字段或 vtable。shim 只从绑定对象取得 raw request 和 metadata，不接受任意 raw/metadata 组合。

原生回调保留首次 owned copy 的时点，防止 opaque getter / `jsEval` 使 Miniblink 临时 char* 失效。首次 parser、ID/method、timeout 的错误顺序、同步 bootstrap 分支、原 receipt、gesture、resolver 创建与 `jsEval` 后所有 context 检查不变。把本地 `const std::string request` 改为可移动的 owned string；只有最终入队调用才移动它。之后原回调的错误、诊断与返回只依赖独立 requestId、receipt、admitted 和 waiting，不使用 moved-from raw。

private impl 先执行原 context/accepting/queue/size guards。无 metadata 时完整执行原 bounded parse/field extraction；有 metadata 时只复用同请求投影。后续 generation、lane、lease、duplicate/capacity、steady deadline 起算、reply credit、worker post 与所有 context 检查保持原位。不能提前计算 deadline，不能将旧 metadata 重新贴上新导航 receipt。

异步 worker 和 resolver 不捕获 metadata 或 string_view。impl 必须在返回前将 requestId 复制到原拥有字段，worker 仅保留原 owned raw、ID、四态 route 和原生命周期捕获。完整 `bridge_adapter::handle_json` 继续验证协议、类型、origin、session、token、权限；metadata 与 route 都不授权。lease 仍在 JS 回复重入之前释放。

## 埋点与兼容

原serialization scope只测第二次接纳parse，首次native parse尚未覆盖。本次不移动埋点：public fallback保留原parse及scope；native复用分支移除第二次parse和围绕该工作的scope，保留的首次native parse仍未由这个metric单独测量。trace_duration本来无条件取QPC，工具observation/input-trace off也不关闭默认scalar，不能宣称off路径零计时开销。旧新样本数减少或零值不代表全部JSON CPU成本消失，也不能直接解释为FPS收益。

不更改 Bridge 协议、SDK公开调用参数、WKE ABI、队列容量、输入/FIFO顺序、shared budget默认0、provider机会或frame策略。首次raw copy不能省略；最终move只转移SDK已有所有权，不改变页面可见异步语义。消除解析会自然消除其分配失败时点，不为保留无用失败点而重新分配DOM；其余原异常映射与资源退役保持。

## 备选方案

- 使用原JSON子串/SAX新扫描器：可能改变转义、重复键、深度和类型语义，本次不采用。
- TLS隐式缓存、全局缓存或凭requestId命中缓存：增加重入/代际和请求配对风险，不采用。
- 异步保存整个DOM或借用string_view：增加内存或悬空风险，不采用。
- 同时删除 json_bridge / transport 的协议或权限解析：扩大安全边界，不采用。
- 抢占业务JS或把provider全量临时对象清理直接切片：寿命和执行语义独立，本次不涉及。

## 验证与边界

私有 helper 夹具逐字节提取当前 Host 的 bounded parser、packet_string、timeout helper，核对已接受请求的投影与旧重复解析结果，覆盖转义、重复键、类型、64KiB、512字符串、深度、timeout边界。另覆盖视图无分配、禁止复制/移动/临时字符串绑定、短/长raw最终move后独立ID/method仍有效，以及接收owned raw后抛异常时调用栈视图仍有效。

该夹具不模拟 Host 并发，不证明原生 `jsEval` 导航撤销、worker/Engine credit、关闭或权限接线。Host 接线完成后必须独立核对真实 guards 和捕获，沿用受影响的实际容量/旧context合同；若需新真实功能轮，必须绑定新SDK/EXE而不能继承旧route收据。所有未执行项继续pending，不以少一次parse直接承诺FPS或输入到光子改善。

## 回退

可撤销native metadata分支，恢复public enqueue的第二次完整parse；首次owned raw、原错误顺序、context/lease/credit/deadline和权限处理保持。旧证据不得改写为本次验证。
