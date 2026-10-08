# ADR-0026：可选 React / Vue GUI 语义绑定

状态：实施首批切片；不代表 UIA、独立 Agent 主体或生产平台验收。

## 背景

公共 `SouiGui` 已提供显式注册、快照、动作、等待和移除语义。业务组件仍须手工协调 DOM 连接、重渲染、旧回调与卸载。框架绑定应复用这些契约，不增加原生权限模型，也不能将框架强制加入 C++ Core 或所有 Web 应用。

## 决定

- 新增独立可选 `@soui/gui-react` 和 `@soui/gui-vue` 模块；运行依赖仅通过 peer 声明框架及 `@soui/native-sdk`。共用私有的生命周期控制器随各模块编译，不增加公共核心依赖。React 使用 18.3 / 19 的公共 Hook；Vue 使用 3.5 的 Composition API。输出 ES2015 ESM，兼容应用仍须经过应用 Chrome 49 构建；这不是框架或目标机兼容认证。
- `useSouiGuiNode(gui, options)` 返回真实 DOM 元素的 ref。React 的 options 是当前 render 提供的对象，只有 layout commit 才替换公开回调；不在 render 注册或更新已注册记录。Vue 的 options 是 getter，仅在 DOM 挂载/更新后的阶段应用，另观察 getter 的响应式依赖。组件必须在连接的 DOM 容器中挂载，且将 ref 绑定到 DOM 元素。
- 应用创建并拥有每文档 GUI 实例。绑定借用实例，卸载只移除自己注册，不调用 `gui.dispose()`。React StrictMode 的 effect/ref 重复设置清理须幂等；Vue KeepAlive 停用时移除，重新激活后用新一代注册。SSR 不注册，不承诺服务端 GUI 操作。
- ID 必须来自业务实体，例如 `invoice:42:approve`，不得使用列表索引、随机值或框架生成 ID 作为业务身份。gui、元素、ID、role、name 或动作名称集合变化属于结构变化，先移除旧记录再注册新一代；普通回调和 state provider 更新保留记录与等待。共享控制器对每代保留独立已提交回调，旧异步结果由已有 `SouiGui` 移除检查拒绝。
- 不读取 DOM 文本、value、组件 props 或事件来生成公开数据。默认 `state` 为空；应用仅提供显式公开投影。`expose: false` 不注册且移除既有注册。敏感控件默认不绑定或显式关闭暴露。绑定不能判断任意业务 JSON 是否敏感，业务动作结果和错误也须自行投影；原生能力白名单、输入/输出校验仍是最终权限边界。
- 动作使用原有有界 JSON 输入/结果和禁用、忙碌检查。处理器可以调用单一 actiongen 生成的 `callAction`，无需更改生成类型或另建 MethodMap。GUI 处理器经已有 Promise 阶段执行，不能自动保留或制造原生 user gesture；需要手势的真实点击必须在应用事件处理器中直接进入原生客户端。
- 移除/停用/销毁立即拒绝该代等待与执行观察；超时只结束观察，实际处理器未完成时仍忙碌。上述行为不是原生 `task.cancel`，不表示任务终止、回滚或阻止应用自身的异步副作用。任务取消仍由应用显式调用公共服务并查询最终状态。
- 框架绑定注册失败沿用原有 GUI 错误并进入框架错误边界，不吞掉重复 ID、断开 DOM或容量错误；无效动作表/函数/expose 为 TypeError，更新失败也移除旧注册，避免框架保留组件时仍可调用旧动作。原生拒绝原样向调用方传播，不重试、不申请授权。

## 备选与迁移

不自动扫描 DOM，不提供选择器脚本执行、另一套授权、隐式 Context 全局 GUI 或新异步任务框架。应用可继续手工 `gui.register()`。此切片不修改模板、Demo、Core、版本身份、标准验证入口；整合者另行加入包身份和标准入口。

SDK 源码中两个模块分别显式安装开发依赖并编译；应用按需从本地模块或 tarball 安装一种绑定，并提供自身框架与单份 Web SDK。绑定编译后的 shared 文件包含在各包内，消费者不引用仓库外相邻源码。避免应用 SDK 源码 alias 与 bare package 产生两份模块实例。

SDK peer 使用 `>=0.4.0-0 <0.5.0 || >=0.5.0-0 <0.6.0` 两个显式分支，允许 0.4/0.5 稳定线和 0.4.0/0.5.0 tuple 的预发行，其他 tuple 的预发行不自动接受。临时只改 SDK package 版本的夹具验证 npm 解析，不等同于该版本源码认证；修前 ERESOLVE 证据与回归在 `tests/gui-bindings/peer-range-regression.md`。

## 验证

锁定测试依赖，使用真实 React `createRoot`（18.3 与 19.3）和 Vue `createApp`、连接 DOM 容器与真实 `SouiGui`，不假造 register/unregister。验证 StrictMode、KeepAlive、ref 替换、ID 重排、状态观察、已提交回调、暴露关闭、卸载及旧完成拒绝；验证 DOM 值不泄漏、原生拒绝、超时不取消实际工作和显式 task.cancel 边界。各模块编译和独立安装包消费者类型检查，检查 peer-only 包内容与适用 npm audit。不启动桌面 GUI；实际 Chrome 49/Win7/PE、UIA、原生手势和长期矩阵由独立 P0 验收。

接口依据：[React useLayoutEffect](https://react.dev/reference/react/useLayoutEffect)、[StrictMode](https://react.dev/reference/react/StrictMode)、[Vue 生命周期](https://vuejs.org/api/composition-api-lifecycle.html)、[KeepAlive](https://vuejs.org/guide/built-ins/keep-alive.html)。
