# ADR-0081：TaskStore 空存储清理的退款记账

状态：proposed；最小修正已通过 Root 的独立 x86 Release fault 回归。当前完整 SDK 候选的九项原生回归和 x64 验证仍待执行，生产、完整字节预算和发布门槛未关闭。

## 背景与证据

基于 `8304f339e4c083a74cb9e08731b7c296c5a277d1` 的真实九项原生回归有八项通过，`sdk.native.task-store-storage-faults` 在原退款断言报出 `Rollback leaked live requests`。该 CTest 运行未强制终止；旧失败记录保持原内容。

新增安全数值诊断后，Root 的真实诊断定位到 `prepare-start`、`failNth=2`、两次分配调用：map 当前字节为 2432 且不变，当前请求数从 18 降至 17；成功分配总数增加一，退款总数增加二，但双方字节增量均为 272。私有 string 的 40848 字节和 41 个当前请求不变；aggregate 当前字节为 43280 且不变，请求数从 59 降至 58。task、operation 和 scope 条目数均保持四。

真实 VC142 `14.29.30133` 的 `xhash` 先分配 map 的 list sentinel，再分配 bucket vector。第二次分配失败时，`_Hash_vec::_Tidy` 仍调用 `deallocate(nullptr, 0)` 清理未取得存储的空 vector。原私有分配器给这个调用退款一次，随后又正确退款 sentinel，因此多退一个请求而没有多退字节。这是已定位的记账错误，诊断文字本身不证明物理泄漏。

该定位诊断 actor 退出失败后，旧 supervisor 清理了 owned 残余 conhost，不能把该 actor 描述为自然 RED。其真实数值与未强制终止的原 CTest 失败分别保留。诊断 v1 的缺少相对内部头文件编译失败、以及 GREEN v1 的 reference verifier 超时也保留；后续 fresh 尝试未改写它们。

## 决定

仅在私有 `task_store::storage_allocator::deallocate` 开头对 `pointer == nullptr && count == 0` 直接返回。这个已观测的空清理调用没有取得存储，不能退款成功请求。其他输入沿原路径执行：先真实 `std::allocator` 释放，再退款 map/string 域和 aggregate 域。非空指针加零 count 也保留该路径，不把所有零 count 调用一概跳过。

保留所有真实分配与失败钩子、实际退款计数、owner 生命周期、同一 epoch、同 owner 的无异常 swap，以及先准备后发布的强回滚。独立 detail allocator 没有该实际调用证据，本次不扩展修改。测试仅在原退款断言前加阶段、故障序号和三个域的安全数值诊断；原 585 行测试、断言、故障点、预算和业务逻辑保持。

## 备选方案、兼容性与迁移

未采用放宽当前请求数相等断言、跳过故障序号、扩大预算、关闭注入、把 count 为零一律视作退款或不退款等方案。它们会掩盖真实退款差异，或改变非空指针的释放语义。

公共 `std::string` ABI、旧 DTO v1 的 coverage/gap `3/63`、独立私有 DTO v1 的 `7/31` 均保持；未增加字段、预算或最低工具链要求。调用者无需迁移。原 RC 观测六份 Source、第三方文件和历史失败证据保持原内容。

## 验证与边界

Root 的 fresh GREEN v2 绑定封存的五份 Source、实际编译输入和 EXE，并独立复核了 36 个物理 pin。`reference-before`、configure、compile、fault suite 和 `reference-after` 五阶段均自然退出 `0/0/Job0`，无 STOP、fallback 或强制通过；fault suite 输出 `TaskStore storage observation contract passed`。configure 仅有未使用 `SOUI_FEASIBILITY_TOOLCHAIN` 的提示。

该独立结果的 `RESULT.json` 为 18192 字节，SHA256 为 `80a9c0af74ad1ef060f9a7345fd45f5e80c8691981768d85abf0df9a39bbb793`；实际 fault EXE 为 280064 字节，SHA256 为 `d1b30f72e4753ae28bbd546807bf2b9e6cfb9955f9be33b611f846c114a024b4`。Root 的独立复核明确 `standaloneX86FaultSuitePassed=true`、`fullSdkAccepted=false`、`x64Passed=false`、`releaseReady=false`。

这只证明封存最小修正的独立 x86 fault 回归。当前完整候选尚未经过九项回归、双架构验证或真实应用启动；私有 allocator 请求也不覆盖公共临时副本、其他 SDK owner、外部分配器/堆成本和最终处置 receipt，不能据此声称完整缓存字节预算、长期内存稳定性、Win7 兼容或生产发布通过。
