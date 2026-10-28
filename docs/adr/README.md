# 架构决策记录（ADR）

2026-10-26：[ADR-0122 输入到可见反馈](0122-input-to-visible-feedback.md)定义分阶段实现、接口、兼容和专人验证边界，当前不代表编译、运行或性能验收。

当公共 API、Bridge 协议、权限、线程与生命周期、兼容目标、SDK 包格式或核心依赖发生变化时，在本目录新增 ADR。

文件命名：`NNNN-short-title.md`，编号递增。状态使用 `proposed`、`accepted`、`superseded` 或 `deprecated`。已接受 ADR 不回写历史结论；后续变化新增 ADR，并在旧记录中标明替代关系。

从 `0000-template.md` 复制模板。PR 必须链接相关 ADR，并在合并前将已批准决定标为 `accepted`。
## 0.4 新增

- [ADR-0016：运行档位与可选现代渲染后端](0016-runtime-profiles-and-modern-backend.md)
- [ADR-0017：版本分支整合与固定 SDK 身份](0017-pr-stack-integration.md)

## 0.5 新增提案

- [ADR-0118：主线程定时任务批次让出输入处理](0118-main-thread-timer-batches.md)
- [ADR-0117：限定范围交付后的当前未验收状态](0117-current-unverified-acceptance-status.md)
- [ADR-0095：发布汇总的编译档位与冻结输入](0095-release-collection-inputs.md)
- [ADR-0096：SOUI 依赖构建的 LLVM 档位](0096-soui-llvm-dependency-build.md)（实施与真实回归进行中）

- [ADR-0066：版本化原生工具链输入与固定 WinRT 搜索根](0066-versioned-native-toolchain-inputs.md)（proposed；实际构建与验收仍待 Root）

- [ADR-0067：分别绑定 LLVM 项目与验证工具的请求](0067-owned-llvm-project-and-validation-requests.md)（proposed；源码和有限逻辑检查通过，实际工具准备与界面验收待完成）

- [ADR-0068：原生运行状态与兼容内核诊断](0068-native-runtime-diagnostics.md)（proposed；32 位修复正在重建，完整兼容性验收待完成）
- [ADR-0069：SDK 源码交付与验收入口](0069-sdk-source-delivery.md)（proposed；源码已采用，正式交付验收待完成）
- [ADR-0070：有限命名缓存观测](0070-finite-cache-observation.md)（proposed；纯数据检查不构成原生缓存或长期资源验收）
- [ADR-0071：长期运行中的命名缓存采样](0071-long-run-cache-sampling.md)（accepted；采集已接线，实际原生与长期验收待执行）
- [ADR-0074：有限命名缓存配置检查与重载校准采样](0074-finite-named-cache-validation.md)（proposed；经验字节预算与真实原生校准尚未完成）
- [ADR-0075：TaskStore map 分配请求观测](0075-task-store-storage-observation.md)（proposed；Source-only，未编译/Native/实际资源验收，完整缓存预算仍未评估）
- [ADR-0076：直接原生任务输入的准入上限](0076-native-task-input-admission-bound.md)（proposed；Source-only，未编译/C++执行；逻辑输入限制不等于完整字节预算）
- [ADR-0077：TaskStore 私有 retained string 分配请求](0077-task-store-private-retained-string-requests.md)（proposed；Phase 1 Source-only，collector 尚未接入，完整缓存预算仍未评估）
- [ADR-0078：独立采集 TaskStore 私有 allocator 请求](0078-task-store-private-storage-collection.md)（proposed；Phase 2 Source-only，未执行真实 capture，完整缓存预算仍未评估）
- [ADR-0079：Ninja 工作目录变量与编译观测快照](0079-ninja-workdir-observation-snapshots.md)（proposed；纯 Reader 回归通过，真实公开 SDK 重跑仍待执行）
- [ADR-0080：编译数据库分类与 RC 完整命令绑定](0080-rc-database-observation-bindings.md)（proposed；Source/pure-only，RC/MT 真实进程观测与常规 SDK 验收仍待完成）
- [ADR-0083：便携 host AMD64 构建调试观察器](0083-host64-build-debug-observer.md)（proposed；显式双根 Source 接线，便携 producer、正常两架构与安装消费者实际验证待执行）
- [ADR-0084：TaskStore 私有分配请求配额](0084-task-store-private-request-quota.md)（proposed；x86/x64 独立原生回归通过，默认未启用；任务服务、完整 SDK 与完整缓存预算待验收）
- [ADR-0085：任务终态的预留失败记录](0085-task-terminal-funded-failure.md)（proposed；独立源码复核通过，两架构原生与完整 SDK 验收待执行）
- [ADR-0086：LLVM 观察器中的 Windows 扩展驱动器路径](0086-llvm-observer-local-tool-spelling.md)（proposed；严格路径回归通过，完整 SDK 重新构建验收待执行）
- [ADR-0087：移除默认导航调试日志](0087-navigation-default-trace-removal.md)（proposed；仅移除同步追加，权限判定不变；实际运行与长期验收待完成）
- [ADR-0088：独立诊断工具编译契约](0088-runtime-doctor-compile-contract.md)（proposed；精确目标使用静态运行库，SDK 校验规则保持；完整候选重建与交付验收待完成）

- [ADR-0089：原生 Bridge 的 TaskStore 配额与独立快照](0089-native-bridge-task-store-policy.md)（proposed；独立源码审查通过，实际编译、校准及生产默认待验证）
- [ADR-0090：由冻结测试源码推导生成依赖](0090-frozen-test-source-derived-dependencies.md)（proposed；完整字节回归通过，新的 SDK 构建验收待执行）

