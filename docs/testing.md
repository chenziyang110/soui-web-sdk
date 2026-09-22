# 测试与验收

完成构建后执行：

```powershell
powershell.exe -File tools/Test.ps1 -Architecture x64 -Configuration Release
```

覆盖 SDK 包校验、冲突与回滚；Web SDK 请求关联、错误和超时；原生桥接注册、会话、令牌与来源校验；模板和 Workbench 的实际启动、页面就绪、桥接调用及关闭。`-NativeOnly` 仅执行桌面启动验证。

桌面测试需要可交互 Windows 会话，脚本串行运行并使用互斥锁。结果在 `out/<架构>/smoke-<配置>.json`，绑定构建 ID、源码身份及全部 EXE/DLL/HTML/JS/CSS 资源。模板、Demo 默认入口、Demo React 入口分别验证。单元测试不能替代真实宿主启动。

`tools/Build.ps1` 在开始构建时使旧成功记录失效；结束时检查编译输入未改变并记录载荷。`-SkipWeb` 只接受源码及 Web 输出均与上次构建匹配的记录，首次构建或文件改变后必须完整构建。通过 CMake 直接编译可以调试，但不能代替这些打包所需证据。改变页面、DLL、编译输入或目标集合后，测试和打包都会要求重新构建。

`node --test tests/sdk/delivery-identity.test.cjs` 验证源码换行、文件集合、路径安全、资源篡改、部署清理、安装字节身份和旧证据失效。

外部 CMake 接入使用 `tests/package-consumer/`：先安装 SDK，再配置 CMAKE_PREFIX_PATH 指向实际的 `out/install/x64`，以相同工具集构建并运行。SDK 单独构建和外部链接分别验证。

本次交付结果见 `verification/acceptance.json`。未执行项不能用历史结果补齐。生产验收另需目标 Windows 实机、DPI/多屏/输入法、持续运行、重复关闭、重载内存、安装升级与回退、故障恢复验证。

完成四组构建、依赖重编译与运行、外部 CMake 和独立项目验证后，维护者可以运行 `node tools/Collect-Verification.cjs` 汇总当前交付结果。该入口会重跑单元测试，核对运行产物散列，并在证据缺失时失败；它不会把未执行项自动标为通过。

0.3 基础切片可使用 `node tools/Collect-Verification.cjs --foundation`：要求 x86/x64 Release 的三种启动场景及四个单应用运行包；重跑单元与原生契约测试。结果明确为 `scope: foundation`、`sourceDeliveryVerified: false`，不等同于完整候选验收。汇总失败会留下未验收状态，不能继续引用历史绿色结果。汇总会更新证据文件，需随后刷新完整源码清单。
