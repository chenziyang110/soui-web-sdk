# PR #103 当前候选：Win32 安装与编译链接验证

> 归档说明：这是旧 source tree `98cb4681032285309841293d65f0041344aa5f14` 的脱敏历史 Windows Release build-only 记录。原文“当前”“本轮”“未提交”等以记录当时为准，不验证 2026-11-02 恢复候选（标识前缀 `e43a79`）。原始技术结果、工具退出码和 SHA 保留；内部记录已删节。

已完成真实 build-only 验证：Runtime 静态库构建、干净安装、纯安装包消费者编译/链接、全部 21 个安装公共头的聚合对象编译，以及原统一模板两个 Windows 翻译单元的对象编译均通过。9 个阶段的工具退出码全部为 0。没有启动任何项目 EXE，没有 CTest 或 GUI 运行，因此不是 runtime 验收通过。

## 源码和设备身份

- 源码独立目录：`<SOURCE_ROOT>`
- 当前候选索引 tree：`98cb4681032285309841293d65f0041344aa5f14`
- HEAD：`9e4e9c1fdeacc0eae8318a1bf40a04e8c1da97e7`
- 固定 main / MERGE_HEAD：`0cdc0a9ace0cdce664ec4a9b54e5f8f71b97ba48`
- 仍为已暂存、未提交的本地合并候选；本轮未改候选源码或索引，末轮比较完全匹配此 tree，无未合并、未暂存或未跟踪文件。没有追更新 main。
- 源码清单 SHA256 保持 `41cf83f1cf060df6842fc55f9cc5676668944057ac825519efe20b0bb5e9aee5`；版本仍为未发布候选 `0.6.0`，releaseReady=false。此次没有重跑清单工具。
- 全新输出根：`<WORKSPACE>\windows-win32-runtime`；没有复用旧 <PRIOR_BUILD>/<PRIOR_BUILD>/<PRIOR_BUILD>。
- 设备 <HOST>，AMD64；Windows 版本注册表读数 `10.0.26200.9711 / 25H2`。这不是 32 位硬件原生运行证据。

实际工具：

| 工具 | 版本 / 选择 |
|---|---|
| CMake | 3.31.6-msvc6，现有 VS2022 BuildTools 官方目录 |
| MSBuild | 17.14.60.43110，`Current/Bin/amd64/MSBuild.exe` |
| MSVC compiler | 19.44.35229.0，Tools 14.44.35207，Hostx64/x86/cl.exe |
| Linker | 14.44.35229.0，Hostx64/x86/link.exe |
| Windows SDK | 10.0.19041.0 |
| 目标 | Visual Studio 17 2022 / Win32 / v143,host=x64 / Release |
| 驱动 shell | PowerShell 7.6.5，工具调用禁用登录 profile |

工具版本、源码身份、阶段时间和状态来自原始技术记录；本公开归档不包含原始机器路径或系统查询记录。

## 执行前及生成后审查

根 CMake Portable ON、Desktop/Tests/Template/Demos/WebView2/UIA/Vendor/AutoHost/InputUI OFF，进入 sdk 的 PortableRuntime 后提前返回。可达项目链无 try_run、execute_process、安装 CODE/SCRIPT、外部工程/下载、项目 post-build 或 vcpkg。

官方 CMake 模块已只读审查：架构探测使用 STATIC_LIBRARY；ABI、atomic 和 Threads 探测使用 try_compile，部分生成并链接 EXECUTABLE，但不执行探测产物。CompilerId 只由现有 MSBuild/编译器构建识别产物。标准编译工具及其正常辅助程序可运行，不能把本轮描述为“没有子进程”。

实际生成的 Runtime/consumer/对象项目没有 PreBuild/PreLink/PostBuild 事件；CustomBuild 只包含官方 CMake stamp 检查。实际 cmake_install 脚本只有文件安装、导出处理和安装 manifest 写入；新 ABI 头明确列入。没有构建 ALL_BUILD、INSTALL.vcxproj、primitives、CTest、原模板 assets 或 stage 目标。

隔离在首次 project() 前生效：

