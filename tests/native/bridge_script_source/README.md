# Bridge 脚本构建的字节等价合同

此 fixture 直接调用生产 `sdk/src/host/bridge_script_source.h`，与变更前真实 Host 的 `json_string_literal`、`bridge_resolution_script`、pending 拼接表达式、即时拒绝表达式逐字节比较。`legacy-source.inc` 由 `generate.cjs --capture` 从维护者保留的原 Host / pending 头提取；不是另写一份参考算法。`legacy-reference.json` 保存来源仓库相对路径、原整文件 SHA-256、片段偏移/hash 和最终 reference hash，不包含开发机路径。

普通 CMake 使用已冻结的小 reference，配置与构建都运行 `--verify` 检查其字节身份，不依赖开发机外部备份。生产 pending 文本只改为 const char[] 以取得编译期长度；参考体保留原 const char* 声明和全部空白换行，合同检查两者内容相同。

16组合同包含空串/null-data view、普通连续字节、全部256字节与逐字节run边界、嵌入NUL、quotes/slashes/控制字节、UTF8与原U+2028/U+2029行为、512B ID、4KiB及接近64KiB普通/重转义response、timeout1/10000/60000和纯构建函数的uint32边界、ordinary/renewal、同时存活的嵌套局部buffer，以及reserve加法溢出在任何大分配前拒绝。它不验证JS执行或引擎生命周期；原resolver/receipt/权限合同仍须按受影响范围保留。

仓库根目录的独立构建入口：

```text
cmake -S tests/native/bridge_script_source -B out/bridge-script-source
cmake --build out/bridge-script-source --config Release
ctest --test-dir out/bridge-script-source -C Release --output-on-failure
```

集成目标为 `soui_bridge_script_source_tests`，CTest为 `sdk.native.bridge-script-source`，超时15秒。CMake最低3.24、C++17；Windows沿SDK的/MD和既有工具链，不引入SDK/内核链接或 GUI。实际执行证据由本次外部构建目录保存，本文件不将编写完成标为测试通过。

可选 `soui_bridge_script_source_tests --benchmark` 在通过同一合同后运行四个固定CPU slice：512B ID pending、4KiB reply、near64KiB reply、4KiB escaped immediate reply。每实现每slice仅128次、一次预热；没有按结果重试/延长循环。输入预构造、固定输出字节与checksum相同。统计普通全局 `operator new/new[]` 的调用与请求字节，记录steady_clock墙钟纳秒；不覆盖所有CRT/OS分配，不等于RSS或峰值存活内存。测量期间不输出日志，最后输出JSON；任何错误非零退出。

`fewerAllocationsObserved` 是该编译器/STL实际结果，不预写成功，不用计时波动作为必过阈值。旧/新固定顺序不是完整性能实验；这只能验证C++脚本构建的局部成本，不能声称降低ADR0159 scope内的jsEval/wkeRunJS耗时、页面FPS、输入响应或达到Electron水平。

若维护者必须刷新参考，使用真实未修改的原Host和pending头，并输出到一个新的空目录：

```text
node tests/native/bridge_script_source/generate.cjs --capture <baseline-host.cpp> <baseline-pending.h> <new-reference-directory>
```

普通测试不运行capture，也不得用当前优化后的Host自动替换reference来消除差异。
