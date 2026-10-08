# ADR-0023：显式选择并核对固定 Windows SDK

- 状态：proposed，随 0.5 工具链固定切片提交评审
- 日期：2026-10-08
- 范围：源码 SDK、应用、独立验收和发布消费者的 Visual Studio 配置入口

## 背景

仓库约定使用 Windows SDK 10.0.19041.0，最低 CMake 为 3.24。原构建入口仅传入 `CMAKE_SYSTEM_VERSION`。已有构建目录重新配置时，其 `CMakeFiles/<版本>/CMakeSystem.cmake` 中的普通变量可能覆盖命令行缓存值；现场缓存虽然记录 19041，生成的协议工具项目实际选择了 26100。CMake 3.27 的 CMP0149 NEW 行为也不再根据此系统变量选择 SDK。另一方面，独立验收直接传入 `version=` 平台选项，该选项直到 3.27 才支持。

## 决定

保持最低 CMake 3.24、MSVC v143、x86/x64 和固定 SDK 版本不变。三个 PowerShell 配置入口共用帮助脚本：CMake 3.24–3.26 使用架构平台和 `CMAKE_SYSTEM_VERSION=10.0.19041.0`；3.27 及以上使用准确的 `Win32,version=10.0.19041.0` 或 `x64,version=10.0.19041.0`，同时保留系统版本参数。低于最低版本或无法识别版本均拒绝。

通过从 3.24 开始支持的 `CMAKE_PROJECT_TOP_LEVEL_INCLUDES` 注入只读断言，在平台选择后、项目编译前检查实际 `CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION` 等于 19041。该文件不覆盖 CMake 的选择结果，不改变策略、平台或最低 Windows 目标；SDK 缺失和旧版本选择回退必须失败，不能只根据缓存或安装目录存在判定固定成功。

已初始化目录的平台、生成器、源码位置或保存的系统版本与本次配置冲突时，在调用配置之前明确拒绝，保留原字节。`-FreshConfigure` 是唯一授权重建缓存的入口，将 CMake 的 `--fresh` 交给用户明确选择；只允许本工作区 `out/` 下的独立生成目录，不接受 `out` 本身、工作区根、逃逸路径或链接。不得自行删除缓存、历史证据、产物或其他工作区目录。

发布验证会升级并回退到历史 SDK。历史 `Build.ps1` 不接受新开关，也不能复用带 `version=` 的平台缓存。因此仅在显式请求 FreshConfigure 时，当前发布工具先验证该项目缓存所有权，再用历史脚本原有的 `x64` 平台和 19041 系统版本执行 `--fresh` 配置，并注入同一实际 SDK 断言；选择结果写入独立的 `<阶段>-legacy-configure.log`。之后调用未修改的历史构建脚本，不向其传入未知参数。未请求刷新时不得自动执行适配配置或清理缓存。

帮助脚本和断言文件属于现有 SDK 管理根 `tools/internal/` 与 `cmake/`，由现有打包、安装和新项目流程携带，不增加安装器格式或额外复制规则。配置失败返回非零，调用方不得继续编译、安装或记录构建通过。

## 备选方案

- 只设置 CMP0149 OLD：依赖已废弃的兼容行为，无法解决已有系统文件覆盖，不采用。
- 统一提高最低 CMake 至 3.27：不必要地改变既有工具链要求，不采用。
- 直接设置 `CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION`：该变量是选择结果，不是可靠输入，不采用。
- 限制最大 SDK 版本但不核对结果：缺少准确版本时可能选到更旧 SDK，不采用。
- 自动清空已有构建目录：可能破坏证据和用户产物，不采用。

## 影响

基础 SDK 和生成项目保留 CMake 3.24 支持。升级帮助脚本后，过去没有准确固定 SDK 的目录会明确失败，用户需确认后重新配置；已准确匹配的平台目录继续增量配置。断言在平台选择完成后运行，不提高 Win7 等运行目标，也不代替链接导入审计和真实目标系统测试。

## 迁移与回退

原入口参数保留，新增可选 `-FreshConfigure`。冲突提示给出需要重新配置的目录；用户检查目录后可显式传入该开关，CMake 仅重建自身缓存和关联 `CMakeFiles/`。新的构建和验证证据必须使用实际固定后的项目，不改写旧配置或验证记录。回退脚本恢复原 SDK 漂移风险，不能据此继续声称准确固定。

## 验证

局部测试覆盖 3.24/3.26/3.27/4.2、x86/x64、最低版本拒绝、旧平台和系统版本冲突、拒绝后原文件字节不变，以及 FreshConfigure 路径和链接边界。发布调用边界回归检查历史/当前脚本的显式刷新和不刷新分支，不执行 fixture 构建脚本或桌面动作。使用临时、无编译的 CMake fixture 验证实际选择断言；本机 Visual Studio fixture 仅配置，不启动 GUI、不编译 SDK、不安装固定包。生成项目搬移验证仅携带帮助脚本和断言，不能代替完整发布交付。

局部入口为 `node --test tests/sdk/cmake-sdk-selection.test.cjs`。完整 SDK、双架构应用、外部安装消费者和目标系统验证仍由集成负责人按最终冻结输入执行。

## 官方依据

- [CMP0149](https://cmake.org/cmake/help/v4.2/policy/CMP0149.html)：策略行为与 SDK 选择时机。
- [Visual Studio 平台版本](https://cmake.org/cmake/help/v4.2/variable/CMAKE_GENERATOR_PLATFORM.html)：3.27 的准确 `version=` 选项与已初始化平台限制。
- [顶层项目注入](https://cmake.org/cmake/help/v3.24/variable/CMAKE_PROJECT_TOP_LEVEL_INCLUDES.html)：3.24 的平台确定后检查入口。
- [CMake --fresh](https://cmake.org/cmake/help/v3.24/manual/cmake.1.html#cmdoption-cmake-fresh)：显式重建 CMake 缓存。
