# ADR-0157：MiniBlink 排队请求复用已解析的方法分类

状态：已接受设计，实施与验证进行中；不代表性能或生产资格。

## 背景

MiniBlink 的 `try_enqueue_bridge_message` 已对有界请求解析 JSON，读取 requestId、method、timeout，选择容量通道并保留原 deadline。worker 调用 `handle_message` 时再次解析同一原始请求，仅用于决定 bootstrap 的会话清理/提交以及 probe 的就绪后处理。实际 `bridge_adapter::handle_json` 随后仍独立处理协议与授权。

这次重复解析在既有基线也存在。本决定消除一项可明确识别的重复工作，不将其解释为已定位任何候选吞吐回退；既有负样本、异时间窗口的组件交叉结果和未关闭性能门槛继续保留。

## 决定

增加仅供实现使用的 `detail::miniblink_request_route`，底层类型为 `std::uint8_t`，四态为 `unknown`、`ordinary`、`bootstrap`、`probe`。分类函数接收已解析的 `std::string_view`，无分配、精确比较完整方法名。它不解析 JSON，也不是权限凭证。

admission 使用原 bounded JSON parser 和原 `packet_string` 得到的 method，完成原校验后分类。worker 按值捕获这个小分类，与同一拥有所有权、入队后不再改写的 request 一起存活，不捕获 DOM、string_view、额外 method 字符串或 session/token。

`dynamic_miniblink_adapter` 增加私有、非 virtual 的 `handle_message_impl`。公共/direct 两个 `handle_message` 签名保持，均传入 `unknown`；在原第一道 accepting/context guard 和 Bridge 空值检查之后，unknown 继续通过原 bounded parser 分类，再执行原第二道 guard。已分类 worker 仅省去这次方法提取，第二道 guard 仍保留。

分类只替代 bootstrap/probe 两个后处理的字符串比较。原 `handle_json`、成功 bootstrap 响应解析、pending session 清理/提交、probe 成功判定、导航撤销、锁和异常所有权保持。generation/context receipt、队列 lane、lease、reply credit、过期检查与原 deadline、容量先释放再递送 JS 的顺序不改变。

同步 native bootstrap 继续走 public/direct unknown 路径。本次不再从 JavaScript binding 穿透另一个捷径，不缓存授权解析，不改变 Bridge 协议、业务方法、渲染或帧策略。

## 兼容与迁移

公共头仅前置声明有固定底层类型的私有实现枚举，并声明 private 非 virtual 方法；不增加对象字段或 vtable 项，不增加消费者配置。已解析分类不是可供外部指定的公共参数。SDK 按既有源码构建/版本规则更新，调用方无需改业务代码。

枚举值本身一字节不代表闭包总大小只增加一字节，编译器可能添加 padding。不得为保存旧分配失败时点保留无用解析；真实 Bridge 异常仍由原 worker 规则转换和退役。内核源码与 ABI 没有本次变更，不因这一私有 SDK 改动重编内核或替换冻结 DLL。

## 备选方案

- 保留 worker 的完整 DOM 重解析：语义直接，但重复分配和遍历请求，拒绝。
- 捕获完整 method 字符串或整个 DOM：多余所有权与内存，拒绝。
- 搜索原始 JSON 子串或使用不同的快速扫描器：可能改变转义、重复键、嵌套和 NUL 语义，拒绝。
- 同时消除 Bridge 授权解析：扩大安全边界，本次不采用。
- direct 入口默认 ordinary：会跳过原解析与上下文检查位置，拒绝；unknown 必须与 ordinary 区分。

## 验证与边界

分类器契约测试从当前 Host 源码逐字节提取 `bounded_bridge_packet`、`packet_string`、`bridge_request_method` 作为旧方法提取语义来源，输出源/片段 SHA，不复制 Host 生命周期函数冒充真实接线。覆盖有效方法、JSON 转义、重复键顺序、嵌套伪 method、前后缀、嵌入 NUL、类型错误、512字节边界、64KiB边界、深度上限及非法包。检查分类无分配且 unknown 不会由已解析方法产生。

这些测试只证明方法提取和分类边界。实现后须另由非作者审查实际 Host guard、bootstrap清理、probe与原生命周期，并用匹配新 SDK 的真实 Host 完成一次 bootstrap/probe/异步 echo/保存/自然关闭功能验证；旧receipt、reply容量和关闭拒绝沿用适用真实契约。不能仅凭 classifier 或源码提取测试宣称已覆盖 Host 并发。

性能收益需绑定新载荷与预定对照，不由少一次 parse 直接承诺比例或 Electron 对齐。没有执行的测试、真实输入/物理呈现及性能资格仍 pending。

## 回退

可撤销 worker 分类捕获和私有 impl 捷径，让 queued 调用重新进入原 public unknown 路径；保留原授权、清理和期限。回退不修改既有性能证据，不把未验证状态改为通过。
