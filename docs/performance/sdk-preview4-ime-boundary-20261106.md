# SOUI Web SDK：IME 消息边界的限定复验

本轮确认了三个 SDK 源码反例和一个真实 MiniBlink 误删边界，并验证了两个未采用的修复提案。**整体输入延迟与帧率验收仍未通过。** 冻结的 `0.7.0-preview.4` SDK、默认内核、运行包及版本未改变，没有重跑 Electron、FPS 矩阵或 R32 中文输入流程。

本报告接续[输入法追加验收](sdk-preview4-ime-20261106.md)，决策及兼容性边界见[ADR-0165](../adr/0165-native-ime-result-composition-order.md)。机器记录见[continuation-r33.json](../../verification/performance/sdk-input-frame-20261105/continuation-r33.json)。以下原件路径相对于本地验收归档 `PR129-performance-20261104/repair-r33-ime-packet-boundary-r1/`。

## SDK 消息顺序与状态

原生消息可以同时含 RESULTSTR 与 COMPSTR。原 SDK 先处理预编辑、再提交结果：旧预编辑为 ni，结果为“你”、新预编辑为 ha 时，新 ha 会丢失。只交换分支又留下 committed 状态，使随后 END 未能清除 ha；仅处理前两项仍会在空更新后保留错误的 composing 状态。

提案先处理结果，再独立处理当前预编辑；在调用 provider 前更新 composing/committed，避免旧调用返回覆盖同步清理后的状态。长度上限、失败恢复、IMC 释放、cursor、ordinal/FIFO 和公共 ABI 均不改变。

实际 v143/C++17、SDK19041 的单一外部 CPU 工具通过20个场景；三个负对照分别命中上述反例。原始退出为 `0/1/1/1`，构建及四次运行自然退出、Job0，无强制结束。它逐字投影生产 UI/Engine 片段，IMC/provider 为显式替身，**不能代替 SDK mailbox、真实 Blink 或系统双标志输入法验证**。

首轮编译失败保留：夹具把 WinAPI 读取函数写成依赖 this 的成员，原生产 lambda 没有捕获 this。第二份夹具仅改成经 HIMC 访问原账本的静态替身，六个生产片段、20个断言及负对照未变。第二轮构建55.903秒；summary SHA `7349a615b9106f14b90c28857d270b83dd42bf127b9d9c6ae6d9bbde21baa87a`，独立审查 SHA `416310d8928c60198f00aaef82dabb7d38987e4f5b751980293734a9ed97e11e`。

## 真实 provider 反例与复验

两个固定原语配方均先通过实际 Blink API 将 ni 确认为“你”，再选中0..1，分别调用空 setComposition 与 cancel。原私有内核中，两次 API 都返回true，却删除了已提交文字；不能将调用返回成功当作功能通过。

| 配方 | 原私有内核 a4f573 | 加四行保护的私有内核 7bc30e |
|---|---|---|
| 空 setComposition，当前无活动组合 | “你”变空，selection0/0 | 保留“你”，selection0/1，无新增 input |
| cancel，当前无活动组合 | “你”变空，selection0/0 | 保留“你”，selection0/1，无新增 input |

保护只在 `setComposition` 入口对“空文字且无实际组合”直接返回。两个实际 run 分别为 `b71a2fbc88863525af89b81e1f99b9ed` 和 `417b30d72e813850164f0b717511905f`；复用同一 Host、页面、SDK4 和匹配运行目录，只换独立私有 DLL。两次入口及 Host 均自然0、Job0、正常关窗，无强制结束；SDK/vendor trace 完整且 loss0。独立分析 SHA 分别为 `6420f7365fd7b113d86f262d415fcb62d6dbd80c9ee0e1871a53ebb844b3c07d`、`55a1eb5847cddb44e4e75738ec8ec84800b3f69eadea5748a72733b1e0280c3b`。

私有候选两架构实际编译、链接0，导出名称集合一致。x64 DLL 为39,212,544字节，SHA `7bc30e80f25b50fb11ac7a943de37fb7f499f8bcc7c9533037ac43172a4cf626`；x86 DLL 为28,324,864字节，SHA `4b33e44621ec607739e17375e858afb65e87be5f9ad1fce4c38097c3d9d882e9`。此为 LLVM22/MSVC142 的私有增量，不代表标准 v143 全内核或 x86 真实交互通过。

新编译单元明确不使用旧 PCH，实际各1002个 include 均在冻结允许集合内且字节相符。旧 PCH 原件身份和四个继承库的历史字节等同性仍未知；因此严格全历史单输入差异、配对性能、默认兼容性均未认证。与 R31 私有 DLL 的字节差不能当作产品 runtime/ZIP 体积结果。

## 可交付提案与剩余门槛

[SDK 补丁](patches/0165-sdk-ime-packet-boundary.patch)为2,653字节，SHA `262e001e67f77b7ff230f2319ec2d4f70e85892fba5d5688b7c172d6bdd9e2ba`；[provider 补丁](patches/0165-provider-no-composition-empty-guard.patch)为984字节，SHA `ce7cefdca8560e70b6b585cb7975ac04d959612bc9f651f230e3cb8bc383a74f`。二者未应用于默认产品。外部 scratch 的补丁检查和应用均0，应用后的原始源码 SHA 与实际候选一致。首次 bare apply 因全局换行配置失败的 scratch/记录亦保留；成功检查显式固定 `core.autocrlf=false`。

`repro-r1/` 保存 CPU 复现源码、原生成器、六个真实片段、固定源码快照及逐文件 SHA；`fixture-read-seam-r2/` 保存实际 CPU 运行；`api-empty-probe/`、`provider-nocomp-guard-build/`、`guard-probe-entry-r1/` 保存原语反例、私有构建与复验。根验证收据 SHA `8bfa0fb58f9c36d9ea7b9c1907abdc4fa81a1a59e0975cd3baa2896abd20ea1a`。

活动 composition 的 compositionend 回调改选区、换文档或建立新组合时的重入/ABA，旧 provider 安全清除能力、plugin、真实系统双标志、更多输入法及 x86 交互仍开放。此轮不继续扩展这些测试，也不将有限保护误称完整输入法认证。

后续性能工作返回 M01 的输入到桌面观察关系及有效帧；既有覆盖缺口、R29 噪声停止和默认预算0保持。`performanceVerified=false`、`inputLatencyVerified=false`、`releaseReady=false`、`wholeGoalComplete=false`；PR129继续 draft，不合并、不发布。
