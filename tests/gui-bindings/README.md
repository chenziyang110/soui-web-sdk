# GUI 绑定组件与交付测试

从交付根运行 `docs/gui-bindings.md` 的构建与验证命令。先构建 SDK Web 与两个绑定，再 `npm ci --prefix tests/gui-bindings` 和 `npm ci --prefix tests/gui-bindings/react18`；每次重建绑定后重新执行这两个 ci，刷新复制安装的消费者，不能只测源码目录中的模块。

测试层次：

- React 18.3 / 19.3 真实 createRoot、StrictMode、Suspense；Vue 3.5 真实 createApp、KeepAlive；jsdom 提供连接 DOM。GUI 注册器和生命周期没有 mock。
- 使用实际安装包入口/类型，检查回调、稳定业务 ID、状态等待、ref 替换、卸载、暴露关闭、旧完成、错误与取消边界。模拟传输只验证公共客户端拒绝/取消传播，不证明原生权限实现或任务已经终止。
- 两模块均 peer-only，私有 shared 文件随模块编译。临时仓库外消费者安装真实 npm tarball，使用已固定且已缓存框架/TypeScript，离线编译正反向类型用例，不引用源码 alias。

测试没有启动桌面 GUI、VM 或本机 WebView2。React/Vue 的 DOM 模拟结果不能代替 Win7、PE、Chrome 49、UIA、原生手势及长期验收。actiongen 与本切片由独立源码基线实现，最终整合还须运行生成动作与绑定的组合用例，不能把独立的类型测试当成组合已经通过。
