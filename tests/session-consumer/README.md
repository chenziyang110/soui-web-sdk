# 安装后的公共会话消费者

直接 CMake 示例按 3.27 以上准确选择 Windows SDK 10.0.19041.0；3.24–3.26 改用 `-A x64`（x86 为 `-A Win32`），仍保留完整引号的 `-DCMAKE_SYSTEM_VERSION=10.0.19041.0`。已有生成平台不同的目录请选新目录，或检查其所有权后显式 `--fresh`；不能在旧缓存中直接切换平台。

把此目录复制到仓库外，只通过安装前缀的 `Soui::Sdk` 编译，不引用 SDK 私有头或 Demo。

```powershell
cmake -S <独立消费者目录> -B <外部构建目录> -G "Visual Studio 17 2022" -A 'x64,version=10.0.19041.0' -DCMAKE_PREFIX_PATH=<SDK安装目录> '-DCMAKE_SYSTEM_VERSION=10.0.19041.0'
cmake --build <外部构建目录> --config Release
<外部构建目录>/Release/soui_session_consumer.exe
```

它验证 `json_bridge` 显式续期入口、公共原生授权与任务 API、模拟时钟的二十次续期、原结果/订阅保留、幂等操作和撤销。只执行后台线程与系统随机数，不启动窗口或 WebView。模拟的 500 分钟不是真实八小时长稳，也不验证浏览器计时器、休眠、模态窗口或 Win7 兼容性。真实证据必须另行绑定载荷并执行。
