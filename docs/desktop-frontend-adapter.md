# 统一桌面前端适配层（未发布）

`Soui::Desktop` 专用 ESM 位于 `sdk/desktop/frontend/desktop.mjs`，声明为相邻
`desktop.d.mts`。安装后在 `share/SouiDesktop/frontend`；生成项目自动复制原文件到
`web/soui`，无 npm、下载、转译或 TypeScript 构建依赖。它不是旧 `sdk/web` 客户端，
不适用于 Miniblink / Chrome 49 路径。

```js
import { invoke, on, readAsset, assetJson } from './soui/desktop.mjs';
const info = await invoke('app.info', null);
const unsubscribe = on('app.changed', value => console.log(value));
unsubscribe();
// 仅当宿主显式 enable_asset_read，且资产位于可信根内：
const config = assetJson(await readAsset('config.json'));
```

## 契约

- `requireDesktop()` 立即检查当前 window 的 version 1 注入接口，无宿主或不兼容
  接口抛出 `DesktopAdapterError`。不等待 DOM、不轮询，不代表原生业务方法可用。
- `invoke`、`on`、`readAsset` 每次重新查找当前接口，直接委托；不缓存旧宿主、
  不重试、不生成 token、不通过浏览器后门发送消息。原生 Promise/错误保留原样。
- `on` 返回原生取消订阅函数。事件/方法名称、allowlist、来源、会话和额度仍由
  preload/原生端控制。该模块不是同源恶意脚本隔离边界。
- 原生请求目前 30 秒超时、至多 256 个 pending；pagehide 拒绝旧请求并清空监听。
  此类 JS 错误可能只有 message，没有 code；原生失败含 code。适配层没有
  AbortSignal 或硬 I/O 取消，不改变导航撤销的既有语义。
- `assetBytes` 校验 `{mime,size,base64}`、规范 base64 和解码字节数，最多 256 KiB；
  返回 Uint8Array。`assetText` 使用严格 UTF-8；`assetJson` 再执行 JSON.parse。
  MIME 是宿主元数据，不保证文本内容符合 MIME，也不进行 HTML 执行或 eval。
- `invoke<TResult,TParams>`、`on<TPayload>`、`assetJson<T>` 的泛型仅表达调用者
  声明，不进行业务 schema 验证。默认 unknown，业务应自行检查非可信结果。

TypeScript 使用支持 `.d.mts` 的模块解析方式（例如 NodeNext/Bundler）可读取类型；
现有 JS 项目照常运行，也可选择 JSDoc。无需全局安装类型包；Window.souiDesktop
声明仅在此模块类型被引用时加入，仍是 optional。既有项目不会自动改写。

## 验证边界

本轮只做适配运行时边界、生成器集成及语法必要检查。原生窗口、三引擎运行和
统一完整回归另行验收；类型提示不等于跨平台认证。
# 输入快照边界

事件解绑函数只负责自己的注册组：同名事件建立新组后，再次调用旧解绑函数
不会移除新组。原有 Set 对相同 callback 的去重语义不变。

assetBytes 对 mime、size、base64 只读取一次，再对同一组值验证和分配。
调用方传入带 accessor 的对象也不能借重复读取改变校验后的大小；256 KiB
限制不变。这不限制调用方 getter 自身的副作用，原生返回仍为普通 JSON 数据。

## 统一窗口控制

`windowControls.minimize/maximize/restore()` 返回 `{accepted:true}`，只表示提交原生
请求。`getState()` 返回观测的 `{minimized,maximized}`；`onStateChanged(listener)`
订阅同型状态并返回取消函数。两标志允许同时为 true，不能用命令返回值猜测状态。
宿主必须显式启用 `enable_window_controls`，默认调用被拒绝；适配器不授予权限，
不缓存先前页面的窗口对象、不绕过原来的 invoke/on 会话与超时。

类型 `WindowState` / `WindowControlAcceptance` 描述这两个不同结果；格式错误的
状态快照抛出 `desktop_adapter_invalid_window_state`，不会把缺字段猜为 false。
