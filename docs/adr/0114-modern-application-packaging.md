# ADR 0114：现代应用打包与中文机器清单

状态：实施中；针对性路由、实际暂存和编码往返已验证，最终公开入口另验。

## 背景与决定

原应用打包入口只接受兼容模板和 Workbench，现代模板缺少公开 ZIP 入口。
增加 `package --kind applications --profile modern` 和 `Package-Applications.ps1 -Profile modern`，
只选择 SouiModernStarter。省略 profile 时保持两个兼容包；非 applications 类型拒绝 profile。
现代暂存必须通过当前构建及实际浏览器退出记录，载荷按 WebView2 校验。

PowerShell 在系统代码页下读取 Node 的 UTF-8 输出，曾把中文路径改成乱码，使 ZIP JSON 清单失效。
build-evidence 的控制台 JSON 将非 ASCII UTF-16 码元写成 JSON 转义；解析后恢复完整字符串，包括代理对。
磁盘清单编码、文件内容和 SHA-256 算法保持不变，不修改系统或进程代码页。

## 兼容性与备选方案

默认包名和内容不变。现代包只包含应用载荷，共享 WebView2 Runtime 单独安装；这不构成固定 Runtime 离线包。
不通过全局编码设置修复机器协议，也不复制旧启动标记替代当前退出证明。

## 验证与迁移

验证档位路由、错误参数、生成项目/仓库目标、真实启动证明失败传播、Unicode 往返。
实际现代暂存和 ZIP 检查覆盖两个架构；实际 PowerShell 往返复现旧清单失败并确认修复后的散列。
原失败包保留，最终候选从新的暂存与验证记录重新生成。