- 过程级清除继承 CMAKE_*、VCPKG_*、compiler/linker launcher、CC/CXX/flags、CL/_CL_/LINK/_LINK_、include/lib 路径和导入 hook；不改用户或系统配置。记录中未发现这些继承变量。
- 明确空 toolchain/module path/project include hooks/user make-rule override；固定 VS instance、Win32 和现有 Windows SDK。关闭用户/系统 package registry。
- UserRootDir、MSBuildUserExtensionsPath、VCLibPackagePath、VcpkgRoot 全指向新任务空目录；关闭 vcpkg、manifest、artifacts integration 及 Directory.Build/user wildcard imports。属性同时写入过程环境、CMake globals 和 MSBuild /p。
- 最终构建使用 `--parallel 1 /m:1 /nodeReuse:false /noAutoResponse`；禁用节点复用。配置探测所用官方 MSBuild.rsp 已只读确认只有注释。
- 安装显式使用新任务前缀 windows-win32-runtime/ri，并清除过程 DESTDIR。生成脚本中的默认 Program Files prefix 未用于安装。

具体参数及各阶段退出状态保存在原始机器记录中；本公开归档仅保留技术摘要。没有使用全局 vcpkg 脚本或改变外部依赖。

## 实际执行阶段和命令

下表给出已执行命令的核心参数摘要和真实退出码。原始机器命令记录未随本公开归档复制；目录以稳定逻辑标识表示。

| 阶段 | 实际核心参数 | 退出码 |
|---|---|---:|
| runtime-configure | `-S <candidate> -B windows-win32-runtime/rb`，Portable ON，其余产品/Tests OFF，固定 Win32 工具链 | 0 |
| runtime-build | `--build windows-win32-runtime/rb --config Release --target soui_runtime --parallel 1` | 0 |
| runtime-install | `--install windows-win32-runtime/rb --config Release --prefix <task>/windows-win32-runtime/ri` | 0 |
| consumer-configure | `-S windows-win32-runtime/consumer-src -B windows-win32-runtime/cb`，RUNTIME_PREFIX/CMAKE_PREFIX_PATH/SouiRuntime_DIR 均只指新安装 | 0 |
| consumer-build | `--build windows-win32-runtime/cb --config Release --target runtime_install_headers_linkcheck --parallel 1` | 0 |
| template-object-configure | `-S windows-win32-runtime/template-object-src -B windows-win32-runtime/tb`，CANDIDATE_ROOT 固定 | 0 |
| template-object-build | `--build windows-win32-runtime/tb --config Release --target unexpanded_template_objects --parallel 1` | 0 |
| allheaders-configure | 在同一 consumer project 只增加安装头聚合 OBJECT 目标并重新配置 | 0 |
| allheaders-build | `--build windows-win32-runtime/cb --config Release --target installed_runtime_header_compileall --parallel 1` | 0 |

每阶段原始 stdout/stderr 和 execution JSON 按上述 stage 名保存。Windows Threads 的 pthread 库未命中后正常确定 Threads=TRUE；ABI/atomic 编译链接探测成功。Runtime 的 C4244 警告及未使用的两个兼容 registry 变量警告均保留，没有删警告或放宽断言。

## 干净安装及消费者来源证据

安装共 29 个文件，所有 install_manifest 目标都在新 windows-win32-runtime/ri 中。21 个公共头（包括 runtime_metrics.h、view_observation_abi.h）全部与当前候选源码的对应文件逐字节、SHA256 一致。生产库与安装库同为 6,715,670 bytes，SHA256：

`d982fac0b10469280b6138840540829e79def5a011eec7b927322daa88ceae43`

消费者 fixture 在任务外置目录中，不改候选源码；没有 enable_testing/add_test、自定义运行命令或 post-build。它：

- 显式 include runtime_metrics、view_observation_abi、task_store、permission_store。
- 以 `find_package(SouiRuntime 0.6.0 EXACT CONFIG ... NO_DEFAULT_PATH)` 仅查新 prefix，并严格断言 Config、include 目录、Release archive 都来自该 prefix。
- 编译期断言 ABI 版本、Snapshot 标准布局、104 字节大小及字段偏移/reader 类型；本轮只归 Win32/MSVC，不扩大为其他 ABI 证明。
- 在 main 中保留真实非 inline 构造、析构、quota 和 metrics 方法引用，实际链接了新安装库。main 没有运行，所以方法返回值和该程序退出码均没有运行证据。

