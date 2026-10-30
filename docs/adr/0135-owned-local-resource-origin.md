# ADR 0135：MiniBlink 显式本地 HTTPS 资源原点

- 状态：源码及回归源码已接线，Host、资源读取与 Native 已获独立静态审查接受；尚未统一编译或运行验收。
- 日期：2026-10-27。
- 范围：MiniBlink 静态应用资源；不增加 HTTP 监听服务、远程调试或通用代理。

## 背景与真实失败

现有 MiniBlink 业务入口通过 `wkeLoadFileW` 加载。集中验证夹具的两个相对资源 URL 因而解析为 `file:///`。当前 FetchManager 在创建网络任务前拒绝原生 `fetch(file:)`；XHR 可以读取本地文件。这不是 DevTools 丢失 fetch 事件，但现有两个 URL × XHR/fetch 的四请求验收尚未满足。

同轮发现本地文件成功回调把 UTC 秒传给要求单调秒的完成时间，Network 显示约 20753 天。该问题另按四处完成时间调用修复，不能用前端截断、归零、改全局时钟或放宽阈值掩盖。

现有 `wkeOnLoadUrlBegin/SetData/Cancel` 不能完整支持本合同：零长度数据会变成空格；Hook 固定 200；取消可跳过全部完成回调，完成通知也早于缓冲析构。不能据其 End/Fail 回调回收信用，或把 SDK 回调数为零称为 native 排空。终身累计少量请求虽有界，却不能支撑长期负载，不作为实现。

## 选择与兼容性

采用独立的 business snapshot 和 native memory loader。它在进入 curl manager 前处理本地资源，使用独立 owner、状态、限额和私有能力 ABI。

- 保留 `dynamic_miniblink_adapter()` 和 `dynamic_miniblink_adapter(diagnostics::mode)` 的现有行为与符号，默认仍为 file。
- 不修改公开 `host_config`、adapter 数据成员布局或虚函数表。新增独立 options 类型及一个明确的双参数、非 virtual 构造重载；避免改变既有单参数构造的重载解析。
- 模式由原生应用构造时选择，与 `allow_native_devtools`、网页、环境变量和 CLI 请求分别处理。资源模式不授予调试许可。
- 模式和状态放入已有 `event_lifetime_` 间接 owner；mount 重建该 owner 时只继承不可变模式，重新建立资源及文档身份。
- AutoHost 默认行为本轮不变。新模式遇到缺少 native 能力的旧内核明确返回 unsupported，不静默回落 file。
- 独立 business-resource ABI 1 必须先探测尺寸、版本和能力；现有 inspector 控制 ABI 3、资源 ABI 2、nonce 域及限额保持不变。

## 资源与 URL

SDK 在文档加载前从应用提供的 `resource_root` 准备不可变静态资源 snapshot；响应回调只查内存，不打开磁盘文件。

业务 snapshot 沿用现有 Web 构建边界：单文件最多 16 MiB、树总量最多 128 MiB、最多 4096 文件。目录数、深度、路径及元数据亦设固定上限。读取必须保留 plain-file、祖先目录、最终路径、重解析点、完整 EOF 和稳定身份检查；URL 只查 snapshot 的规范相对 key，不能直接拼接任意磁盘路径。复用读取原语，不复用 inspector 的策略对象或扩大其 1 MiB 单项上限。

本轮新模式的准备 root 限定原始绝对本地 fixed/RAM 磁盘路径，不接受 UNC、设备命名空间或带点段的 root；默认 file 模式保留旧行为。资源相对 key 为 ASCII 字母、数字、`_-.` 和层级 `/`，大小写精确，单段最多 128 字节、路径最多 1024 字节、32 层；目录最多 4096（包括 pin 的 root 祖先），累计名字最多 8 MiB。非 ASCII 的父目录仍使用 Windows Unicode 路径处理。这些是新模式明确的兼容边界，不把其他目录或 URL 别名静默归一成受信成员。

每个 mount 使用不可预测的独立 Host 主机名，每个文档使用独立路径 nonce，例如 `https://<hostNonce>.soui-local.invalid/<documentNonce>/index.html`。具体拼写在实现接口冻结时统一；nonce 不是调试授权。

- Native owner 绑定真实 view 出生身份和文档 receipt；request metadata 不能由网页伪造或在复制时升级为新代际。
- 本保留命名空间的无 owner、跨 Host、旧文档及非法路径请求在 curl 前失败，不发生 DNS、连接或网络 fallback。
- GET 返回真实 MIME、状态、正文和单调时间；零字节资源必须精确保真。不存在的资源返回明确的本地 404；非法请求和不支持的方法明确失败。
- 不关闭 CORS、不增加任意原点许可，也不复制 inspector 的 CSP。应用自己的页面策略仍有效。
- 该原点提供静态 GET 响应，不提供 WebSocket、SSE、Beacon/Ping、外部下载或插件网络通道；这些入口在创建异步对象前明确拒绝保留域。可选原生媒体 provider 也不能取得该域的 URL：在交两个外部 hook 前报告 FormatError，不把它宣称为已有本地音视频 provider。普通外域、file、Blob 与 data 继续各自原策略，opaque URL 的正文不能被当成资源 authority。
- 初始入口、导航、Bridge bootstrap、main-context ACK 和 reload 一起按精确 entry、view 及当前 document receipt 校验；只比 Host 不足以授予 Bridge。
- 原有 file 信任路径保留。新模式只允许声明入口及既有允许的 hash 导航；query、路径别名、编码逃逸等不能扩大资源集合。
- 源诊断仍只接受有身份与容量校验的受限 inline map；distribution 仍不携带业务源码。不新增临时 HTTP 服务或 map 网络 fallback。

