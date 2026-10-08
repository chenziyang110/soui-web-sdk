# ADR-0015：preview.10 Web 依赖维护

日期：2026-10-08。状态：采用。

## 背景

发布前依赖审计发现 Tailwind 3 的 braces 构建链告警，以及 source-map-js 和 Vue 服务端渲染器的新增告警。当前 Workbench 使用客户端渲染，仍保留完整锁文件的审计要求。

## 决定

保留现有 Tailwind 3.4.17 实际生成的 utility CSS 和 MIT 许可证，移除不再需要的构建插件。source-map-js 在模板与 Demo 中更新到修复补丁。Demo 的 Vue 从 3.4.38 更新到同一大版本的 3.5.43，使编译器、运行时与服务端渲染包版本一致。

## 备选与兼容

继续保留告警依赖会使新的交付材料重复携带已知问题。为此迁移 Tailwind 4 会扩大 CSS 与浏览器兼容范围，故本次采用静态 CSS。Vue 更新不改变 SDK 公共接口，不提高 Chrome 49 构建目标；页面仍通过 legacy 转译及现有 polyfill。静态 utility CSS 后续新增样式须显式维护。

## 验证

Vue 升级首次构建由既有指纹保护拒绝。检查新 runtime-dom 后确认 CSSStyleDeclaration 缓存仍是同一组声明、写入与读取，仅声明新增 PURE 注释；更新精确散列与替换位置，保留不匹配即失败的保护。其他 DOM Symbol 属性不在此兼容补丁范围内。

锁文件审计、Web 构建、x86/x64 Debug/Release 原生启动，以及 Workbench 默认 Vue 和 React 页面 Bridge 就绪/关闭均须实际通过。仅审计通过不能替代浏览器兼容验证。修复前报告保留在发布验证目录。

来源：[source-map-js 公告](https://github.com/advisories/GHSA-68fv-2mgg-jv7q)、[Vue 公告](https://github.com/advisories/GHSA-g2v6-rqmx-r4w6)。
