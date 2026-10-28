# 导入之后的 Miniblink 修补

基线为本 SDK 的 `2d09a2ff` 中 `third_party/sources/miniblink`，其上游来源仍由 `SOURCE-MANIFEST.json` 和原有 `LOCAL-CHANGES.patch` 记录。历史导入清单不改写成后续补丁的验证结果。这里按顺序保存导入之后的源码和动态绑定头补丁；当前交付已经包含这些补丁，重新导入时先恢复原有本地改动，再依次应用。

| 补丁 | 原因 | 当前验证状态 |
|---|---|---|
| `0001-release-validation-tooltip.patch` | 每个 WebView 创建普通及表单校验两个 ToolTip，析构仅释放普通实例。公共消费者连续销毁重建时观察到数百个 `MbToolTip` 窗口残留。补齐校验提示对象释放。 | x64 早期重编译及三模式各 30 轮通过；后续与其他补丁组合进入 preview.4 固定 DLL。当前固定包的长循环资源门槛仍待完成。 |
| `0002-release-draggable-region.patch` | 每个 WebView 的 `CreateRectRgn` 结果 `m_draggableRegion` 未在析构释放，与首个补丁后每轮 GDI 增 1 一致。补齐对应 `DeleteObject`。 | 在补丁 0001 之后应用；x64 重新编译并完成重建 30 轮，USER/GDI 增量均为 0，普通句柄仍增 30。更长运行及 x86 待完成。 |
| `0003-close-thread-wake-event.patch` | 非主线程创建唤醒事件，析构已等待工作线程并关闭线程句柄，却遗漏事件句柄。在线程真正退出并 join 之后关闭事件。 | 三补丁组合已完成 x64 全依赖重编译及三模式各 30 轮。重建普通句柄增量由 30 变为 0，USER/GDI 同样无增长；x86 及长循环待完成。该源文件含旧编码，使用 ASCII 上下文补丁应用，保持其余字节不变。 |
| `0004-align-dynamic-binding-bool-abi.patch` | 交付的动态绑定头把 54 个实际导出为 C++ `bool` 的返回类型声明成 `BOOL`，x86 因返回 ABI 不一致把 Promise 误判为字符串。按同一内核源码的 `wkedefine.h` 对齐全部共享符号。 | ABI 契约测试覆盖全部共享返回类型；修复后的 x86/x64 公共消费者 reload/recreate/multiwindow/bridge 各 3 轮通过。根因、失败实验和诊断链见[归档](../../../verification/production-validation/x86-bool-abi-root-cause/README.md)。 |
| `0005-gpu-surface-lifecycle.patch` | GPU 表面在合成线程创建，却原先在该线程退出后于 UI 线程析构；`GrGLCreateNativeInterface()` 返回的引用也未释放。把表面释放移回合成线程，并补齐 Skia 接口引用释放；保留可选关闭诊断。 | x86/x64 历史候选 GPU 重建 30 轮正常退出但 USER 对象仍逐轮增长；x64 软件对照的句柄和内存在 100 轮出现回收，不能把其 30 轮端点误判为线性泄漏。详情见 [GPU 生命周期记录](../../../docs/acceptance/gpu-lifecycle.md)。修补已进入 preview.4 固定 DLL，GPU 仍是实验模式。 |
| `0006-verify-tls-peer-and-host.patch` | Miniblink 网络句柄原先关闭 TLS 证书链和主机名校验。移除不安全默认值；允许宿主选择信任根后，强制启用链和主机名校验。 | x86/x64 重编译候选及 preview.4 固定 DLL 均完成回环 HTTPS 各六项，包括错误证书、主机名不匹配、重定向和回调试图关闭两种校验；公共消费者四模式短回归通过。[固定包原始证据](../../../verification/production-validation/preview4-fixed-2026-09-23/README.md)保留。信任根交付和旧 TLS 组件维护仍未验收。 |
| `0007-mark-utf8-source-encoding.patch` | 独立源码包使用 LF 行尾，四个无 BOM 的 UTF-8 内核文件在代码页 936 下出现 C4819 和 `TileGrid` 编译错误；Skia 另有同类警告。仅增加五个 UTF-8 BOM，保留正文原始字节和其余旧编码源码。 | 补丁可在仓库根目录正向及反向校验；双架构底层重编与实际启动的结果见[独立源码编码复测](../../../verification/production-validation/preview6-relocated-dependency-encoding-2026-09-25/README.md)。未替换固定 DLL，生产门槛不变。 |
| `0008-bound-ready-task-priority-wait.patch` | 连续动画提交可使严格低优先级的已就绪解析任务长期饥饿。提取实际选择策略，对等待达到 50 ms 的就绪任务按到期先后优先，保留未来任务资格和退出原规则；50 ms 不是执行时延保证。 | 两架构策略测试先失败、后各十二项通过，补丁正反向检查通过。[x86 完整依赖重编与原动画实测](../../../verification/production-validation/scheduler-fairness-x86-rebuild-2026-09-26/README.md)均退出 0；冻结消费者只替换内核后任务和关闭完成。x64、完整应用及目标系统未验证，未替换固定 DLL。见 [ADR-0016](../../../docs/adr/0016-scheduler-ready-task-fairness.md)。 |