## 有界 native 所有权

准入上限在实现前冻结：每 owner 最多 64 个活动操作、64 个待办及 128 MiB 被借用的响应字节；进程最多 32 个未释放 owner、512 MiB business snapshot payload。Inspector 另计，不能借用其额度。额度在终态按真实所有权归还，不使用终身累计替代，也不通过新建无界队列等待额度。

SDK 的准备阶段同样计费：开始准备前取得进程内最多 32 个准备/存活 snapshot 信用，闭集文件元数据确定总字节后、分配正文前取得 512 MiB 字节信用。拒绝不等待；失败归还信用，成功后随实际最后一份 immutable snapshot 正文释放归还，shared 引用不重复计费，禁止隐式深拷贝绕过账目。Native bind 保留独立的 owner/正文账目，不能用其较晚的准入代替准备阶段计费。

XHR 在 loadstart 前、fetch 在创建 loader 前、普通子资源在确定实际加载后预留同一个 typed admission cell；实际 memory loader 恰好接管一次原 slot。缓存命中不新增额度，未转移请求取消归还，关闭认识未创建 loader 的 reservation；跨线程复制不复制授权。Ping、外部下载、原生 URL request 和插件入口亦须在可返回错误的前置位置拒绝本保留命名空间，不能依赖 void loader 的同步 `didFail`：该回调可能早于 XHR 订阅或重入 Ping 构造函数。

Native loader 参考现有 memory-load 机制，但使用独立 business 状态与暂停闸门。操作持有真实 owner/epoch、活动 lease 及 callback/bytes borrow。成功、失败、取消和重入关闭都必须有终态；清理发生在实际最后一次借用退出后。

单请求额度不足在前置入口明确拒绝，记录累计 `denied`，不把该请求当成功，也不把它误报为整个 owner 的致命故障或取消其他已接受请求。Provider 与原生 dispatcher 故障则保持 owner 的 sticky FAILED；重复查询、迟到消息或后续物理清理成功不能把故障漂白。

系统 `PostMessage` 失败时保留已准入的原有有界 task、client 和 lease，不同步 `didFail`、不回滚成已完成、不添加轮询 timer。Owner 显式 FAILED，关闭新准入和 dispatcher callout；SDK 的既有观察与 UI pump 随后撤业务 scope、进入原 Host 关闭流程，真实取消原待办并通过 native/broker fence 排空。故障仍按原期限记录，不能把窗口消失、取消后的物理回收或失败操作计为成功。

## 暂停与两阶段退休

当前 debugger 暂停循环会派发 Windows 消息；不能直接复制 inspector 的 message-only window/timer 作为业务 dispatcher。业务待办必须检查真实 native pause depth，暂停时保留在有界容器内，不反复投递 timer，不泵业务 FIFO；最外层恢复仅唤醒一次。

退休使用独立的 native bind/close/query 操作及带身份的 receipt，具体 C POD 声明先由唯一接口作者冻结：

1. 先撤销业务 scope，并关闭新导航、lookup 和资源准入。
2. 取消或丢弃有界待办，阻止新的客户端 callout；已进入的 callout 栈保持 lease，关闭返回 pending。
3. 活动操作、正在执行回调、待办及 payload borrow 全部为零后，在 Engine owner 上显式释放 payload/callback owner，发布 `PayloadRetired` receipt。
4. 然后才继续原有 view 物理销毁和 Host/Runtime fence。

不能等待所有 loader 对象或 state shared_ptr 析构后才允许销毁 view，否则可能与 Document/MemoryCache 形成环等。完成或取消的 loader 可保留关闭后的 identity，但必须清空 client、loader、bytes 和 payload owner，不能再次 lookup/callout。持有关闭 identity 不等于仍借用 payload。

reload 同样先退休旧 owner，再发布新文档 nonce 和可信入口；旧 metadata 不能改绑新 owner。准备、切换及退休的并存 owner 数有界，pending 时不能不断创建后继。调用方保留原总期限，UI 不同步等待排空；超期或失败仍保留诊断和必要物理 owner。

## 备选方案

- 继续 file 并删掉 fetch：不满足已冻结夹具，不采用。
- 只换 URL/base URL 或启动本地 server：前者没有受控响应，后者增加不必要服务与依赖，不采用。
- 旧 Hook 加累计请求数：缺少空文件、状态及可回收终态，不采用为生产合同。
- 对普通业务复用 inspector snapshot：角色、限额、CSP 和授权边界不同，不采用。
- 给公开配置追加字段：会改变已有 C++ 布局；显式构造重载足够，本轮不采用。

## 实现与验证责任

公共 options/构造声明及本 ADR 由 Root 唯一维护。Native owner、ABI、memory loader、request metadata、暂停/恢复与 broker fence 由一个作者负责；SDK Host/Bridge/reload 接线由另一作者负责；资源读取与闭集由独立模块作者负责。共享头和共享生命周期文件不得并行覆盖。

先完成所有代码、针对性回归源码及非作者静审，再统一编译和实测。至少验证默认 file/旧构造、显式模式、双 Host/旧文档隔离、路径与链接拒绝、容量与取消、空资源/404、四个真实 XHR/fetch、SourceMap/断点、暂停及重入关闭、循环信用回收。Native 排空必须有 receipt 与实际资源证据，不能仅凭窗口消失或进程退出。

修复只重跑受影响项，保留本轮原失败及旧候选身份。该决定不授权公开分发、不关闭法律或整体 SDK release 门槛，不自动通过 D01–D14。
