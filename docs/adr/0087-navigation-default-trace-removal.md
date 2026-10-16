# 0087: 关闭生产导航路径的残留同步日志

状态：SOURCE_ONLY 候选。独立 Source 复核、新 SDK/MF 接入、真实重建、正常 14 项、100 次 reload 和 8 小时实际资源/流畅度验证均待完成。Native、SDK、normal、guest、reload 和长时结果为空，发布资格为 false。

生产 SDK 私有定义 SOUI_SOURCE_KERNEL_HOST；因此 allow_document_navigation 中的 legacy source-navigation.log 代码不是仅供外部 debug build 使用。每次导航都会同步打开资源目录父级文件并以 app 模式追加，缺少公开配置开关、容量上限和生命周期回收。被拒绝或超长 URL 也进入日志写入。这既产生导航路径上的磁盘 I/O，也能在重复 reload/长运行中无限增长。

以 sm05-terminal-reader-adopted-v1 的当前 Host 为基线，仅删除该函数中的六行条件 trace 块。函数继续原样返回 is_trusted_entry_url(url, resource_root_)；trusted_document.h、本地 file URL、index/react 文档、路径等价、Bridge 权限和导航回调均不修改。不替换成另一种日志，不新增公开 API、环境开关或 unknown-file 排除。fstream include 保留，因为其他 Host writer 仍使用它；本片不声称完成所有日志/资源域审计。

有限检查 Host 所有 ofstream 与 source-navigation 相关引用，生产 SDK 中仅找到这一处 source-navigation.log writer，没有 source-navigation-rejected.log 的独立生产 writer。后者存在于旧归档 smoke 验证：verify-benchmark-smoke.cjs:71-73 要求 allowed/rejected 日志；旧 acceptance runner、资源 binding 和 A/B draft 也有既有诊断排除。它们属于旧验证依赖，不是公开可配置日志契约。冻结旧文件不改；Root 后续应在新审查过的验证 Source 中固定无 trace 的完整 payload 成员并替换 marker 依赖，不能保留生产写入来满足旧 marker。

旧 Gold Source2 的 14 项和 native 自然关闭虽正常，严格 78-member inventory 因唯一 140-byte trace 而失败，其原失败保持。旧 Gold Source3 collector 将此已知文件严格分离后得到有限 normal 成功；它仍是 trace-enabled Gold 的真实历史成功，不变成无 trace 或当前 SDK 候选的验证。本片只引用这些已关闭记录，未重新运行内核、Native 或 SDK。

源码验证只删除精确 trace 块，再反向插入以重建原 Host 全部字节，确认判定 return、callee、callback 和其余源码不变。不复制一套 URL 判定作为镜像实现测试，也不把源码检查称为 Native 行为测试。新的真实 normal/guest collector 必须在新的 SDK/MF/producer 下明确要求 no-trace exact 78；正常 14、100 reload 和 8h 的性能、资源与自然关闭仍须真实记录。当前正在进行的 Source13 SDK 和 Task17 actual 不包含本候选修改。
