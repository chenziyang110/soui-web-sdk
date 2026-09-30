# 基准系列 Git 与独立交互负载门禁回归

原 HEAD `6e43dc0090e15cedff3e30b0a0a3f1ef6a0c9478` 的基准系列共享分类没有 `Git.exe`、新模板 `ScrollStarter.exe` 和验收探针 `V02InputProbe.exe`；它们也不匹配已有 SOUI 名称规则。在外层检查后出现这类进程时，系列首尾及轮内门禁可能漏判竞争负载。最小修改补齐这三个名字，大小写统一；它们即使是本轮后代也不能按基准宿主、采样器和 InputTrace 豁免。协议说明同步更新，SDK Core 没有变化。

修改前后三个文件的字节、源码 HEAD、散列和差异保存在 [23 份原件](raw)。准备阶段因外部 Git/QEMU 活动只做语法和差异检查，均原始退出 0，没有运行产品或性能系列。外部高 CPU Git 随后实际退出；重新查询无已知竞争进程后，在 `2026-09-30T02:16:19.604Z` 运行本次修改的 `series-interference.test.cjs`。直接子进程退出 **0**，未超时、stderr 为空，**21/21** 检查通过且无跳过。新增用例验证三个进程名，以及外部/本轮后代两种来源都拒绝，预期 SOUI 宿主、采样器和 InputTrace 仍允许；其余用例核对真实 Windows 进程查询、PID/创建身份、查询或日志失败拒绝、后续负载出现和仅自有句柄清理。首尾实际进程查询未发现已知竞争负载，但不是连续采样或性能隔离证明。

`Verify-Archive.cjs create` 固定精选原件清单；`verify --originals <外部根> --index` 复核原件、工作树与 Git index。归档校验结果仅说明这些字节一致，不替代对测试行为及原始退出码的阅读。外部原件根为 `C:\SOUI-SDK-production-validation\benchmark-git-load-gate-20260930`。旧受干扰样本、准备记录与历史功能测试不改写。

本次只关闭已知名称分类缺口。未知负载、五秒采样间出现又退出的短进程及整机隔离仍需人工控制；当前没有五对 SOUI/Electron 的有效正式结论。物理输入到呈现、冷启动至少十次、热启动至少三十次、空闲 CPU、同功能运行包与接入成本未通过。`sourceDeliveryVerified=false`、`benchmarkValid=false`、`releaseReady=false`；不发布或合并。
