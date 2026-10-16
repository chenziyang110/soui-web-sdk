# ADR-0082：私有存储请求的独立读端解释

状态：proposed。本切片仅 Source 与既有纯 JS 测试；qualified=false、releaseReady=false。

## 背景与决定

ADR-0078 已沿既有 opt-in 分支序列化实际 getter status 和独立私有 DTO。当前基线 67a4fe2 的两架构九项回归共 18 项通过，只证明这些 Native 契约；长期/100 reload 实际采集与完整 SDK observer、预算、兼容性和发布仍待验证。本切片复用四个既有读端，不改 C++、ABI、DTO、getter、collector、argv 或采集入口。

读端先验证既有完整 Source、payload、SouiAcceptance.exe、consumerBuildId、Owned PID/FILETIME、原始事件日志与 Owned/context 原始日志绑定。SDK 请求属于固定消费者 EXE，node.dll 的命名缓存 pin 不能替代它。保留完整原始事件作为 allSamples，位置、phase、cycle、window 和 mount/close 生命周期随每个原 DTO 保留；旧 OS 资源计算仍使用原 sample 子集。

仅规范十进制 U64 字符串、严格 bool、schema=1、精确 scope 可解释。ready 的 coverage_mask=7 与 gap_mask=31 是位掩码，分别指三请求域与五个未覆盖范围。三个请求域必须来自该 DTO 的同一 Store observation。ready、有效 clock、非饱和且相应 exact flags 为真时才派生；busy、disabled、unavailable、缺失、默认零、饱和与退休副本保留原值及原因，derived=null。已知 shape/accounting/epoch 矛盾标 invalid；绑定或可用量缺失保持 partial/not-evaluated。

current 和累计成功/失败请求检查 aggregate=maps+private strings；累计成功 allocate/free 与 current 的退款关系也校验。事件 aggregate high-water 直接保留，不能把两个域的不同时间峰值相加。每次 mount 建立独立生命周期，Store epoch 在该活 owner 内固定；它不必等于 bridge/service/renderer epoch。仅同 epoch、同生命周期、有序 clock 做累计差分。相同副本不算 fresh，退休副本不派生、不作析构后零断言，也不能填补校准点。

100 reload 只收 baseline cycle 10 加 100 measured cycles 的 101 个原位置，每个位置保留全部 view 与 nullable 差分、事件 peak 和缺失/busy/invalid/retired/重复计数。runner 从同 allSamples 和同完整绑定复算新结果并深比较；既有 ownerCaps、namedCaches、资源预算、policy、自然 Job 与 argv gates 原样保留。私有结果是独立部分观测，不增加生产预算准入。

## 边界、备选与验证

不复用旧 taskStoreStorage mask=3/gaps=63 拼原子快照，不从公开字符串 capacity 推 allocator 请求，不从退休快照猜最终退款，也不将 node.dll 身份借给 SDK。拒绝从这个部分范围推经验阈值、heap 字节或 quota：fullCacheBudget/calibratedByteBudget 均 not-evaluated；configuration/budgets/quota/calibrationPin=null。

迁移为既有 opt-in 报告增加相邻独立结果，默认路线不增加新准入条件。四个既有测试文件覆盖规范 U64、busy/饱和、账本和峰值、生命周期/退休副本、绑定与 aggregate 篡改、101 点缺失。纯测试的 synthetic DTO 只证明读端分类；真实 getter/serializer 的既有 Native 证明来自 Root，真实长期采集不由本切片替代。
