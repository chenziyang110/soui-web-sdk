# 合成滚动 ACK 反馈回归

测试直接包含生产 `CompositorScrollAcknowledgement.h`，编译生产 `ScrollController.cpp` 与 `ScrollTreeSnapshot.cpp`；默认规范化查询也直接调用生产 `WebLayerScrollClient`。设计见 [ADR 0120](../../../docs/adr/0120-preserve-compositor-scroll-acknowledgements.md)。测试不复制 ACK 数学实现，不执行产品或注入 GUI 输入。

默认程序包含 25 项：10 项 ACK 核心断言及 15 项新增接口/Controller 边界。覆盖正负小数、非零原点、双轴、整数身份、clamp、相同归一化位置的新 sample、epoch/sample fence、非有限数和有限运算溢出、默认 identity 查询、原始重复 receipt、明确失效、在途/连续 ACK、反向、负向累计、临时覆盖扩展，以及真实积分得到 max-minus-epsilon 后的向外输入拒绝。

Controller 夹具把受控 Blink 归一化结果交给真实生产 ACK helper，再使用 helper 发布的 total/adjustment 激活同 epoch snapshot。clamp 和非零原点也使用受控 canonical 输入，不测试 GraphicsLayer 的归一化算法。覆盖始终受限，输入预测使用真实 `advanceInputPrediction`；期望终点来自测试操作，不在测试中重写阻尼或累计公式。外部相同/不同值的两个测试显式调用生产 `invalidate()` 后发布新 epoch，**只验证取消的状态契约**；它们没有执行 Home、CSS、拖动、FrameView 或 GraphicsLayer 的真实来源入口，不能把结果扩称为 DOM 端到端通过。

## 构建

从仓库根目录执行，Windows/MSVC v143/C++17、Windows SDK 10.0.19041.0、Release 静态 CRT `/MT`。构建不运行测试，实际执行遵守项目的隔离环境约束。

```powershell
cmake -S tests/native/scroll_ack_feedback -B ../scroll-ack-tests-x86 -G "Visual Studio 17 2022" -A Win32 -T v143 -DCMAKE_SYSTEM_VERSION=10.0.19041.0
cmake --build ../scroll-ack-tests-x86 --config Release --parallel 2
cmake -S tests/native/scroll_ack_feedback -B ../scroll-ack-tests-x64 -G "Visual Studio 17 2022" -A x64 -T v143 -DCMAKE_SYSTEM_VERSION=10.0.19041.0
cmake --build ../scroll-ack-tests-x64 --config Release --parallel 2
```

获准的运行环境中执行 `Release/soui_scroll_ack_feedback_tests.exe` 或相应目录的 `ctest -C Release --output-on-failure`。程序输出 JSONL，全部断言成功才退出 0；任何失败退出 1，不设置 `WILL_FAIL`。保留 stdout/stderr、实际 PID/退出码、源码与 EXE 散列。编译通过不证明运行通过。

## 旧生产代码对照

旧版本没有新增 helper，不能伪造一个“旧 helper”来生成预期 RED。显式设置 `SOUI_ACK_BASELINE_WEBLAYER_SOURCE` 时，配置步骤用 `extract_baseline_ack.py` 从指定旧 `WebLayerImpl.cpp` **机械提取、不改写** `setScrollPositionDouble` 和 `setScrollPositionFromCompositor` 两个真实方法体。`baseline_ack_shim.h` 只提供该方法所需字段、点类型及可控 didScroll 回调；回调将夹具提供的 canonical 位置交回提取出的普通 setter，commit 替身不执行布局或渲染。

```powershell
cmake -S tests/native/scroll_ack_feedback -B ../scroll-ack-baseline-x86 -G "Visual Studio 17 2022" -A Win32 -T v143 -DCMAKE_SYSTEM_VERSION=10.0.19041.0 -DSOUI_SCROLL_KERNEL_ROOT=../preserved-baseline/third_party/sources/miniblink -DSOUI_ACK_BASELINE_WEBLAYER_SOURCE=../preserved-baseline/third_party/sources/miniblink/cc/blink/WebLayerImpl.cpp
cmake --build ../scroll-ack-baseline-x86 --config Release --parallel 2
```

每次配置生成 `baseline_ack_methods.receipt.json`，绑定整份旧 CPP、提取方法、生成 CPP 的散列。显式源码覆盖同时选择旧 Controller/Snapshot 与头文件。旧程序只运行与当前程序完全共用的 **10 项核心 oracle**；新增 raw-receipt、canonicalAdjustment 与 Controller 集成的 15 项不在旧 API 上伪造实现。两组用例数量和范围必须分开报告。旧方法的受控回调不等于真实 Blink producer；与真实 DOM/GUI 证据互相补充。

当前目录仅准备测试源码；真实执行结果以独立回执为准，不预写通过。既有 target-retention、scroll-coverage 与覆盖收缩诊断保持原有源码和结论。
