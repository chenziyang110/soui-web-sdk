# ADR-0072：公开预览显式选择 LLVM 验证工具

- 状态：proposed
- 日期：2026-10-13

## 背景

`tools/Soui.ps1 dev` 默认读取已准备的 MSVC v1 验证工具。当前 LLVM 验证工具使用 v2 记录；仅指定 `--devkit-root` 不能把该记录当作 v1 使用。Vite 6.4.4 的精确允许版本已另行补齐。

## 决定

dev 增加现有六路径参数，并且只在明确传入 `--native-toolchain LLVM22MSVC142` 时使用当前本地 `llvm-build-observer.preparation`。默认 v1 路线不变，不自动准备工具，不修改或移植请求、报告及 runner。

显式路线要求所选架构的真实 Release/v2 prepared 记录、stock 自己拥有的 request/report raw pins；六个 CLI 路径必须分别等于该已认证请求的 LLVM、MSVC、Windows SDK、Ninja、CMD、PowerShell 选择。既有 observer/measurement reader 继续实际检查请求、源码指纹、全部工具与观察记录。其合法 v1 fallback 在显式 LLVM dev 中拒绝。预览结束后再次读取同一准备，并要求 request/report raw pins 未变。

继续使用原 localhost、strictPort、duration 和 Owned Job 调用。dev 默认 duration 900000ms、范围 1000–3600000ms；worker Job 预算为 duration+15000ms，默认 drain 2000ms。成功仍须正常 budget close、自然 0/0、Job0、无强制清理。runner 只能来自所选 prepared 记录。

Soui 入口保持原三个环境键的保存/恢复；忙租约立即拒绝，abandoned 时记录已取得 ownership 后立即拒绝，finally 仍释放并销毁。环境恢复失败也不能跳过释放租约。此处不增加新控制器或租约。

## 兼容与验证

未知 profile、缺少或漂移的六路径、架构不符、v1 fallback、未 prepared 和前后 raw pin 改变均拒绝；原 MSVC 参数、其他命令与预算保留。新增纯 Node 数据回归先 RED 后 GREEN；PowerShell 只做静态语法检查。

后续由 Root 在完整当前源码上实际准备工具并运行公开入口，检查真实 HTTP 响应、拒绝外部文件/Host、关闭及 Owned 原件。数据测试不证明真实 preview/HMR、Native Bridge、桌面窗口、编译器、长期资源或发布资格。`qualificationPassed`、`releaseReady` 保持 false。

## 备选方案

复制 v2 为 v1、绕过 prepared reader 或任意指定 runner 都会破坏现有身份合同，故不采用。直接启动内部 worker 不能代替公开入口验证。
