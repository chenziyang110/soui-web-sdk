# 可选 React GUI 绑定

此模块借用应用的 `SouiGui`，返回 DOM ref，不拥有 GUI、原生客户端或任务。React 18.3 与 19 共用 `useSouiGuiNode`；依赖为 peer，基础 SDK 不要求 React。输出 ESM，需要应用构建；Chrome 49 仍需兼容转换。

完整安装、稳定 ID、状态投影、StrictMode、敏感字段与等待/取消边界见交付源码的 `docs/gui-bindings.md`。不要导入 `dist` 内部路径。SDK 源码中先构建 `sdk/web`，再在本模块运行 `npm ci` / `npm run build`；编译产物包含私有 shared 实现，可独立 `npm pack` 安装，不引用相邻源码。
