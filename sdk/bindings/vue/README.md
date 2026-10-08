# 可选 Vue GUI 绑定

此模块借用应用的 `SouiGui`，在 Composition API setup 中调用 `useSouiGuiNode(gui, optionsGetter)` 并返回 DOM ref。Vue 3.5 的挂载、更新和 KeepAlive 激活/停用使用同一 GUI 契约；依赖为 peer，基础 SDK 不要求 Vue。输出 ESM，需要应用构建；Chrome 49 仍需兼容转换。

完整安装、稳定 ID、状态投影、KeepAlive、敏感字段与等待/取消边界见交付源码的 `docs/gui-bindings.md`。不要导入 `dist` 内部路径。SDK 源码中先构建 `sdk/web`，再在本模块运行 `npm ci` / `npm run build`；编译产物包含私有 shared 实现，可独立 `npm pack` 安装，不引用相邻源码。