- [ADR-0091：同一 Bridge 的原生 TaskStore 额度验收通道](0091-task-store-quota-native-acceptance-channel.md)（proposed；独立源码与序列回归通过，实际采样及校准待验证）

- [ADR-0093：内存不足时仍可释放 JSON](0093-allocation-free-json-destruction.md)（定向回归通过；完整候选验收进行中）
- [ADR-0094：应用等待真实 Runtime 关闭](0094-application-runtime-shutdown.md)（真实桌面失败已复现，修复与重验进行中）
- [ADR-0097：0.5 内核重建与公开接口验收](0097-kernel-rebuild-import.md)（两架构重建与应用验收进行中）
- [ADR-0098：公开 Bridge 配额的可重复校准负载](0098-task-store-calibration-workload.md)（独立消费者实施中，生产默认额度未冻结）
- [ADR-0099：安装消费者的原生构建观察](0099-installed-consumer-debug-capture.md)（x64 实际重载通过，x86 修复重验中）
- [ADR-0100：单次验收核验复用实际已验证输入](0100-acceptance-input-reuse.md)（实施与真实验收进行中）

- [ADR-0101：TaskStore 默认请求字节额度](0101-default-task-store-request-quota.md)（128 MiB 决策已采用；新默认入口实际验收进行中）

- [ADR-0103：整数显示频率与高刷节拍](0103-integer-high-refresh-policy.md)（双架构定向验证通过；实际呈现单列）

- [ADR-0102：图像缓存驻留记录预算](0102-retained-resource-cache-budget.md)
- [ADR-0104：依赖构建来源与交付源码](0104-dependency-producer-roots.md)
- [ADR-0107：依赖来源声明以纯文本随包交付](0107-inert-upstream-license-metadata.md)

- [ADR-0108：长期负载中的 TaskStore 请求字节限额](0108-long-task-request-quota.md)（独立 proposal，冻结候选尚未采用）

- [ADR-0109：有限 UIA 投影变化通知](0109-uia-state-notifications.md)（双架构定向验证通过；实际业务与读屏单列）
- [ADR-0110：显式构建与绑定可选 UIA 交付](0110-uia-build-delivery.md)（ON/OFF 搬移消费者通过；最终公开入口待验）
- [ADR-0111：保存每次构建的独立 CMake 配置日志](0111-immutable-cmake-configure-log.md)（真实重配置探针通过；四配置完整复验待执行）

- [ADR-0112：有界的进程地址空间观察](0112-bounded-process-address-census.md)（双架构真实扫描及独立读取回归通过；实际负载预算待校准）

- [ADR-0119：在临时光栅覆盖边界保留已接收的滚动目标](0119-preserve-scroll-target-beyond-raster-coverage.md)（proposed；源码候选与端到端验证分开）
- [ADR-0120：区分合成滚动确认与外部滚动意图](0120-preserve-compositor-scroll-acknowledgements.md)（proposed；默认整数元素滚动源码候选；完整 x86 构建、限定 GUI 位移与同值 CSS 取消已验证，性能及完整配置资格未完成）
- [ADR-0122：输入到可见反馈的实施契约](0122-input-to-visible-feedback.md)（代码交付与性能资格分开）
- [ADR-0123：有界输入恢复与 owner 调度](0123-input-admission-and-scheduler.md)（输入源码候选，调度包持续集成）
- [ADR-0124：帧截止、资源通知与有界画布所有权](0124-frame-resource-presentation.md)（按工作包集成，运行资格未关闭）
- [ADR-0125：有界输入、任务与可见帧关联](0125-input-frame-trace.md)（源码候选；编译与执行未运行）

- [ADR-0127：WebView2 owner 输出与重入边界](0127-webview2-owner-output.md)（proposed，候选默认关闭）

- [ADR-0126：GPU输入到可见反馈独立候选](0126-gpu-input-ui-candidate.md)（proposed，未启用/待数据）
- [ADR-0118：MiniBlink 关闭的分阶段继续状态](0118-miniblink-nonblocking-unmount.md)
- [ADR-0119：显式离线 Web 源码诊断构建档](0119-offline-web-build-profiles.md)
- [ADR-0124：Web 构建目标、资源清单与体积分析](0124-web-targets-and-size-analysis.md)
- [ADR-0125：显式运行时依赖与包内内容去重](0125-runtime-manifests-and-bundles.md)
- [ADR-0126：可选压缩档位与实测开销](0126-optional-compression-profiles.md)
- [ADR-0127：可选自动后端宿主](0127-automatic-renderer-host.md)

- [ADR-0128：受控业务窗口的 CPU 呈现](0128-controlled-cpu-presentation.md)（业务 HWND 与 Inspector 回调路径分开；真实运行待验）
- [ADR-0129：SHostWnd 上的原生诊断菜单](0129-native-host-window-menus.md)（仅附加菜单的可信 opt-in 窗口采用系统非客户区；真实动作待验）
* [0130: Bounded legacy source maps](0130-bounded-legacy-source-maps.md)
* [0131: Native view lifetime observation](0131-native-view-lifetime-observation.md)
* [0134：受控 Inspector 的键盘事件快照](0134-controlled-inspector-keyboard-snapshot.md)
* [0135：MiniBlink 显式本地 HTTPS 资源原点](0135-owned-local-resource-origin.md)

- [ADR-0123：公共原生受控 DevTools](0123-public-devtools-candidate.md)（默认关闭与目标许可；本轮发布资格单列）
- [ADR-0136：0.6.0 普通 Release 分类与限定范围](0136-0.6.0-release-scope.md)（用户本次授权；生产资格仍未完成）
