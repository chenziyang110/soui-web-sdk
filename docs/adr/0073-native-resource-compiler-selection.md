# ADR-0073：LLVM profile 固定 Windows RC 初始化选择

- 状态：proposed
- 日期：2026-10-13

## 背景

当前完整 SDK 的默认 ALL 实际构建在模板资源编译失败。CMake 4.2.3 cache 的 `CMAKE_RC_COMPILER` 已是固定 Windows SDK 10.0.19041.0 的 x64 `rc.exe`，生成规则却使用 `cmake_llvm_rc` 并把 Clang depfile 参数传到 RC 一侧。

实际安装的 `Platform/Windows-Clang.cmake` 在 `CMAKE_RC_COMPILER_INIT` 为空时仍按 PATH 找 rc/llvm-rc，并以 INIT 或 COMPILER 任一匹配 llvm-rc 选择 LLVM 资源预处理。当前工具链只绑定 COMPILER，因此两个选择可不一致。旧失败的 `.res.pp`/`.res.d` 缺席，不能据此推断失败子阶段已经执行。

## 决定

同时把 `CMAKE_RC_COMPILER_INIT` 绑定到既有同一个 SDK `rc.exe` 路径，使用原严格 `_soui_llvm_bind` 拒绝冲突。只固定初始化选择，不改 CMake 安装、不覆盖资源规则、不删除 dep tracking，不换 compiler/SDK，不改变模板、资源、厂商 gates 或预算。

保留 LLVM 22.1.3、MSVC 14.29、Windows SDK 19041、6.01 和全部原输入认证。后续必须从新源码独立重算请求/工具链 pin，并使用 fresh build；旧失败与旧库不改写，不把 source-only 选择检查当实际资源成功。

## 验证与范围

Node 数据回归读取真实工具链，检查两个 RC 选择都严格绑定同一 SDK 文件且没有自定义规则绕过。Root 独占执行实际资源探针、fresh CMake 生成和原默认 ALL/Install；应核生成规则没有 LLVM RC wrapper/Clang depfile 参数流入 Windows RC，并核原资源真正编译/链接及正常关闭。

本变更未运行 CMake、RC、编译器、SDK、GUI 或 Native 测试。实际 DefaultALL、模板/Workbench、安装、ABI 与发布资格仍待验。
