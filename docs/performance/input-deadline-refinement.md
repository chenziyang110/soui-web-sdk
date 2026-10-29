# 输入批次共享截止时间

既有显式共享预算限制 native/FIFO，但输入批次另外取得完整1ms。当前轮预算已经耗尽时，输入仍可能连续处理八项；预算关闭的构造也会调用无用的单调时钟。

本次只补私有输入截止传播：Engine把同一轮deadline传给input service，mailbox取它与原1ms的较早值，在回调边界让出。即使已过期，也先给一个符合ordinal cutoff的输入机会，防止旧输入永久阻塞后续FIFO/cleanup。八项上限、轮转、handler锁外调用、pending、取消和关闭所有权保留。旧service override通过默认委派兼容。

`SOUI_ENGINE_SHARED_TURN_BUDGET_US`默认仍为0；0或非法配置的预算对象不再读取clock。provider原回调和WKE ABI没有改动。本次没有贯通跨DLL的provider剩余预算，也不能抢占单次回调。持续native压力下更短的输入批次可能增加排空轮数，必须以实际输入尾延迟选择显式配置，不能因该选项存在就默认启用。

## 实际验证

2026-10-29，Windows x64 Release，显式LLVM22/MSVC142/SDK19041本地增量构建。四个正常SDK/fixture目标实际编译，9.486秒自然0/0；相关11项CTest实际通过，1.758秒自然0/0。两阶段末态均无活动进程、没有强制终止。853项实际SDK与受影响fixture文件在构建/运行前后逐字节SHA不变。

```powershell
cmake --build out/pr129-native-x64 --config Release --parallel 2 --target soui_input_mailbox_tests soui_engine_capacity_tests soui_engine_thread_timeout_tests soui_engine_thread_startup_tests
ctest --test-dir out/pr129-native-x64 -C Release --output-on-failure --timeout 30 -R '^(sdk\.native\.input-mailbox|sdk\.native\.engine-capacity\..*|sdk\.native\.engine-diagnostic-timeout|sdk\.native\.engine-startup-rollback)$'
```

上述命令消费已配置的LLVM验证目录，不是默认MSVC143或完整发布构建。新增用例覆盖已过期截止时的一项进展、pending/head与FIFO cutoff、MAX恢复恰好一次、quiesce/close/重入关闭、旧override回退，以及真实Engine配置0传无限deadline、1000传有限deadline。既有ordinal、IME/分配失败、终态、迟到唤醒、多Host与容量回归保留。

原始本地证据名称及SHA256：

- `input-budget-x64-r1/result.json`：`4ebcc53691bbcf4b7c984f2ec10dd6a098bcdddfc1966ccef7c7c0bd2aa08b78`
- `input-budget-x64-r1/source-before.json`：`e10f34541b5feb29dc1005788ca4d39569b58c07fe02639a49e5bd0f2f0633e2`
- `input-budget-x64-r1/tests-stdout.bin`：`cfaede2cfc854d9d225da8822576407b2077a776d17ff7c6f0ef98c17590e190`

SDK静态库从14196420B增至14199244B（+2824B）。静态库不能替代实际EXE/runtime/ZIP体积对照；完整运行、x86、默认MSVC143、物理输入和性能仍须相应证据。本次没有重编内核、操作GUI或宣称FPS提升。

同一ES5页面、同一内核和工具链的手动MiniBlink消费者随后实际重链并部署：EXE仍为1506816B，按相同部署清单边界统计的运行目录也增加0B。新SDK库SHA为`4da09039eedde79778bd45f509be0340593350befad330af187b079f6ca32a9a`；新EXE SHA为`6454ec301b43e6f413714547902dd5de2c4bd9c0c99d9d046b0a373025d5b00e`。两份独立启动入口只给子进程配置0或1000µs，未实际启动，不构成帧率/输入或完整ZIP通过结论。

兼容与回退：保持默认0可保留原独立输入批次策略；需要源码回退时回退本次私有deadline接线并匹配重编SDK/消费者。不得为回退性能候选撤销已有输入终态、计量真实性或关闭屏障。
