# MiniBlink 排队请求方法分类契约

此无窗口 fixture 使用生产 `miniblink_request_route.h`，并在每次配置/构建时，从当前 `soui_miniblink_host.cpp` 逐字节提取原 `bounded_bridge_packet`、`packet_string`、`bridge_request_method`。生成目录 `source-binding.json` 记录整个来源和三个片段的 SHA-256，不手工复制这些方法，也不提取/模拟 Host 的线程或会话函数。

41个输入覆盖方法名精确比较、JSON转义/重复键/嵌套、前后缀/NUL/类型错误、512字节方法边界、64KiB消息边界、深度32/33与非法JSON。各例比较 admission 已解析 method 与 direct 原提取及原两种字符串后处理条件。分类期间禁止 C++ heap allocation；另验证非NUL结尾 string_view边界和值捕获不引用旧字符串。Release配置不会删掉断言，失败返回非零。

这只是方法提取/分类契约，不代表 admission 完整字段合法、Bridge授权通过、Host实际调用路径或bootstrap清理/导航并发已经运行。比如某些测试故意使用只有method的JSON，不能把方法分类成功解释为可执行请求。真实接线须由匹配 SDK 的实际 Host bootstrap/probe/异步echo/自然关闭及适用生命周期契约另验。设计见 [ADR-0157](../../../docs/adr/0157-miniblink-queued-method-route.md)。

从仓库根目录独立构建示例（执行前选择当前授权的构建目录和工具链；命令本身不证明已执行）：

```powershell
cmake -S tests/native/miniblink_request_route -B out/miniblink-request-route-x64 -A x64 -T v143 -DCMAKE_SYSTEM_VERSION=10.0.19041.0
cmake --build out/miniblink-request-route-x64 --config Release --target soui_miniblink_request_route_tests
ctest --test-dir out/miniblink-request-route-x64 -C Release -R '^sdk\.native\.miniblink-request-route$' --output-on-failure
```

父 `tests/native/CMakeLists.txt` 只需 `add_subdirectory(miniblink_request_route)`。此目标不链接/启动MiniBlink，不增加SDK运行依赖，不替代真实性能验收或推导提升百分比。
