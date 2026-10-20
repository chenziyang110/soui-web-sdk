# ADR-0123：显式启用的跨平台本地资产读取

状态：开发分支决定，未发布。

## 背景

三个系统 WebView 的 `file://` 页面对 fetch、CORS 和模块加载支持不同。
直接放宽文件或网络安全配置不可接受，三个自定义 URL scheme 也不能未经目标机
验证就宣称拥有相同的 HTTP/ES module 语义。

## 决定

增加默认关闭的 `options.enable_asset_read`。启用后顶层页面通过
`window.souiDesktop.readAsset(path)` 读取资源根内普通文件，返回 Promise，
结果为 `{mime, size, base64}`。路径是严格 UTF-8 的根相对文件路径，不是 URL；
拒绝绝对路径、父遍历、反斜杠、冒号、百分号及 URL query/fragment。
文件最大 256 KiB，返回 MIME 仅按受控扩展名表推断，未知类型为
`application/octet-stream`。二进制 base64 保持原始字节。

保留内置 Bridge 方法 `soui.asset.read`，关闭时明确拒绝，不能转交业务 handler。
使用现有顶层验证、随机令牌、request ID、导航代际、队列/响应大小限制。
不新增监听端口、网络许可、浏览器安全开关、第三方依赖或原生 scheme。
读取属于 UI 线程上的有界本地 I/O；资源目录必须是应用拥有且运行时不被不可信
进程修改的部署目录。规范化和 symlink 根边界检查不宣称解决恶意并发文件替换。
不支持设备、管道、目录；此 API 不适合作为大文件/流媒体服务。

## 兼容性及迁移

原 file URL、Bridge invoke 和业务方法保持不变；只有显式启用的窗口获得能力。
保留方法名是新增的公共命名限制，使用该名称的业务代码必须迁移。
目录选择不扩大资产根权限，各窗口使用自己的根和会话。此切片解决配置/文本/
二进制资产读取，不拦截 fetch，不实现原生 ES module、HTTP、Range 或 CORS。
未来 URL 服务须独立评审和真实引擎验收，不能由此实现推导通过。

## 验证

公共 C++ fake-backend 契约覆盖启用/禁用、根边界、symlink、特殊路径、大小、
MIME、字节、窗口和会话隔离。真实生成 preload 的 VM 测试覆盖三后端共用调用、
拒绝与会话生命周期。Linux 编译/链接；Windows/macOS 与真实 GUI 仍须目标机验收。
