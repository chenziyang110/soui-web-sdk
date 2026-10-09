# ADR0042：交互端点注册表退休与异常后的锁释放

状态：两处源码修复已实施；两架构的故障对照和实际文件编译已记录。不表示内核构造、启动、关闭或发布验收完成。

## 问题

`wke.cpp` 在销毁 WebView 时逐个把匹配端点加入临时 vector，再从全局注册表删除。后续 vector 扩容抛出异常时，已摘除端点的注册表引用失去持有者；它们仍可能处于 active 状态，正常关闭尚未进入。销毁路径不能依赖另一笔堆分配来保存这些所有权。

同一注册表的创建入口在裸 `new` 后执行 vector 插入。插入失败时，未发布端点缺少独占所有者。异常必须在实际 C++ 实现入口内处理，不能依靠经过 C 接口调用的外层 catch。

`ActivatingObjCheck` 使用显式 Enter/Leave 包围 STL 插入和快照分配。异常展开会跳过 Leave，使其他线程无法继续访问注册表。本切片沿用 ADR0041 的 C++ 展开配置。

## 决定

端点增加仅供退休使用的 private 链指针，不改变公开结构、函数表或 opaque handle 契约。在注册表锁内一次性摘除所有匹配端点，把已有的注册表引用转交给局部侵入链，不分配新存储。锁外逐个取得端点锁、撤销 active 和 view/compositor 指针，再释放该注册表引用。无关 WebView 的端点保留；调用者持有的引用仍按既有 retain/release 契约处理。

未发布端点由局部独占所有者持有。只有注册表插入成功才交接调用者和注册表的引用；创建失败在该 C++ 入口内返回空端点。这里不捕获内核致命错误，也不声称可以恢复 PartitionAlloc 的 fail-fast。

Activation 的现有临界区改用不可复制的局部 RAII guard。正常路径和异常路径都实际执行 Leave，异常仍按原调用链传播。本切片不新增 GC 功能，也不改变注册表的成员、查询结果或快照顺序。

## 验证与范围

故障回归应绑定实际源码：旧销毁实现第二次临时存储分配失败后，已摘除的首个端点仍 active 且保留无人管理的注册表引用；新实现必须在禁止分配时撤销多个匹配端点，保留其他 view，并保持引用计数正确。创建失败应证明没有未发布端点泄漏、注册表没有部分发布。

Activation 插入及快照分配失败后，由另一真实线程尝试取得同一临界区，证明锁实际释放。旧实现的失败记录保留。两架构分别编译实际修改的 vendor 文件；源码片段或锁测试不代替完整 DLL、GUI 或目标系统验证。

已完成的有限结果见[生命周期故障回归](../../verification/production-validation/0.5-controlled-cursor-2026-10-09/lifetime-fault-regressions.json)：每架构 9 个候选正例、6 个旧版真实反例、3 个对照和 9 项候选 CTest；随后编译实际完整 `wke.cpp`、`ActivatingObjCheck.cpp` 和新建 config PCH，确认 `/EHsc`。编译使用实验 GPU 配置作类型检查，不证明默认软件内核或实际资源回收。编译监督的 124/generic false 和事后输入绑定限制仍保留。

公共 portable 回归已加入测试入口，并在 x64/Win32 分别完成 9 项直接执行和 9 项 CTest，见[实际记录](../../verification/production-validation/0.5-controlled-cursor-2026-10-09/portable-lifetime-suite.json)。生成的 3,994 字节内容与实际源码片段逐字节相等；两个测试目标构建前还会检查整份源码摘要和生成片段的 HEX。支持的片段必须无 NUL、使用 LF 且以 LF 结尾，其他输入明确拒绝。此前 Windows 文本生成改变换行的失败记录仍保留；这次收窄输入范围不代表原先无末尾 LF 的生成要求通过。

随后 x64 [默认软件内核的完整编译与链接](../../verification/production-validation/0.5-controlled-cursor-2026-10-09/vendor-full-software-x64.json)实际消费了这两个修改文件，并生成完整 DLL；历史核心库来源和编译监督的 generic false 仍保留。普通 SDK 的真实启动成功，资源关闭仍超时，不能由完整 DLL 已生成推断关闭通过。

`CWebView/WebPage/WebPageImpl` 的部分构造清理、worker 启动失败、observer 交接和实际关闭 drain 仍需后续修复。Activation singleton 销毁与重新初始化也不在本次锁修复的证明范围内。这些缺口仍阻止正式发布，不能由本 ADR 或短回归追认通过。