实际 `/sourceDependencies` JSON 列出 129 个 include，其中 11 个项目头全部来自 windows-win32-runtime/ri/include，源码 SDK、旧 <PRIOR_BUILD>、旧 checkout 回退均为 0。实际 vcxproj 和 /VERBOSE:LIB 链接日志指向 windows-win32-runtime/ri/lib/Release/soui_runtime.lib。相关证据：`consumer-project-audit.json`、`consumer-dependency-identity.json`、`../cb/package-origin.txt`、`../cb/consumer-deps.json` 及原始 CL/link tlog。

消费者产物仅解析文件字节：I386 0x014c、PE32 0x010b、Console 3，37,888 bytes，SHA256：

`f20207ff04eb4c2de55e8231d67f2e1a83a786812767b83c16dc0841fcecd321`

全部安装头的补充 OBJECT fixture 显式 include 21/21 个安装头，并断言完整版本常量为 0.6.0。实际编译依赖共 165 项，其中 21 个项目头全部来自新安装前缀，无源码回退。这补充了消费者只实际使用 11 个项目头的覆盖限制。结果在 `all-headers-dependency-identity.json`、`../cb/all-headers-deps.json`；没有入口点或应用执行。

## 模板和 Desktop 边界

原统一模板 Windows 的 main.cpp、startup_dialog.cpp 已按原文件直接完成对象编译，未替换 @APP_* 字符串，没有调用生成器、资产同步或 staging，也未链接 Desktop。VS 的 OBJECT 项目除对象外产生内部 .lib；这不是完整 Desktop 库或应用链接验收。

| 原模板对象 | 格式 | SHA256 |
|---|---|---|
| main.obj，112,525 bytes | I386 COFF | `6631e87c1c67b17787f3390cfbc416bc87b2d007edbeecba6ef9507e5923c34c` |
| startup_dialog.obj，2,009 bytes | I386 COFF | `5ed9c7a954748202d8e83e117ce36f4affedc833eeca7eb86ee4ed0f41f02ded` |

这些编译输入包括原模板本地头和源码中的 desktop/app.h；不冒称来自已安装 Desktop SDK。Runtime 消费者的严格安装来源证明与此对象检查分别记录。

完整 Windows Desktop configure/install/link **未尝试**：任务内没有 WebView2.h、WebView2LoaderStatic.lib 或官方包；精确 NuGet 缓存 `<CACHE>/webview2-sdk` 不存在，SOUI_WEBVIEW2_SDK_ROOT、NUGET_PACKAGES 为空。缺少完整官方 SDK 是前置依赖阻塞，不是权限拒绝。源码声明的可选版本 1.0.4258.31 和包 pin `56f7f4b8bf9aee4b8efefbbdd4f67d5f74ebd1b100ed0806da71bf76af481aa9` 仅为声明，没有实际包可核 hash。本轮没有下载安装，也未构造替代 SDK 或假包。

原模板正式 CMake 应用目标会强制执行资产同步，故本轮没有调用该目标。纯对象结果不能替代生成模板、安装版 Desktop 消费、原生平台或 GUI 验收。

## 保留状态和剩余工作

新 build-only 安装/链接证据已关闭本轮 Win32/MSVC/Release 的 Runtime 漏装头验证缺口。其他架构/配置及 Linux/macOS 没有本轮实测，不继承历史成功。当前 tree 的运行门槛和发布版本协调仍未关闭。

7 项历史证据与二进制的散列保持不变。原 4551 技术错误记录保留，原 primitives 和 consumer 产物未修改或运行。

本阶段项目 EXE 启动数为 0，CTest 调用数为 0；没有应用 PID、输出或退出码。旧 primitives 的 4551 技术错误仍有效，旧 consumer 底层启动原因未知。新 EXE 仅解析文件字节，未加载运行。

此阶段完整 Desktop 安装/链接因缺少完整官方 WebView2 SDK 未尝试，取得依赖后的结果由补充报告记录。运行验收未执行。
