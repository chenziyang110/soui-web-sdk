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

不根据缺少一行 delete 就宣称所有内存或句柄增长均已解释。普通句柄和 GDI 的剩余来源仍须对照修复后逐轮数据；进程结束清理不能代替窗口级资源回收。

三个补丁的完整组合现已完成 x86/x64 全依赖重编译和公共独立消费者三模式各 30 轮。x86 重建普通句柄保持 294、GDI 保持 55、USER 保持 9；x64 分别保持 280/55/9。[逐候选记录](../../../docs/acceptance/lifecycle.md)保留完整内核散列与 run-id。表中单补丁的历史验证边界不因此改写；组合候选仍需完整压力、真实应用和依赖更新验收。
