# @APP_NAME@

最小 SOUI 统一桌面项目，应用版本 @APP_VERSION@，标识 @APP_IDENTIFIER@。
此版本属于应用自身，不代表 SDK 版本或正式发布。

## 配置与构建

先准备与目标平台/架构匹配的已安装 SouiDesktop 开发 SDK、C++17 工具链、
CMake 3.24+ 和 Node（仅生成/staging 工具需要，最终应用/doctor 不需要）。
SDK 前缀由配置命令传入，项目中不保存开发机绝对路径：

```sh
cmake -S . -B build -DCMAKE_PREFIX_PATH="<已安装SDK前缀>" -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --target @APP_NAME@
```

Windows 使用兼容 MSVC 的工具链；macOS 在 project() 前默认最低12.0，若已安装
SDK 要求更高版本，请配置 CMAKE_OSX_DEPLOYMENT_TARGET。交叉编译应传入正确
toolchain，生成器不下载 SDK、不安装依赖、不猜测编译器或系统权限。

运行构建目录中的 @APP_NAME@（Windows 为 .exe，多配置生成器通常放在 Release
子目录）。程序通过 SDK 获取实际可执行文件旁的 web，不依赖启动工作目录。
每次构建应用目标均刷新 web；删除的旧构建资源不会自动清理，交付时使用下方
新目录 staging，避免将旧文件一并部署。

## 最小接入面

- main.cpp 只有一个窗口和 app.info 原生方法 allowlist，不提供任意文件/命令能力。
- 模板显式开启 HTTP 资源服务，SDK 默认行为不变；readAsset 仍默认关闭。
- 前端使用同源 CSP、静态/动态模块和 JSON fetch，无 CDN、内联脚本或包安装。
- 保持前端相对 URL；绝对 /assets 路径不适用于窗口随机前缀。资源目录应为可信、
  不可变的应用资产，不能混入密钥或将同源不可信脚本视为权限隔离主体。

## 应用目录与诊断

```sh
cmake --build build --config Release --target @APP_NAME@_stage
```

生成 build/staged-Release（配置名取决于生成器）。输出必须是新目录，重复 staging
不会覆盖。可重新配置 `-DAPP_STAGE_DIRECTORY="<新的输出目录>"`。

Linux 使用目录内 launch.sh；独立诊断为 `./launch.sh --doctor`。Windows 运行
程序，诊断为 `powershell -NoProfile -File doctor.ps1`，不修改执行策略。
macOS 输出 .app，独立诊断为 `/bin/sh doctor.sh`。manifest 与 DEPENDENCIES.txt
记录复制文件散列和外部依赖要求。

这不是自包含安装器：Linux 需要兼容 GTK/WebKitGTK 及 helper，Windows 需要
WebView2/VC runtime，macOS 使用系统 WebKit。诊断的 unknown 或退出0不证明
依赖已安装、GUI 可运行或架构兼容；目标运行、三引擎模块行为、签名/公证、
部署许可和完整回归仍需验证。SDK/第三方许可不会由生成器复制为应用许可，
分发前须确认适用授权和必要声明。不要关闭沙箱或改变安全设置来伪造通过。
