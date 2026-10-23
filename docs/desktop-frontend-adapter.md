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

## 常用系统服务

`windowProperties.setTitle(title)/resize(width,height)/show(visible)/focus()` 与
`clipboard.writeText(text)` 返回 `{accepted:true}`；`clipboard.readText()` 返回文本，
`dialogs.chooseDirectory()` 返回 `{cancelled,path}`。它们仍通过原始invoke调用，
宿主须分别开启 options.services 的窗口属性、剪贴板读取、写入和目录对话框权限。
默认拒绝；这些开关是可信应用预授权，不是用户手势检测，也不会代用户批准OS提示。

示例业务调用已迁移到标准服务；没有删除旧Host接口或改写已有生成项目。目录取消
是正常结果，不应当作异常；返回路径不授予文件读取能力。网页超时不代表模态系统
对话框或原生剪贴板操作已取消，关闭/导航的迟到响应仍受既有隔离规则约束。

返回类型为 `DesktopServiceAcceptance` 与取消/已选择两种分支的 `DirectorySelection`。
适配器对返回文本按严格UTF-8计数（最多64KiB），拒绝NUL及孤立代理字符；本地校验
失败分别使用 `desktop_adapter_invalid_service_acceptance`、
`desktop_adapter_invalid_clipboard_text`、`desktop_adapter_invalid_directory_selection`。
原生拒绝、权限错误和系统错误保留，不由这些本地格式错误替代。

## 打开与保存文件路径

`dialogs.openFiles(options = {})` 调用 `desktop.dialog.openFiles`；
`dialogs.saveFile(options = {})` 调用 `desktop.dialog.saveFile`。宿主须独立开启
`options.services.file_open_dialog` / `file_save_dialog`，两者默认 false，不继承
目录选择权限。这些接口只选择路径，不读取、创建、截断或写入文件，不授予持续
访问、资源根目录访问或安全书签权限，也不隐式触发权限申请。

```js
import { dialogs } from './soui/desktop.mjs';
const opened = await dialogs.openFiles({ extensions: ['txt', 'md'], multiple: true });
if (!opened.cancelled) console.log(opened.paths);
const saved = await dialogs.saveFile({ title: '保存位置', defaultName: 'notes.txt' });
if (!saved.cancelled) console.log(saved.path); // 仅最终确认路径，文件尚未由 SDK 写入
```

类型 `OpenFileOptions` / `SaveFileOptions` 只接受以下字段，不做隐式类型转换。
选项须为普通对象（也允许无 prototype 的记录）；未知字段、字段值显式 undefined、
null 或错误类型均拒绝。省略参数或传入 undefined 使用空选项：

- `title?`：最多 4096 UTF-8 字节，允许空字符串，不允许 NUL 或孤立代理字符。
- `extensions?`：原始数组最多 16 项，每项为 1..16 位 ASCII 字母或数字；转为
  小写并按原顺序去重。省略或空数组不限文件类型；不接受点、通配符、MIME 或路径。
- 打开的 `multiple?`：仅 boolean，默认 false；true 时最多 32 个路径。
- 保存的 `defaultName?`：允许空字符串；非空时最多 255 UTF-8 字节的便携 basename。
  拒绝 C0/C1/DEL 控制字符、`<>:"/\\|?*`、末尾点或空格及 `.` / `..`。
  首个点前的基名去掉末尾空格后，按 ASCII 大小写不敏感拒绝 CON、PRN、AUX、NUL、
  CLOCK$、CONIN$、CONOUT$、COM1..9、LPT1..9 及 COM/LPT 后接 ¹²³ 的保留名。

扩展名仅为选择 UI 提示，不保证内容类型或安全；原生面板可能调整默认后缀，
以返回的最终路径为准。默认名的便携规则不限制用户最终选择的本平台合法名称。

`OpenFileSelection` 是 `{cancelled:true, paths:[]}` 或
`{cancelled:false, paths:string[]}`；单选成功必须恰好一项。
`SaveFileSelection` 是 `{cancelled:true, path:''}` 或
`{cancelled:false, path:string}`。成功路径非空，每项最多 32768 UTF-8 字节，
打开的全部路径最多 128 KiB；拒绝 NUL、孤立代理字符、错误字段/类型与额外字段。
原生公共层按实际平台的 filesystem 规则验证绝对路径，浏览器适配器不猜测 OS
或重写跨系统路径解析器，只验证响应形状、文本编码与额度。

非法选项同步抛出 `desktop_adapter_invalid_file_dialog_options`；非法响应分别拒绝为
`desktop_adapter_invalid_open_file_selection` / `desktop_adapter_invalid_save_file_selection`。
只有原生公共层将 `E_CANCELLED` 转为取消分支；适配器不捕获或吞掉权限、关闭、导航、
系统错误，也不把异常转换成取消。超时不保证系统面板已关闭，不自动重试。
