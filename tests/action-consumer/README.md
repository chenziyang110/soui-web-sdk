# 安装后生成动作消费验证

该工程仅通过 `find_package(SouiSdk COMPONENTS ActionJson)` 引用安装目录，不引用 SDK 源码、Demo 或私有头文件。`contracts/actions.json` 是测试动作的唯一来源；生成输出与模板一样检查身份。测试 bootstrap、公共注册/调用、安全整数/Unicode/可选 null/嵌套数组编解码、绕过 TS 的原生输入检查、发现、白名单拒绝、错误输出与异常。不启动 GUI，不证明目标系统支持。

从交付根目录，先显式生成并检查，再将 consumer 复制到仓库外的新目录：

```powershell
node tools/sdk/actiongen.cjs --source tests/action-consumer/contracts/actions.json --root tests/action-consumer
node tools/sdk/actiongen.cjs --source tests/action-consumer/contracts/actions.json --root tests/action-consumer --check
cmake -S sdk -B out/actiongen-sdk-x64 -G "Visual Studio 17 2022" -A "x64,version=10.0.19041.0" "-DCMAKE_SYSTEM_VERSION=10.0.19041.0"
cmake --build out/actiongen-sdk-x64 --config Release --target soui_sdk --parallel 2
cmake --install out/actiongen-sdk-x64 --config Release --prefix ../actiongen-installed-x64
Copy-Item -LiteralPath tests/action-consumer -Destination ../actiongen-consumer-x64 -Recurse
$actionInstalledPrefix = (Resolve-Path -LiteralPath ../actiongen-installed-x64).Path
cmake -S ../actiongen-consumer-x64 -B ../actiongen-consumer-x64-build -G "Visual Studio 17 2022" -A "x64,version=10.0.19041.0" "-DCMAKE_SYSTEM_VERSION=10.0.19041.0" "-DCMAKE_PREFIX_PATH=$actionInstalledPrefix"
cmake --build ../actiongen-consumer-x64-build --config Release --parallel 2
ctest --test-dir ../actiongen-consumer-x64-build -C Release --output-on-failure
```

目录必须是新建的；完整搬移安装前缀后重新配置 consumer 可检查 CMake 可搬移性。x86 使用新构建/安装/consumer 目录，将平台改成 `Win32,version=10.0.19041.0`。本切片的实际执行记录由整合者保存，不能把上述步骤当成已执行证据。
