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
每次构建应用目标均同步受管 web，源中已删除的文件也会从旧受管副本移除。
工具拒绝接管已有无清单目录，拒绝手工改写/新增文件或链接；不要把用户数据放在
构建 web 中。旧模板迁移请选新的构建目录，不要删除所有权清单来绕过校验。
运行中的应用应先停止再构建；这不是热更新机制。交付仍使用下方新目录 staging。

## 最小接入面

- main.cpp 只有一个窗口和 app.info 原生方法 allowlist，不提供任意文件/命令能力。
- 模板显式开启 HTTP 资源服务，SDK 默认行为不变；readAsset 仍默认关闭。
- 前端使用同源 CSP、静态/动态模块和 JSON fetch，无 CDN、内联脚本或包安装。
- 保持前端相对 URL；绝对 /assets 路径不适用于窗口随机前缀。资源目录应为可信、
  不可变的应用资产，不能混入密钥或将同源不可信脚本视为权限隔离主体。

## 前端类型与调用

web/soui/desktop.mjs 是 SDK 的薄适配层，app.mjs 已通过它调用 app.info。相邻
desktop.d.mts 提供可选 TypeScript/编辑器提示，不要求安装包或编译 TS。可用
invoke、on、readAsset 和 assetBytes/assetText/assetJson；泛型只提供静态提示，
不代替业务结果校验。无原生宿主立即报错，不模拟成功，也不增加 Bridge 权限。
模板仍未启用 readAsset，需应用显式选择；普通同源资产继续使用 fetch。

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

## 校验已有交付目录

`cmake --build build --target @APP_NAME@_verify_stage --config Release` 只读校验现有
staging 目录，不重新构建或覆盖它。stage目标创建产物后也会自动执行此校验。
输出是结构化结果；文件与自附SHA-256清单一致不等于签名或来源可信，也不表示
系统依赖与窗口运行已验收。不要把报告写入被校验目录，否则会成为额外文件。

## 创建交付归档

已有staging目录校验通过后，构建 `@APP_NAME@_archive_stage`（多配置构建同样传
`--config Release`）。默认产生 `<staging目录>-delivery`，其中有ZIP、SHA-256和
交付清单。归档目标不重新构建或覆盖staging，输出须为新目录；自附散列不是签名，
此步骤也不会自动发布或验证真实窗口运行。详见SDK应用staging文档。
# 安装模板完整性

输出目录不得与模板树或 SDK frontend 适配源树重叠；生成器在创建任何目录前
拒绝此类输出，避免新项目污染后续生成所需的 SDK 源文件。

生成器在创建输出前检查 CMakeLists.txt、main.cpp 及 web 下的 index.html、
app.mjs、feature.mjs、details.mjs、config.json、style.css。缺失必需依赖时
报告缺失路径并拒绝写出半成品；README 等可选文档不作为运行依赖。