补丁前文件原始字节 SHA-256 为 `d9c3a403e43493cdc13344781eb680c838ab68695e7c2119829ecb11313662da`；新源码由 SDK 的全源码清单记录。修改原生依赖后，必须以隔离目录的新 DLL 运行既有复现和真实应用，再更新预编译依赖身份、来源说明及许可核查对象。

2026-09-27 更新：0008 的 [x64 完整重编及原动画复现](../../../verification/production-validation/scheduler-fairness-x64-rebuild-2026-09-27/README.md)也退出 0，两架构候选内核现已导入 preview.15，来源记录为 `third_party/manifests/kernel-build-preview14.json`（保留首次导入候选名）。当前 SDK 四配置编译通过，完整应用运行、Win7、交互和资源门槛仍待验收；表中各次旧结果保留原执行范围。

不根据缺少一行 delete 就宣称所有内存或句柄增长均已解释。普通句柄和 GDI 的剩余来源仍须对照修复后逐轮数据；进程结束清理不能代替窗口级资源回收。

三个补丁的完整组合现已完成 x86/x64 全依赖重编译和公共独立消费者三模式各 30 轮。x86 重建普通句柄保持 294、GDI 保持 55、USER 保持 9；x64 分别保持 280/55/9。[逐候选记录](../../../docs/acceptance/lifecycle.md)保留完整内核散列与 run-id。表中单补丁的历史验证边界不因此改写；组合候选仍需完整压力、真实应用和依赖更新验收。

## 0.5.0 源码采用：0009

`0009-0.5.0-native-runtime-diagnostics.patch` 记录干净仓库基线 `0201e30b` 的 Native 子树到当前固定源码的变化：44 个已有文件修改、7 个文件新增，51 个补丁路径均相对仓库根目录。内容包含此前形成的 Worker/MessagePort 生命周期、线程退出、原生诊断与构建兼容修正，以及最后生效的 `Release|Win32` 静态 libcurl 声明修复。既有 0001–0008、`SOURCE-MANIFEST.json`、`LOCAL-CHANGES.patch` 和表中的历史验证说明保持原字节。

完整固定源码边界为 23,543 文件、1,330 目录、321,004,079 字节；外部原始源码清单 SHA-256 为 `426b5d537bb294f984d896c84e90cd8f5d4875ff1b7fc0910dca40f91f8aca0f`。采用仅复制这 51 个实际差异文件，23,492 个既有未变文件没有重写；采用后全体文件、目录集合及原始散列与该边界相同。补丁为 2,433,999 字节，SHA-256 为 `ba60b25f036f6103bf15e493e75492374d89ca8617f96dd70fc742d29b5be303`。

对序列化 unified 补丁按文件做正向与反向原始字节重构，51 个结果分别匹配目标与采用前内容；保留 BOM、旧编码和行尾。此核对没有执行 Git 应用。维护者可在仓库根目录对采用前基线运行 `git apply --check third_party/patches/miniblink/0009-0.5.0-native-runtime-diagnostics.patch`；当前已采用树使用同一路径的 `git apply --reverse --check`。正常重放仍须固定原有补丁和基线，不能把不同来源的工作树当成同一补丁前件。

此次采用是源码变更，没有替换预编译 DLL/LIB、更新旧二进制身份或把历史诊断升级为当前验收。当前两架构的完整重编、实际 ABI/应用/长期资源、目标系统和许可发布门槛仍须按各自原件核实，不能从诊断接口存在或源码散列一致推导通过。接口与迁移边界见 [ADR-0068](../../../docs/adr/0068-native-runtime-diagnostics.md)。

## 0.5.0 默认字体名称修正：0010

`0010-default-font-unicode.patch` 在 0009 之后应用，只修正 `WebPageImpl::initSetting` 的默认字体字面量。原字面量含乱码且显式取前两个字符，实际 x86 诊断观察到请求 `U+7039/U+5B29`，而系统字体名称为“宋体”（`U+5B8B/U+4F53`）。字体族比较因此拒绝该对象，后续在 `ComputedStyle::computedLineHeight` 读取空字体时出现访问冲突。

