# 可选 Loader 的独立安装消费者

本目录只通过安装后的 `Soui::AutoHost` 和公共头文件编译。把本目录复制到仓库外，
使用 `SouiSdk_DIR` 显式选择同架构的新安装前缀，再构建 Release。
运行时传入一个新建的空目录绝对路径；程序验证实际 Runtime 发现得到明确不可用，
不创建浏览器、Host 或业务窗口。

动态档的 `soui_deploy_webview2_loader()` 复制安装前缀中的匹配 DLL 到 EXE 旁；
静态档为 no-op。每个档位使用不同安装前缀和应用目录。通过 `dumpbin /imports`
检查动态档 EXE 没有 `WebView2Loader.dll` 和 `EventSetInformation` 硬导入，并核对
复制 DLL 与已认证包的逐字节身份。移开 DLL 后再运行应仍返回明确不可用；恢复后
以新进程重新观察。静态档保留原链接方式，作为导入表对照。

这些检查只证明安装、链接和启动前发现边界，不代表真实 Win7、完整 AutoHost
页面启动、WebView2 创建、长期或生产验收。缺导出、坏映像、文件锁和模块持有的
原生回归另见 `tests/native/webview2_loader`。
