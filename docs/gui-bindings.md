# 可选 React / Vue GUI 绑定

绑定帮助业务组件注册现有 `SouiGui` 语义，不提供独立 Agent 身份、UIA、脚本选择器或原生权限。应用按需使用 `@soui/gui-react` 或 `@soui/gui-vue`；没有绑定的 C++/Web 应用不需要框架。

## 显式安装与构建

SDK 源码根目录执行（仅选择需要的绑定）：

```powershell
npm ci --prefix sdk/web
npm run build --prefix sdk/web
npm ci --prefix sdk/bindings/react --install-links
npm run build --prefix sdk/bindings/react
# Vue 应用改为 sdk/bindings/vue。
```

应用自己的 Web 工程保留自身 React 18.3 / 19 或 Vue 3.5 依赖，并安装同一份本地 SDK 和一种已编译绑定。相对路径由应用位置决定，例如生成项目的 `web/`：

```powershell
npm install ../sdk/web ../sdk/bindings/react --install-links
```

这是按需使用；完整 SDK 源码交付可能包含两套模块目录，并不表示安装器已按框架裁剪源码。`--install-links` 将本地模块打包复制，避免绑定通过源码目录解析到另一份开发用框架。也可在已编译模块中运行 `npm pack`，将 tarball 交给独立工程安装；运行文件和类型声明包含私有 shared 实现，不引用相邻源码。模块没有 runtime `dependencies`，框架与 SDK 通过 peer 提供；构建开发依赖单独锁定。两个模块的版本与根 `VERSION` 和本地 Web SDK 完全一致，编译不借用其他 SDK 版本。

标准入口显式使用 `tools/Build.ps1 -WithGuiBindings` 和 `tools/Test.ps1 -UnitOnly -WithGuiBindings`。默认构建不安装 React/Vue。仅验证绑定而不编译 C++ 时，使用 `node tools/sdk/gui-bindings-delivery.cjs build --root .`，随后 `node tools/sdk/gui-bindings-delivery.cjs test --root .`。`-SkipWeb -WithGuiBindings` 必须有匹配当前源码与输出的编译记录，或来自已安装 SDK 的可选交付声明。

需要交付已编译绑定时，在完整编译之后运行 `node tools/sdk/soui-sdk.cjs pack --source . --output dist/sdk/<VERSION> --version <VERSION> --with-gui-bindings`，以实际版本替换占位符。可选声明列出 Web SDK、React 和 Vue 的 ESM 与类型入口，并将三套 `dist` 纳入 SDK 清单；安装与目录搬移后可以直接从这些目录 `npm pack`，不依赖原仓库或源码 alias。默认 source-only 包不包含这些编译输出。同版本的不同清单不能互换，固定发布必须选择一份最终内容；已有 SDK 锁仍拒绝同版本不同内容。

应用必须把所有 `@soui/native-sdk` 导入指向同一实例。不要让现有 SDK 源码 alias 和 bare package 指向两份 SDK；可统一使用 package 或统一应用的 alias/dedupe 配置。绑定只使用 SDK 类型并借用传入对象，不创建客户端或 GUI。不要导入绑定包内部 `dist` 路径。

SDK peer 明确为 `>=0.4.0-0 <0.5.0 || >=0.5.0-0 <0.5.1 || >=0.5.1-0 <0.6.0 || 0.6.0-preview.3`：支持 0.4/0.5 稳定线以及 0.4.0、0.5.0、0.5.1 的预发行，并精确加入当前 0.6.0-preview.3；不自动承诺其他 minor/patch 组合的预发行（例如 0.4.1-preview.1、0.5.2-preview.1）或其他 0.6 版本。包测试分别验证本次真实同版本源码的 tarball 消费，以及临时元数据夹具的 npm peer 解析；后者不认证相应 SDK 源码或平台。历史 0.5.0 和 0.5.1 包保持各自原 peer 声明，当前范围用于本次尚未发布的 0.6.0-preview.3 交付。

输出是 ES2015 ESM，由应用构建为目标页面载荷。兼容应用仍须执行现有 Chrome 49 转换和所需 polyfill；React 19 属于现代档。本次 DOM 组件测试不证明 Win7/PE 或旧引擎认证。

## 共享契约与实例所有权

两个模块都导出 `useSouiGuiNode` 和 `SouiGuiBindingOptions`。React 接收当前 options 对象；Vue 接收 options getter。返回的 ref 只能绑定一个实际 DOM 元素，且应用根容器应连接文档。绑定不是 Vue 组件 ref、React 自动事件处理器或 SSR GUI：服务端不注册，客户端须在正常组件生命周期使用。

应用在每个页面文档创建一次 `createSouiGui()`，并在真正结束该文档时显式 `gui.dispose()`。组件只借用该实例，卸载移除自己的注册；不会销毁其他组件或原生客户端。Vue 传入的 gui 在该组件生命周期固定，切换文档/GUI 时重建该组件；React 可通过新 gui 值切换并移除旧注册。

options 包括业务 `id`、显式 `role` / `name`、命名 `actions`、可选 `state` provider 和可选 `expose`。动作输入/返回、名称范围、容量、禁用与忙碌校验沿用 `SouiGui`。不合法动作表、函数或 expose 选项抛出 `TypeError` 并移除旧注册；原有注册错误保留 `SouiBridgeError` 并交给框架错误边界。重复 ID 不覆盖已有注册。

`expose` 默认开启，`expose: false` 不注册并移除已注册观察。ID 应来自业务实体，例如 `order:42:approve`；列表使用相同实体 ID 作为框架 key，不使用索引、随机数或 `useId()` 代替业务身份。同一个 GUI 中 ID 唯一。

