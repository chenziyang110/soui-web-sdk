# WebView2 输出准备：无窗口观测辅助

对应 ADR-0156。`output_timing_policy.h` 为私有事件构造器；不更改公开 trace ABI，不提供 input/frame 因果，也不更改默认prepare/owner预算。Host接线由唯一owner应用，详见ADR和集成补丁说明。helper单测通过不代表Host已经接线。

独立CMake目录可以编译 `soui_webview2_output_timing_tests` 与 `soui_webview2_output_cpu_bench`。前者使用真实recorder/exporter，验证180组元数据和失效边界；可传一个全新绝对输出文件导出合法trace，再用现有reader和本目录`analyze-trace.cjs`复核。fixture自身不创建窗口、COM/WebView或发输入。

`output_observation` 回归直接提取当前 Host RAII 结构的原始字节，校验异常展开、显式取消后再抛异常、重复取消和成功收尾。`projection.json` 记录提取位置与散列。该小回归仅将 state/output 身份和 `note_output` 替换为有界 fake，验证生产 guard 的终态规则；它不执行实际队列、配额、序列化或 COM，不能代替 Host 集成测试。

CPU bench在配置阶段从当前 `webview2_host.cpp` 提取原 `serialize_output`、`parse_bounded`、`wide`，保存在构建目录并写 `projection.json`源hash。候选分支投影使用实际 `owner_output_budget::may_cache`，比较64B、4KiB、约60KiB的ASCII/转义/Unicode response/event；ABBA、每配置128次。每一轮检查最终wire完全相等，raw+缓存不超过现有上限，malformed event仍回退并保留最终失败。

```powershell
cmake -S tests/native/webview2_output_timing -B out/webview2-output-timing -G "Visual Studio 17 2022" -A x64 '-DCMAKE_SYSTEM_VERSION=10.0.19041.0'
cmake --build out/webview2-output-timing --config Release
ctest --test-dir out/webview2-output-timing -C Release --output-on-failure
out/webview2-output-timing/Release/soui_webview2_output_timing_tests.exe '<全新trace绝对路径>'
node benchmarks/protocol/input-frame-trace.cjs '<trace绝对路径>'
node tests/native/webview2_output_timing/analyze-trace.cjs '<trace绝对路径>'
out/webview2-output-timing/Release/soui_webview2_output_cpu_bench.exe > '<全新CPU结果绝对路径>'
out/webview2-output-timing/Release/soui_webview2_output_cpu_bench.exe --compare-guard > '<全新guard对照结果绝对路径>'
node tests/native/webview2_output_timing/summarize-cpu.cjs '<CPU或guard结果绝对路径>'
```

CPU投影的“owner”是同一个CPU线程上的阶段标签；没有真实owner queue、COM、renderer、GPU、呈现或input。其意义是识别预制成功时可移出的工作量，以及cache失败时重复编码的代价；不能用来决定默认开启或报告FPS提升。`task_end→bridge_response`包含接纳等待，纯queue仍看原task_queue histogram；bridge_response不等于COM成功交付。trace有丢记录时只保留观察计数，不输出完整分段分布。

`--compare-guard`只比较prepare已经开启时的旧行为与response大包必败早跳；同时验证Unicode、边界和可压缩空白event保持wire与缓存语义。正式`bridge_response.value`一直保留raw字节数，reader只用task_id/generation关联私有准备事件。
