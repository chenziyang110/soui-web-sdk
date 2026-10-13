# ADR-0069：采用完整 SDK 产品源码

- 状态：proposed（源码已采用；构建、运行期与发布验收未完成）。
- 日期：2026-10-13
- 关联：[进程所有权](0003-process-runtime-ownership.md)、[参考业务](0051-reference-business-parity.md)、[可重试关闭](0056-reference-retryable-host-close.md)、[标量指标](0061-native-runtime-scalar-metrics.md)、[有界 Engine](0062-bounded-engine-admission.md)、[工具链身份](0066-versioned-native-toolchain-inputs.md)

## 背景

公开源码需要接入已组合并逐件核对的 SDK 实现、工具、测试、页面和文档，不能仅携带新的验收说明而继续使用旧 getter、关闭逻辑或构建入口。本次产品文本来自完整 SDK 61b Source，其 1333 文件清单原字节 SHA256 为 `e89559b0f3f74600e56259acfa4c8084263f5785eefe71452a51d649cf55afb9`。长期规则使用 9f8 的 Owned 实际耗时修正；此前 5d 的缺失耗时绑定不能作为通过前件。

## 决定与采用边界

采用清单内的产品文本原字节，现有相同文件不写入，不删除公开树的额外文件。每个已有目标在独占文件句柄内重核采用前身份，新增目标使用只允许新建的方式；并发变化必须报告，不能覆盖。

依赖源码与补丁、预编译和其他二进制、历史 verification/provenance、版本和根源码清单由维护者分别处理。本切片不修改其身份或资格，也不覆盖维护者拥有的验收入口、runner、桌面 smoke 与其他明确冲突叶。依赖清单仅采用维护者已核对的两个 zlib `1.3.1` 版本文本纠正，载荷哈希不改。许可审查和历史引用由维护者单独接入，未验证状态不能因复制而升级。解析器历史夹具仅是明确保留原失败或未评价状态的测试输入，不是当前资格证据。

## API、线程和生命周期

沿用单一进程 Engine 所有者线程与各窗口独立 WebView、Bridge、session 和资源所有权。业务对象必须比 Host 存活更久；导航撤销真实 scope，不重新执行或伪造旧任务的终态。标量诊断保持原 opt-in、try-lock、独立 owner epoch 与不完整状态；多个 getter 的顺序读取不形成跨组件原子快照。

关闭仍先撤回窗口请求和 WebView，再完成 Bridge/worker join，最后关闭进程 Runtime。精确 `E_BUSY` 和 `E_CURSOR_CLOSE_PENDING` 只允许在原期限内保留真实所有者并泵送后重试，不能重置 deadline、释放未完成对象或把强制清理当自然退出。Engine 与 scope 的现有容量和拒绝路径不改变。新诊断公开布局需要 SDK 与消费者一起重编，并使用匹配的真实内核；复制旧 DLL 不证明新 v2 getter 可用或 ABI 相容。

## 验证与交付

本次只完成文件成员选择、字节采用和回读，没有执行构建、测试、helper 或原生程序。采用后的公开入口需要维护者绑定当前源码、工具链、真实消费者请求和 fresh 输出，继续检查原 Owned 预算、租约与自然 whole-Job 关闭。历史记录保留原身份和范围。

长期规则中的八小时阈值是本轮维护者在运行前选定的规则，不是历史认证；欠采、完整进程树或第三方缓存缺证的对应子门仍为未评价。标量 string 容量不等于整个 allocator/heap；短测、参考业务或新源码均不关闭 Win7/目标系统、运行期、ABI、资源、许可证、完整 P0 或正式发布门槛。VERSION 和发布状态保持由维护者明确决定，签名按本项目已有 SDK 用户豁免处理。
