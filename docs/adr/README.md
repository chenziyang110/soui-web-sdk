# 架构决策记录（ADR）

当公共 API、Bridge 协议、权限、线程与生命周期、兼容目标、SDK 包格式或核心依赖发生变化时，在本目录新增 ADR。

文件命名：`NNNN-short-title.md`，编号递增。状态使用 `proposed`、`accepted`、`superseded` 或 `deprecated`。已接受 ADR 不回写历史结论；后续变化新增 ADR，并在旧记录中标明替代关系。

从 `0000-template.md` 复制模板。PR 必须链接相关 ADR，并在合并前将已批准决定标为 `accepted`。
## 0.4 新增

- [ADR-0016：运行档位与可选现代渲染后端](0016-runtime-profiles-and-modern-backend.md)
- [ADR-0017：版本分支整合与固定 SDK 身份](0017-pr-stack-integration.md)

## 0.5 新增提案

- [ADR-0066：版本化原生工具链输入与固定 WinRT 搜索根](0066-versioned-native-toolchain-inputs.md)（proposed；实际构建与验收仍待 Root）

- [ADR-0067：分别绑定 LLVM 项目与验证工具的请求](0067-owned-llvm-project-and-validation-requests.md)（proposed；源码和有限逻辑检查通过，实际工具准备与界面验收待完成）

- [ADR-0068：原生运行状态与兼容内核诊断](0068-native-runtime-diagnostics.md)（proposed；32 位修复正在重建，完整兼容性验收待完成）
- [ADR-0069：SDK 源码交付与验收入口](0069-sdk-source-delivery.md)（proposed；源码已采用，正式交付验收待完成）
- [ADR-0070：有限命名缓存观测](0070-finite-cache-observation.md)（proposed；纯数据检查不构成原生缓存或长期资源验收）
- [ADR-0071：长期运行中的命名缓存采样](0071-long-run-cache-sampling.md)（accepted；采集已接线，实际原生与长期验收待执行）
- [ADR-0074：有限命名缓存配置检查与重载校准采样](0074-finite-named-cache-validation.md)（proposed；经验字节预算与真实原生校准尚未完成）
- [ADR-0075：TaskStore map 分配请求观测](0075-task-store-storage-observation.md)（proposed；Source-only，未编译/Native/实际资源验收，完整缓存预算仍未评估）
- [ADR-0076：直接原生任务输入的准入上限](0076-native-task-input-admission-bound.md)（proposed；Source-only，未编译/C++执行；逻辑输入限制不等于完整字节预算）
