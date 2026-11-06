# ADR 0155：复用 WebView2 请求的有界调度元数据

状态：实现中。日期：2026-11-02。范围：SDK 私有 WebView2 request admission/worker，不改变公共 Bridge 协议、授权、线程或回调顺序。

## 背景

`receive_message` 已对原始请求执行有界 JSON 解析；worker 在接纳、文档和期限检查后又解析同一文本，仅用于 method/sessionId 的调度检查。此重复解析创建完整 DOM，与业务 Bridge 必需的协议解析无关。

## 决定

从 ingress 已有 DOM 捕获私有 immutable metadata。method 最多缓存128字节、sessionId 最多缓存256字节，保留 missing/string/invalid 状态；任一合法字符串超过缓存范围时，不复制它，worker 回到原 bounded parse 路径。最多257个当前代接纳槽，每项新增字符串内容最多384字节，当前代内容上界98688字节；另有一次ingress临时与导航后正在处理的至多一个旧代请求，以及固定对象、string capacity和allocator开销。这是有界缓存内容成本，不是既有请求byte计账，也不是总进程驻留内存上限；完整内存仍需实测。不扩大请求槽或合法消息上限。

每字段只在旧代码本来读取它的位置抛出错误。bootstrap 不读 sessionId；无 session 的普通请求先拒绝；过期请求仍先返回超时。metadata 缓存分配失败只放弃缓存，原接纳仍可继续。不存在由缓存授予的权限。

原始 raw、document/generation、业务 deadline 和 revoke barrier 保留。Bridge `handle_json` 的协议、类型和授权检查仍执行；response 解析、COM 前 credit 释放和最终来源检查不变。不把此改动描述为 WebView2 原生输入路由优化。

## 备选与兼容

保持重复解析最简单但保留已知冗余；保留整个 JSON DOM 会扩大驻留成本与所有权；raw 字节切片不能直接提供 JSON 解码后的字符串。选择小型有界缓存与原路径回退，不新增公开虚函数或第三方依赖。

## 验证与回退

差分测试以旧 `packet.value` 访问行为为基准，覆盖缺失/错类型、bootstrap、已有/未建 session、过期优先级、转义文本、缓存边界与回退。编译真实 WebView2 host 并执行受影响容量/生命周期测试；CPU/输入/帧率收益待同负载实测，不能从减少一次解析推算 FPS。

回退只删除私有缓存接线，恢复 worker bounded parse，不修改授权或错误语义。默认候选路径需正确性和受控对照完成后决定是否采用。
