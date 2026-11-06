# SDK owner loop 行为回归

测试直接引用 SDK 的私有调度头与公共 `windows_owner_loop`，不保留另一份策略实现。policy 用例延续私有原型的 worker、queued/pending、旧 token/timer、modal、异常、关闭及取消失败反例，并检查 ack-only 和临时/永久回退。Core 用例覆盖真实公开循环、被动取消息观察、嵌套、队列为空时服务、持续业务 idle、真实 OS timer、modal 返回和实际弱 owner 寿命。

全部窗口隐藏或为 message-only，不注入用户输入，不激活窗口。真实 timer 用例验证系统选择机会，不是帧率、输入延迟或物理显示验收。失败不得通过放宽资格或增加时限绕过。

仓库正常构建链接 `Soui::Sdk`。也可从根目录执行有限源码检查：

```powershell
cmake -S tests/native/owner_continuation -B out/owner-loop-contracts -G "Visual Studio 17 2022" -A x64 -T v143 -DCMAKE_SYSTEM_VERSION=10.0.19041.0
cmake --build out/owner-loop-contracts --config Release --target soui_owner_continuation_tests
ctest --test-dir out/owner-loop-contracts -C Release --output-on-failure
```

独立配置只编译当前 `sdk/src/host/windows_owner_loop.cpp` 与两个测试文件；不能据此宣称 SDK 安装包、SOUI 模板、真实 WebView2 或性能已通过。实际执行须遵守仓库编译资源与助手生命周期规则。
