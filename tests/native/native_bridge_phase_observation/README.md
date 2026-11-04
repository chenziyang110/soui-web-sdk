# 原生 Bridge 分段观测合同

本 fixture 直接包含生产私有 `native_bridge_phase_observation.h`，不复制统计算法、不链接 Host、不加载内核，也不创建窗口或输入。`generate.cjs` 从实际 Host 原字节提取 `native_bridge_phase_json` 函数编译进 fixture；同时保留三个 scope 与诊断出口的源码片段、offset/hash，检查原前后 guard 的顺序。对应 ADR-0159；默认关闭不读新增时钟和不分配统计块的 Host 配置责任与本 helper 的 null-scope 责任分开。

测试时钟是静态编译期 policy；scope 热路径没有 `std::function`、动态 callback 或时钟实例。唯一快照 Probe 为冷静态模板，生产调用使用 inline no-op。测试友元只在 fixture 定义，用于极限初值和受控读取交错；不使用改变生产类定义的宏，不增加公共安装头或 ABI。

26 个 CPU 合同覆盖：关闭配置/null scope、冷绑定、unsupported atomic、三段独立桶、正常返回/异常及已在展开中的正常清理、同段及跨段重入 inclusive wall-time、仍有 outer scope 时读取、旧统计块退休、错误 owner、未绑定/非法 phase、坏频率/时钟、counter/sum/bucket/sequence 溢出、最多3次一致读取、reader 并发故障与自然 scope 退还。QPC 可用时零 duration 是合法桶0；QPC失败不能伪造零 duration。模拟 unsupported 模式不证明实际架构的 atomic 属性，实际支持由编译器 trait 和最终 x86/x64 构建提供。

另5项执行实际生产 serializer：极限uint64字符串精度及3×64桶/8KiB、可用source的合法0、真实helper故障的null phases/缺失QPC、超过8KiB拒绝、旧script从标记到末尾的JSON解析。合计31项。最坏uint64 fixture仅为格式长度边界，并不声称这些人为counter组合是一轮可用观测。

独立构建入口（实际执行须绑定工具链和新输出目录）：

```powershell
cmake -S tests/native/native_bridge_phase_observation -B ../native-phase-contract-out
cmake --build ../native-phase-contract-out --config Release
ctest --test-dir ../native-phase-contract-out -C Release --output-on-failure
```

SDK 根构建接入由根 CMake owner 维护。本目录目标为 `soui_native_bridge_phase_observation_tests`，CTest 名为 `sdk.native.native-bridge-phase-observation`，单次15秒上限。工具链、actual source/EXE 身份与真实输出须由执行者留存；README 不是执行成功记录。

源码顺序合同只证明当前原字节的选取与相邻 guard/调用次序，不执行 mounted Host、导航/关闭或实际JS。script后缀合同组合实际serializer输出与旧格式；完整getter的disabled/lifecycle行为仍需独立整合审查。以上不证明物理输入或性能收益。计数是 inclusive native 调用 wall-time，不能相加成 CPU 占比或据此声称 Electron 对齐。
