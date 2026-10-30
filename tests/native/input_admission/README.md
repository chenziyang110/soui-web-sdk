# 输入 mailbox 回归源码

`fixture.cpp` 覆盖普通/边沿独立额度、总文本字节、分配失败、FIFO 后恢复、按住键隔离、IME commit、关闭和 Engine 32 Host 注册/轮转。测试项 `sdk.native.input-mailbox` 由根测试配置接入；Root 负责注册。

实际执行范围以对应 ADR 和证据收据为准。普通 SDK 测试入口为 `ctest --test-dir out/<当前构建目录> -C Release -R sdk.native.input-mailbox --output-on-failure`；真实 IME、capture/focus、导航/关闭竞争仍需目标机验证。此源码不能证明真实输入延迟或性能。

W09 源码另外覆盖 cooperative_turn_budget 的 default-off/minimum/invalid-range 截止规则。`SOUI_ENGINE_SHARED_TURN_BUDGET_US=100..5000` 仅是待 profile 选择的有界候选，默认0保留组件独立预算；它不抢占单个 callback，FIFO 仍服从输入 ordinal fence。新增 native/provider/input/FIFO/serialization QPC 成本直方图用于专人的配对源码版本/profile；没有实测数据时不能默认采用或声称 p95/p99 改善。

私有输入服务现在可接收 Engine 的同一 shared deadline；mailbox 取其与既有 1ms 的较早者，最多8项。已过期仍先处理一个合资格输入再让出，不越过 FIFO cutoff；未采用新虚函数的内部服务仍调用原 `service_before`。provider 通过私有可选 ABI 尾字段接收同一显式截止，默认0和旧 provider 保留旧入口；这是合作边界，不是整个 turn 硬上限。默认0或非法预算构造不再为无效截止读取单调时钟；显式起点的测试构造语义保留。

`deadline_cases.h` 直接调用实际接口，以已过期截止检查一项进展、pending/head续接、cutoff、恢复、quiesce/close及重入关闭，另保留旧入口8项上限和旧override回退。Windows用临时、作用域恢复的1000µs配置检查真实 Engine 向服务传递有限共享截止，不用 sleep 或要求按时调度。由root统一整合受影响构建/测试；具体执行以收据为准，源码检查不代表输入性能改善。

`provider_cases.h` 检查不同 epoch 的剩余时长向下换算、过期和零 QPC 截止、采样/频率失败、算术溢出、旧 ABI 前缀及短表/capability/空指针回退。真实 Engine 覆盖默认/非法/有限预算、旧 callback、一次分派、已有事件续接、双 callback 替换、旧 setter 清除和锁外捕获退休。legacy 与 bounded callback 分别在执行中自行清除或替换，旧捕获在 setter 返回后仍存活，回调退出才退休；正常 turn 以局部 swap 保活，不逐轮复制或分配。`standalone/` 可编译实际 Engine、interaction protocol 和 trace 源码执行同一夹具，范围不含实际 Host/provider DLL，也不能替代完整 SDK/内核验证。执行收据见 ADR-0147 的集成记录。

`visibility_cases.h` 使用实际 latest-visibility policy，覆盖失败不ACK、下一机会重试、稳定态不重复调用、hide/show合并后强制fresh、setter期间新intent，以及关闭/重建的unknown状态。实际Host专人用例还需 show/hide、parent minimize/restore、owner发布失败后重试，确认hidden不layout/record/composite、恢复首帧全脏且main/composite phase重置；源码fixture没有创建窗口或证明真实可见性。