新字面量使用 Unicode 转义，明确保留两个正确字符，不依赖源码中文编码。没有增加空字体兜底或修改比较规则。该修正尚待重新构建并复测原 `reply-full-zero-business`、清理及 ABI 用例；后备字体路径仍需单独核实。上节的固定源码和原始散列仅对应 0009 采用时点，本次后续变化由顶层源码清单记录。

## 有限命名缓存观测：0011

`0011-finite-cache-observation.patch` 以已包含 0010 的 `18da0e9` Native 子树为前件，记录 10 个已有文件的新增观测接线和 2 个新头。它新增独立 216 字节 cache snapshot/table，不改变原 renderer 168/264 字节 ABI；通过原 WKE ITERATOR 导出机制暴露可选 `wkeGetCacheDiagnosticsApi`。Blink 仅 owner-thread 读取现存 MemoryCache；Skia 仅对已发布 attached glyph/global retained resource 实例 try-lock，不触发 lazy-create、遍历、purge、消息消费或 limit setter。既有锁 acquire 算法保留，仅增加非阻塞 try 方法。

策略字段来自当前组件实际配置，不能解释为全部 heap 硬上限。detached/TLS/外部借用对象、其他缓存实例与 allocator 不在范围。SDK 显式异步请求、完成副本、缺失/忙/不可用以及原采样时间见 [ADR-0070](../../../docs/adr/0070-finite-cache-observation.md)。`git apply --reverse --check` 已对当前工作树进行非写入检查；Node 纯数据/源码检查不证明 Native 编译、真实缓存行为或长期资源验收。所有旧补丁和历史结果保持原角色，新 Native 实际验证尚未执行。

## 驻留资源记录字节预算：0012

`0012-retained-resource-byte-budget.patch` 在 0011 后只改变 Blink 初始化、SkResourceCache 的正字节限制及对应注释三个文件。原始字节重放已核对，不转码上游文件或转换行尾。前后完整源码树身份、三个文件散列和补丁散列见 `third_party/manifests/kernel-cache-source-050.json`。历史 SOURCE-MANIFEST 保持原导入角色，当前编译身份由实际构建前后完整清单记录。策略及真实像素引用验证见 ADR-0102；完整新内核和应用结果另计。

## 主线程调度批次：0013

`0013-bound-main-thread-scheduler-batches.patch` 以 `dc1e5bca0f6467f3481c15d8f5f54dab7fa69d06` 为前件，修改 `WebThreadImpl.cpp`、`WebThreadImpl.h` 和 `BlinkPlatformImpl.cpp`。主线程正常调度在完成 32 个任务或超过 2 ms 协作预算后返回消息泵；单个回调、观察器及任务入队仍可能超过该预算。保留原 16 ms 共享定时器继续处理剩余任务，工作线程保留 50 ms 预算。平台关闭中的三次显式调度改用 `fireForShutdown()`，绕过正常主线程批次限制并沿用原 50 ms 预算；最终 `fireOnExit()` 清理保持原行为。批次之间的等待可能降低积压任务吞吐，策略和验收边界见 ADR-0118。

补丁已在隔离副本进行正反向原始字节重放，并在当前源码执行反向非写入检查。重放显式使用 `git -c core.autocrlf=false -c core.eol=lf apply`，避免全局换行配置转换验证副本。补丁及三个修改文件的前后散列见 `third_party/manifests/kernel-input-latency-source.json`；该记录不声明完整源码树与历史内核相同。

本次为未发布的源码候选。随仓库提供的 DLL、`kernel-build-050.json`、二进制许可记录及原生 SBOM 继续描述原有导入，不包含本补丁。完整内核重编、实际输入响应、积压吞吐和关闭验收分别记录；补丁重放不代表这些验证通过。

## 任务唤醒与有界接纳：0014

`0014-ready-task-wake-and-admission.patch` 以 PR109 的 `3293bebe27ba8508bbd9dfe8b6e0d445e7879f3f` 为前件，包含五个原始 vendor 文件的改动：SharedTimerWin、WebThreadImpl 头/实现、WebPageImpl 头/实现。新任务和就绪积压通过合并窗口消息唤醒，跨线程接纳纳入正常主线程预算；正延时按 post 时间计时，资源受阻的 commit 延后重试。原 timer、worker 吞吐预算、显式关闭排空和资源 fence 保留。

前后字节数、散列及补丁身份见 `third_party/manifests/kernel-ready-task-source.json`。此为源码候选；最终编译、补丁重放和运行交由 PR 验证人员完成，不引用早期实验作为最终通过结果。历史导入身份、固定二进制来源及生产门槛保持原义。设计和兼容边界见 ADR-0121。
