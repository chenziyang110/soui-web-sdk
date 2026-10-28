# 已接纳滚动目标的保留回归

测试直接编译 `third_party/sources/miniblink` 中的 `ScrollController.cpp` 和 `ScrollTreeSnapshot.cpp`，不复制运动公式。设计契约见 [ADR 0119](../../../docs/adr/0119-preserve-scroll-target-beyond-raster-coverage.md)。

## 原始反例与边界

`accepted_target_retention_tests.cpp` 保留首次 RED 用例的完整源码和断言。真实内容范围为 0..1000，初始已准备覆盖为 0..300，两次已接纳输入依次为 `+250` 非平滑和 `+100` 平滑。唯一变化是同 epoch 的准备覆盖在第二次输入之前或之后到达。两种时序最终都应到达 350，任何中间视觉位置都不得越过当时准备完成的覆盖。临时覆盖边界不是实际内容边界。

首次隔离运行在 x86/x64 均得到 early 约 350、late 约 300，进程均返回 1。源码中的 `current_source_red_expected: true` 是该冻结基线的预期元数据，不是当前源码执行结果；程序仍按实际断言返回 0 或 1，不将失败倒置为通过。

`target_retention_boundary_tests.cpp` 的默认程序包含 19 个边界：停靠时不空转、准备覆盖到达后恢复及重新锚定时间、反向输入、非零基点下的负向累计、非平滑输入累加未完成目标、双轴及未参与轴插值、真实内容边界、新 epoch、显式 barrier、撤销资格、宽或高为零、方向覆盖无余量、按候选目标接纳反向输入、真实边界饱和但仍有未完成目标、取消回到基点、真实范围缩小、准备刷新保留预测、连续确认不阻断在途插值。测试使用固定的虚拟单调时间，不用 wall-clock 等待。

这些用例只证明 Controller/Snapshot 层。它们不覆盖 LayerTreeHost 的实际资源 fence/唤醒、原生输入桥接、主线程 fallback、画面呈现或安装器的主观流畅性；需要另外的集成和 GUI 验证。

## 构建与运行

以下命令从仓库根目录执行。Windows、Visual Studio 2022 v143、Windows SDK 10.0.19041.0，Release 静态 CRT `/MT`。构建不会执行测试；实际运行须遵守项目的隔离环境约束。

```powershell
cmake -S tests/native/scroll_target_retention -B ../scroll-target-tests-x86 -G "Visual Studio 17 2022" -A Win32 -T v143 -DCMAKE_SYSTEM_VERSION=10.0.19041.0
cmake --build ../scroll-target-tests-x86 --config Release --parallel 2
cmake -S tests/native/scroll_target_retention -B ../scroll-target-tests-x64 -G "Visual Studio 17 2022" -A x64 -T v143 -DCMAKE_SYSTEM_VERSION=10.0.19041.0
cmake --build ../scroll-target-tests-x64 --config Release --parallel 2
```

获准的隔离运行环境中，分别执行输出目录 `Release/` 下的 `soui_scroll_target_retention_tests.exe` 和 `soui_scroll_target_boundary_tests.exe`。有完整源码构建目录时也可使用：

```powershell
ctest --test-dir ../scroll-target-tests-x86 -C Release --output-on-failure
ctest --test-dir ../scroll-target-tests-x64 -C Release --output-on-failure
```

保留完整 stdout、stderr、退出码、源码与 EXE 散列和测试架构。任何用例失败都保留真实非零退出码；编译通过不证明运行通过。原有 `tests/native/scroll_coverage` 的 13 项回归独立执行，不改其断言来迁就新结果。

## 独立的覆盖收缩反例

默认关闭的 `SOUI_BUILD_SCROLL_CONTRACTION_DIAGNOSTIC` 会额外生成 `soui_scroll_target_coverage_contraction_tests.exe`，只运行 `coverage-shrink-expand-no-replay`。同 epoch 覆盖突然缩小到不包含当前视觉位置，再恢复覆盖时，不应重放旧 consumed offset 或发生反向跳动，最终仍应保留已接纳目标。这是旧基线也存在的校正边界，不属于本轮固定尺寸安装器目标保留修复的已解决范围。

```powershell
cmake -S tests/native/scroll_target_retention -B ../scroll-contraction-diagnostic-x86 -G "Visual Studio 17 2022" -A Win32 -T v143 -DCMAKE_SYSTEM_VERSION=10.0.19041.0 -DSOUI_BUILD_SCROLL_CONTRACTION_DIAGNOSTIC=ON
cmake --build ../scroll-contraction-diagnostic-x86 --config Release --target soui_scroll_target_coverage_contraction_tests
```

该诊断保留正常的正确性断言，没有 `WILL_FAIL`、跳过或放宽。2026-10-23 隔离运行确认：旧覆盖基线与目标保留首修复源码的 x86/x64 四个程序均返回 1，在扩覆盖时重放了旧累计偏移。这里的“首修复”对应 `236248...` Controller 源码，不是后来修正真实边界 epsilon 的 `3C0D02...` 源码。主回归最终采用后者并在两架构通过原始 2 项、边界 19 项和旧覆盖 13 项；两组结果必须分开解释，详见[本轮维护与验证记录](../../../docs/maintenance/scroll-target-retention-20261023.md)。独立诊断不能计入主回归通过数，也不能将主回归通过扩称为任意 viewport 变化或覆盖收缩均已解决。

明确比较旧源码时，可在一个新的构建目录额外指定 `-DSOUI_SCROLL_KERNEL_ROOT=../preserved-baseline/third_party/sources/miniblink`。这个路径同时选择生产源码与头文件，默认仍使用本 checkout；必须记录实际基线路径、源码散列、EXE 散列和退出码，不用旧实现结果替代当前实现验收。
