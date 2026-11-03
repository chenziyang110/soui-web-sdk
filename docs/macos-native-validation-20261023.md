# macOS 限定原生构建与交付验证：2026-10-23

范围：PR #103 基线提交 `abb69fea`，加本轮 `cmake/SouiPlatform.cmake` 修复。
最终文件 Git blob 为 `25d67764ce8ead437743e29963278a68abd7c7c4`，已与云端
收紧版一致：仅 macOS 编译器架构 ID 接受列表，不忽略空元素，不放宽其他平台。
这是限定验收记录，不是正式发布、签名或全部架构运行通过声明。

## 环境与配置

ARM64 Mac，macOS 26.4.1、Xcode 26.6、AppleClang 21、CMake 4.4.4、
Ninja 1.13.2、Node 22.23.3、Apple SDK 26.5。默认 Command Line Tools 的 SDK
指向缺失目标，故仅在本次 CMake 配置显式选择 Xcode SDK；未修改全局工具链设置。

```sh
cmake --preset desktop-macos-universal2 -G Ninja \
  -DCMAKE_OSX_SYSROOT=/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk
cmake --build out/desktop-universal2 \
  --target soui_desktop_example soui_desktop_contract --parallel 1
```

配置 exit 0；构建 30/30、exit 0，包含 WKWebView Objective-C++ 后端编译。
最终收紧补丁再次配置 exit 0，再次构建 exit 0（no work）。两个程序的 `lipo`
检查均包含 x86_64 与 arm64。只在 ARM64 宿主运行公共契约 CTest，1/1 通过；
它使用 fake backend，不测试 WKWebView、真实窗口或 loopback listener。

## 已安装 SDK 到应用交付

使用本次 SDK 安装目录中的生成器创建独立 `SouiMacVerify` 项目，再配置、构建
universal2 程序，执行 staging、verification 和 archive，均成功。
verification 核对 12 个文件、1,221,480 字节。产物仍是未签名开发包，
`signed=false`、`nativeRuntimeVerified=false`；未运行 Intel slice，未做公证。
本记录不附带或虚构原始日志附件，也不将生成项目构建成功当作 GUI 验收。

## GUI 阻塞与未验证项

普通执行环境下资源 listener 绑定失败；独立 Node `127.0.0.1` listen 探针返回
`EPERM`。经获批的进程提升后越过 listener，随后应用返回
`E_APPLICATION: Could not activate a regular macOS application`。
受支持桌面工具的 `getApp` 两次超时；之后应用列表及 `lsof` 未找到目标程序运行。
因此未完成真实窗口、Bridge 按钮、模块/dynamic import/fetch、窗口关闭验收，
不将此环境激活阻塞定位为已确认源码缺陷，不绕过权限或关闭安全保护。
未修改用户剪贴板。后续需要能够正常激活 regular macOS application 的图形执行会话，
再运行最小窗口与 Bridge/模块链；不能用 fake-backend 结果替代。

构建时磁盘空间问题已缓解至约 18 GiB，不是最终阻塞。源码恢复未物化的
465 个历史 verification 文件不参与本次源码构建；第三方文件的 CRLF 差异未纳入补丁。
最低 macOS 12、独立 Intel 机器及其他系统版本的运行兼容性仍未验证。