## React 示例

```tsx
import { useSouiGuiNode } from '@soui/gui-react';
import type { SouiGui, SouiJson } from '@soui/native-sdk';

export function Approve({ gui, orderId, phase, approve }: {
  gui: SouiGui; orderId: string; phase: string; approve: () => Promise<SouiJson>;
}) {
  const ref = useSouiGuiNode<HTMLButtonElement>(gui, {
    id: `order:${orderId}:approve`, role: 'button', name: 'Approve order',
    actions: { activate: () => approve() },
    state: () => ({ state: { phase } })
  });
  return <button ref={ref} onClick={() => void approve()}>Approve</button>;
}
```

只有 layout commit 才更新公开回调，未提交或被 Suspense 放弃的 render 不改变原注册。StrictMode 重复 effect/ref 清理可以安全重建；组件卸载不会调用共享 gui 的 dispose。

## Vue 示例

```vue
<script setup lang="ts">
import { useSouiGuiNode } from '@soui/gui-vue';
import type { SouiGui, SouiJson } from '@soui/native-sdk';
const props = defineProps<{
  gui: SouiGui; orderId: string; phase: string; approve: () => Promise<SouiJson>;
}>();
const bind = useSouiGuiNode<HTMLButtonElement>(props.gui, () => ({
  id: `order:${props.orderId}:approve`, role: 'button', name: 'Approve order',
  actions: { activate: () => props.approve() },
  state: () => ({ state: { phase: props.phase } })
}));
</script>
<template><button :ref="bind" @click="props.approve()">Approve</button></template>
```

在 setup 中调用，getter 的依赖变化在 DOM 更新后的阶段应用。KeepAlive 停用会移除注册，激活重新注册；不会让缓存但已脱离文档的节点继续接受动作。`v-if`、元素种类变化和 ref 替换也是生命周期变化。

## 状态、异步工作与隐私

默认 state 为 `{}`。绑定不读取 DOM value、文本、组件 props 或事件来生成快照；上述示例只投影 `phase`。密码、令牌、路径等控件默认不调用绑定或设置 `expose: false`；不要直接返回整份业务模型。显式 name、state、动作结果和错误文本仍可能泄露应用主动提供的数据，绑定不能自动判定或清除这些内容。

同一元素和结构的回调/state 更新保留观察等待；gui、元素、ID、role、name 或动作名称集合变化会移除旧一代注册。旧观察以 `E_GUI_REMOVED` 失败，旧异步完成不能把结果交给复用该 ID 的新注册。state provider 应短小、无副作用，读取显式公共业务状态；绑定不冻结应用闭包访问的外部数据。

```ts
await gui.perform({ id: 'order:42:approve', action: 'activate', timeoutMs: 1000 });
await gui.waitFor({ id: 'order:42:approve', state: { phase: 'done' }, timeoutMs: 1000 });
```

动作等待和状态观察都有现有容量/超时边界。`E_TIMEOUT` 结束观察，处理器仍未结束时节点继续 busy；卸载、KeepAlive 停用、expose 关闭或文档 dispose 也只拒绝观察。这些操作不会调用原生 `task.cancel`、回滚或阻止应用处理器自己的异步副作用。需要取消的业务必须显式调用公共任务取消服务，随后查询最终任务状态；收到取消请求响应并不自动证明所有副作用已撤回。

处理器可以调用 actiongen 的 `callAction(native, 'app.greet', { name })` 等已生成包装器，其 MethodMap、输入/输出校验和原生最终授权继续有效；绑定只登记函数，不重试、不授予能力。`SouiGui.perform` 在 Promise 阶段调用处理器，因此不能用于自动保留/制造原生 user gesture。需要真实手势的动作在实际点击处理器中直接进入原生客户端，并由原生检查；Agent 与网页自报标志不能提升权限。

## 验证

两个绑定构建后，从交付根执行：

```powershell
npm ci --prefix tests/gui-bindings --install-links
npm ci --prefix tests/gui-bindings/react18 --install-links
npm run typecheck --prefix tests/gui-bindings
npm run typecheck:react18 --prefix tests/gui-bindings
npm run test --prefix tests/gui-bindings
npm run test:react18 --prefix tests/gui-bindings
npm audit --prefix sdk/bindings/react
npm audit --prefix sdk/bindings/vue
npm audit --prefix tests/gui-bindings
npm audit --prefix tests/gui-bindings/react18
```

测试使用固定依赖与 jsdom 连接 DOM、真实 createRoot/createApp/SouiGui；框架和 GUI 注册器没有 mock。原生传输模拟只验证拒绝/取消传播，原生权限实现另有 SDK 契约测试。包测试在临时仓库外工程安装真实 tarball，用本次缓存离线恢复固定框架依赖并检查类型、实际 ESM 导入和 SDK 安装搬移；首次运行前应完成以上 `npm ci`，源码交付回归还要求匹配的可选构建记录。`Collect-Verification.cjs --with-gui-bindings` 可显式附加相同测试与审计，普通采集不处理框架依赖。测试不用真实桌面窗口，不能替代 Chrome 49/Win7/PE、UIA、手势或长期目标机矩阵。

生命周期依据：[React useLayoutEffect](https://react.dev/reference/react/useLayoutEffect)、[StrictMode](https://react.dev/reference/react/StrictMode)、[Vue 生命周期](https://vuejs.org/api/composition-api-lifecycle.html)、[KeepAlive](https://vuejs.org/guide/built-ins/keep-alive.html)。
