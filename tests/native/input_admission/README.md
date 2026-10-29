# 输入 mailbox 回归源码

`fixture.cpp` 覆盖普通/边沿独立额度、总文本字节、分配失败、FIFO 后恢复、按住键隔离、IME commit、关闭和 Engine 32 Host 注册/轮转。测试项 `sdk.native.input-mailbox` 由根测试配置接入；Root 负责注册。

当前未编译、未执行。专人使用对应源码重编 SDK 后执行 `ctest --test-dir out/<当前构建目录> -C Release -R sdk.native.input-mailbox --output-on-failure`，并补真实 IME、capture/focus、导航/关闭竞争与两架构。此源码不能证明真实输入延迟或性能。

W09 源码另外覆盖 cooperative_turn_budget 的 default-off/minimum/invalid-range 截止规则。`SOUI_ENGINE_SHARED_TURN_BUDGET_US=100..5000` 仅是待 profile 选择的有界候选，默认0保留组件独立预算；它不抢占单个 callback，FIFO 仍服从输入 ordinal fence。新增 native/provider/input/FIFO/serialization QPC 成本直方图用于专人的配对源码版本/profile；没有实测数据时不能默认采用或声称 p95/p99 改善。

私有输入服务现在可接收 Engine 的同一 shared deadline；mailbox 取其与既有 1ms 的较早者，最多8项。已过期仍先处理一个合资格输入再让出，不越过 FIFO cutoff；未采用新虚函数的内部服务仍调用原 `service_before`。provider 机会没有加入共享预算，不声称整个 turn 硬上限。默认0或非法预算构造不再为无效截止读取单调时钟；显式起点的测试构造语义保留。

`deadline_cases.h` 直接调用实际接口，以已过期截止检查一项进展、pending/head续接、cutoff、恢复、quiesce/close及重入关闭，另保留旧入口8项上限和旧override回退。Windows用临时、作用域恢复的1000µs配置检查真实 Engine 向服务传递有限共享截止，不用 sleep 或要求按时调度。由root统一执行受影响构建/测试；这些源码检查尚不代表实际执行或输入性能改善。

`visibility_cases.h` 使用实际 latest-visibility policy，覆盖失败不ACK、下一机会重试、稳定态不重复调用、hide/show合并后强制fresh、setter期间新intent，以及关闭/重建的unknown状态。实际Host专人用例还需 show/hide、parent minimize/restore、owner发布失败后重试，确认hidden不layout/record/composite、恢复首帧全脏且main/composite phase重置；源码fixture没有创建窗口或证明真实可见性。
