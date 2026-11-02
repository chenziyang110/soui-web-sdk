# ADR-0138：可选的 WebView2 动态 Loader

状态：proposed；关联 Issue #133、PR #134。源码与短回归不授予 Win7 或生产资格。

## 背景

当前静态 WebView2 Loader 带入 Win7 不支持的 EventSetInformation 导入，进程可能
在 AutoHost 执行后端选择之前失败。官方同版动态 Loader 可作为应用本地可选依赖。

## 决定

增加默认 OFF 的 SOUI_WEBVIEW2_LOADER_SHARED。开启时只从主 EXE 所在目录的
绝对路径加载 WebView2Loader.dll，一次解析发现和创建两个必要导出；任何一个缺失
均使整个 Loader 不可用。成功模块及导出持有到进程退出，不在异步回调期间卸载。
首次失败也固定到进程退出；运行中替换 DLL 不产生新的选择或业务重放。

实际文件/路径不存在及导出缺失可在创建前表示可选后端不可用。权限、错误架构、
损坏映像、策略及未知错误保留失败；既有环境和注册表准入在调用 Loader 前执行。
Runtime 的明确不存在仍沿用原分类，已选择现代后端后的初始化失败不触发降级。

LoadLibraryExW 只使用 DLL 所在目录与 System32 搜索其依赖，不回退 CWD/PATH，
不修改进程全局搜索策略。所用 SEARCH flags 在 Win7 需 KB2533623；缺该支持时
显式加载保留失败。AutoHost 在 Win7 系统选择阶段直接走兼容路径，不调用 Loader，
也不新增 SetDefaultDllDirectories 等较新系统函数的静态导入。依据 Microsoft 的
[LoadLibraryExW](https://learn.microsoft.com/en-us/windows/win32/api/libloaderapi/nf-libloaderapi-loadlibraryexw)
与 [DLL 搜索顺序](https://learn.microsoft.com/en-us/windows/win32/dlls/dynamic-link-library-search-order)。

静态与动态路径分别缓存，允许同一构建目录 OFF→ON→OFF。动态档安装匹配 DLL，
不导出不存在的静态库，也不链接 DLL import library。SDK 提供显式部署 helper，
将已认证的同架构 DLL 复制到指定应用 EXE 旁；基础 SDK 不获得新依赖。恢复器验证
两架构 Loader 字节，LLVM 读取器按实际构建档位认证其消费的 Loader 输入。

构建证据保存实际配置的 Loader 档位及选中输入路径、大小和散列；动态档必须带
该记录，后续 DLL 删除/改写或档位/路径变化使证据失效。旧静态记录保持原始范围，
不追认新 DLL 资格。该散列来自已有恢复/构建流程，不伪造编译或加载进程。

## 备选、兼容与迁移

保留静态默认避免改变已有现代消费者。拒绝改用 DLL 静态导入，因为进程启动仍会
要求 DLL；拒绝延迟到创建时才检查第二导出，否则发现成功之后无法安全重选后端。
采用动态档的应用必须重编整套 SDK/应用并调用部署 helper；不覆盖已发布同版资产。
公共 C++/TypeScript API 不变。回退时关闭选项、重新编译，并保留两档各自部署目录。

## 验证

用实际 DLL 原生夹具覆盖缺文件、各自缺导出、有效调用、失败固定与进程持有；
确认权限/未知 HRESULT 不被解释为不存在。CMake 用例实际配置 OFF→ON→OFF，
检查静态链接、动态导出、安装路径及部署 helper。两架构真实 SDK/安装消费者和
PE 导入另行执行；Win7 启动、GUI、长期稳定及生产认证继续单列。
