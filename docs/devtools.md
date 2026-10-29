# 原生受控 DevTools（候选）

本候选已接入公共Host、两个内置后端、两种模板和Workbench，编译通过与真实运行验收分别记录。默认关闭，Debug和Release使用同一许可逻辑。当前两个内置后端采用应用明确接受的`destroy_target`结束策略：结束诊断关闭当前业务Host，调用方先处理未保存状态。无损`detach`不是本候选已声明的内置能力。

`0.6.0-preview.4` 修复受控 MiniBlink 业务窗口的 CPU 呈现接线：业务 endpoint 使用引擎 HWND 绘制，Inspector 继续使用现有像素回调与 WM_PAINT；见 [ADR 0128](adr/0128-controlled-cpu-presentation.md)。此处描述源码行为，当前构建与真实窗口结果另行记录，旧候选证据仍绑定旧载荷。

`0.6.0-preview.5` 为兼容 inline source map 调整有限容量，保留原单项和目标总账目边界；见 [ADR 0130](adr/0130-bounded-legacy-source-maps.md)。真实三份 map 的纯数据检查不等于浏览器中的 TypeScript 断点通过，新增并发 map 仍可能正确触发总预算拒绝。

`0.6.0-preview.7` 的受控 MiniBlink inspector 按原事件快照转发键盘消息，不从引擎线程读取按键当时的修饰键状态。受控接口 ABI 为 3，资源快照 ABI 仍为 2；必须使用匹配的新内核，ABI 相符仍不能替代完整资源配对校验。队列饱和保持显式失败，暂停输入、中文 IME 和焦点仍需真实窗口验收；见 [ADR 0134](adr/0134-controlled-inspector-keyboard-snapshot.md)。

`0.6.0-preview.8` 已接线显式的 MiniBlink 本地 HTTPS 资源模式，见 [ADR 0135](adr/0135-owned-local-resource-origin.md)。这为旧 `file:` 页面被内核拒绝的 `fetch` 提供新的来源模式；不调整 CORS、调试授权或 inspector 资源预算。新代码完成静态审查后统一编译、实测，旧候选结果不作为新模式的运行证据。

## 当前候选的实际验证范围（2026-10-28）

`0.6.0-preview.9` 是本地未发布候选。source-kernel loader 已补齐可选 `wkeLoadURL` 绑定；无该导出的旧 file-only vendor 继续支持，显式 HTTPS 模式仍按原能力契约拒绝。本轮另修复 Windows 字体最后候选、Workbench 原生夹具交付和受控 HTTPS ready 探针，保留原授权、资源 fence、关闭期限及历史失败。

| 后端 / 架构 / 配置 | 实际通过范围与原产物身份 |
|---|---|
| MiniBlink / x64 / Release | `.9` source-diagnostic 应用已有 DOM/CSS、Console、Network 200/404、eager TS/lazy TSX 断点与单步、同步/Promise 异常及真暂停原生结束的单例证据；小双 Host case 另观察 A 暂停、B pending，手工恢复后按原期限结束。后续字体修复产物另完成模板 DOM 42→43、业务显示43、原生结束；最终 Workbench 完成原生两组扫描、Inspector、Console42和原生结束。 |
| MiniBlink / x86 / Release | 字体修复后的模板完成默认 No/回车拒绝后显式 Yes、精确 DOM 节点选择、HTML/业务42→43及原生结束；最终 Workbench 完成 FileInsight 页授权、Inspector、Console42及原生结束，原生扫描复用字节相同的已过子项。Network、Sources等其余面板未据这些短测判通过。 |
| WebView2 / x64 / Release | 原 `modern-first` source-diagnostic 产物在 Fixed Runtime155.0.4283.62 完成默认拒绝/原生授权、DOM/CSS、Console、eager TS/lazy TSX、单步及异常暂停、真暂停原生结束。Network成功请求为200/78B；缺失资源实际为 `ERR_FILE_NOT_FOUND`、无HTTP状态，原 `>=400` 条件未通过。 |
| WebView2 / x86 / Release | 原 `modern-first` 产物在同版 Fixed Runtime完成默认拒绝/原生授权、Console42、eager TS断点/单步/继续、debugger暂停及暂停中原生结束；DOM编辑、Network、lazy TSX与异常暂停未测。 |

上述结果各自绑定原源码、EXE/DLL、配对资源和进程身份；不把 `modern-first` 的结果改挂到后续应用，也不把旧 MiniBlink 面板结果重新归属到字体修复产物。WebView2 原生 `attached`/`paused`/`visible` 仍按公开能力报告 unknown，Inspector可见截图不改变此状态。最终四个模板/Workbench原进程均有保留句柄的自然退出0证据；不据此推断全部heap、回调或长期资源收敛。

仓库内证据索引为 `plans/devtools-core-short-matrix.md`、`plans/2026-10-27-devtools-core-candidate.md`；本轮四例记录为 `out/devtools-sdk-delivery/unified-20261026-keyboard-abi3/combined-short-font-fixture-1/Summary.json`。它们是仓库验收记录，不是源码SDK包内文件。完整D01–D12、Debug/其他环境、正式安装搬移及性能仍按矩阵保留未完成范围；D13千次和D14八小时后置，未执行，`releaseReady=false`。

本文档后续修订不改变已生成811文件源码包、旧配对或构建清单的字节身份；新文档交付须另行生成清单，不回写旧包散列。

## MiniBlink 本地资源原点

原有零参数和单参数 adapter 构造继续使用 file URL。需要同原点 XHR/fetch 的原生应用显式选择下列新构造；`host_config.resource_root` 仍指定应用资源目录：

```cpp
auto adapter = std::make_unique<soui::sdk::dynamic_miniblink_adapter>(
    soui::sdk::diagnostics::mode::disabled,
    soui::sdk::miniblink_local_resource_options{});
soui::sdk::soui_miniblink_host host(std::move(adapter));
```

兼容模板与 Workbench 的 `use_local_resource_origin` 默认 false，由应用修改自己的原生初始化代码显式选择；它独立于 `allow_native_devtools`，网页和环境变量不能开启它。WebView2 继续使用自身已有的资源映射，AutoHost 默认行为不变。缺少独立 business-resource ABI 1 的内核明确拒绝新模式，不能静默退回 file。

资源准备生成不可变闭集：最多 4096 文件、16 MiB/文件、128 MiB 总量，保留路径、重解析点、EOF 和稳定身份检查。初始准备和原生注册共享原有限启动预算，超时后的 worker 与资源仍须真实退休。响应只读已准备内存，不在网络回调打开文件。每个 Host 的主机名和每个文档路径独立；导航和 Bridge 只信任当前精确入口及真实文档 receipt，旧文档、跨 Host、非法路径均不能进入网络 fallback。

新模式仅接受绝对本地 fixed/RAM 磁盘 root；资源相对名字限 ASCII 字母、数字、`_-.` 与 `/`，大小写精确，单段 128 字节、路径 1024 字节、32 层。父目录可使用 Unicode；UNC、设备路径、链接或名字别名明确拒绝。其他资源存放方式继续使用原构造的已有行为。

保留域只提供静态 GET，不提供 WebSocket、SSE、Beacon/Ping、外部下载、插件通道或原生音视频 provider；相关入口明确拒绝，不向外部媒体 hook 转交该域 URL。Blob/data 的 opaque 正文不被误当成资源 authority，仍按原策略处理。

暂停时业务资源回调保持有界待办，恢复后继续；关闭先停止准入，待活动回调与正文借用退出并取得 native 排空 receipt，才销毁 view。资源额度按实际终态回收。该模式的四请求、源码断点及暂停关闭已有上表 MiniBlink x64 Release 单例证据，其他架构和场景不据此外推；长期循环后置，不能以窗口消失代替排空证据。

准备阶段受进程内 32 个准备/存活 snapshot、512 MiB 正文总量限制，正文分配前即计费。单请求满额明确拒绝并累计 `denied`，其他已接纳请求继续。不可恢复的资源 provider 或消息投递故障则撤销业务准入，并由既有 UI `pump()` 推进原 Host 关闭；应用须继续消息循环和原关闭流程，故障记录与最终物理回收分别保留。

## 原生接入

包含`<soui/host/webview_host.h>`。在构造Host的UI线程、首次`start`前配置；WebView2必须在`prepare_async`前配置，受控模式会选择独占profile并由SDK持有runtime exit watch。该模式不能再调用旧的外部`acquire_runtime_exit_watch`来取得第二个owner；未启用DevTools的旧应用继续使用原退出watch接法。

```cpp
soui::sdk::devtools::configuration diagnostic;
diagnostic.access = soui::sdk::devtools::policy::application_controlled;
diagnostic.ending = soui::sdk::devtools::termination::destroy_target;
diagnostic.f12 = true;
diagnostic.ctrl_shift_i = true;
// MiniBlink另指定已部署的匹配资源包及原生交付身份：
// diagnostic.frontend_root = application_root / soui::sdk::devtools::package::frontend_subdir;
// diagnostic.frontend_manifest_sha256 = approved_manifest_pin;
// diagnostic.kernel_sha256 = approved_kernel_pin;
const auto configured = host.configure_devtools(diagnostic);
// 检查configured，再按后端原准备/start流程启动。
```

配置不发放会话许可。在可信原生菜单处理器中完成应用自己的身份/企业策略检查，告知调试可查看数据、执行页面代码及结束时的关闭策略，然后调用：

```cpp
soui::sdk::devtools::grant authority;
const auto authorized = host.authorize_devtools(authority, std::chrono::minutes{5});
if (authorized.ok) {
  const auto request = host.open_devtools(authority);
  // request.accepted()只表示已接纳；保存request.id查询同一操作。
}
const auto observed = host.observe_devtools();
// host.devtools_operation(id)查询最后32个操作，未知/淘汰返回E_OPERATION_NOT_FOUND。
```

grant不能从字符串、JSON、地址或网页字段构造。它绑定Host独立控制对象、进程实例、实际文档和许可epoch。新授权替换旧grant；文档变化、撤销或到期立即拒绝新的打开。零有效期绑定当前文档寿命；显式时长范围为0至24小时。非零时长使用原生UI定时器和steady_clock，应用必须持续分派UI消息；暂停页面JS不会冻结此时钟。未启用或没有到期许可时不创建该定时器。

同一目标重复打开返回同一操作，不无限创建窗口。异步预备中的操作由`host.pump()`继续；其ID不变。源码错误、资源失败、后端HRESULT及撤销失败保持实际结果。

## 结束、导航与状态

`host.revoke_devtools(reason)`先废止grant并关闭原生业务准入，再非阻塞推进结束。`destroy_target`继续原Host关闭流程；保留Host、adapter、Bridge、窗口和消息循环，直到实际结束。`close`/`destroy`也接入相同屏障，包括WebView2尚未完成mount的准备资源。没有自动重放业务写入或恢复旧scope。

显式`close`/`destroy`一旦开始，`pump()`继续同一目标关闭操作，直到unmount及最终退休屏障完成；尚未打开inspector或配置了`detach`也不能提前报告完成。仅撤销尚未使用且未进入Host关闭的许可时，业务Host继续运行。

观察分别报告`attached`、`paused`、`visible`的yes/no/unknown，及opening/revoking/drained/failed。WebView2公开open没有可靠的窗口可见、连接或关闭事件，本候选这三项保持unknown；固定debugger实验不是产品观测依据。MiniBlink依据实际内核snapshot报告。

MiniBlink 的 `observation.callbacks` 明确标注 `miniblink_session` 域和 `sampled`：pending 汇总 interrupt、protocol queue、resource queue，active 汇总 dispatch、resource 与前端 paint。未采样时旧数值字段中的零不表示已观察到零；这些字段也不覆盖全部 Engine 或 OS 进程回调。原关闭逻辑继续依据真实 broker/session drain receipt。

原生 scalar diagnostics 可选调用 `read_miniblink_view_metrics`，独立观察已加载 DLL 内 `CWebView` wrapper 的创建、销毁和存活数量。首次 Runtime 必须以 `diagnostics::mode::scalar` 构造，后续 Host 不能开启已关闭的观察模式。读取只拷贝缓存；缺失 getter、忙碌或身份无效保持未知，不触发 JS、消息泵或生命周期操作。样本的 PID/birth/epoch 和模块路径须结合交付清单的 DLL 字节身份使用；内部 Blink 对象、合成器、子进程和回调分别计量，见 [ADR 0131](adr/0131-native-view-lifetime-observation.md)。

`0.6.0-preview.6` 的 WebView2 原生调用方可用第五个构造参数 `diagnostics::mode::scalar` 显式开启 `observe_native_resources()`。原四参数构造继续默认关闭观察。样本绑定真实 PID、进程创建时间、UI owner 和 adapter epoch；getter 只复制标量，并尝试读取退出回调的现有锁，不调用 COM、等待进程或泵消息。非 ready、busy、计数或退出观察故障都不能当作零。

其 view 范围是 SDK 实际持有的 Controller 引用：成功 Close 后仍保留的引用继续计数，只有实际 Reset 才记释放。callback 范围是已提交的 preparation/async 完成、SDK 策略事件和退出回调的实际借用；尚未进入 SDK 的 COM dispatcher 队列及引擎内部工作不在观察范围。退出事件订阅的等待 flag 不等于一条已排队回调。多个 adapter 保留各自 epoch 后分别汇总，不重复计入共享进程资源；这些数据不报告 Inspector 窗口数、attached/paused/visible，也不替代原始退出屏障。

`resources_retained=false`只表示后端物理owner已回收；失败仍为failed，不转成drained。WebView2 scope失败/未知、异常browser退出均保留失败；即使进程已确实终止并安全回收，也不能作为正常退休或敏感目标切换通过。Host退休失败后拒绝在同一Host重新启动新文档。

MiniBlink 前端资源退休返回 `BUSY` 时，调用方保留 endpoint、窗口和相关 owner，交后续 `pump()` 推进；尚未发布的 drain receipt 不能当作有效记录。只有原关闭序号及全部资源 fence 核准后才销毁 endpoint。真实 `FAILED` 与最终物理清理分别记录：资源排空后仍保留故障和 native error，不能因目标已销毁而恢复成成功。

暴露给inspector的旧目标在退休前不能reload或导航到新敏感文档，返回`E_TARGET_RETIREMENT_REQUIRED`。只有配置策略、尚未打开调试器时，普通reload继续按原契约处理并废止旧grant。

## 窗口内入口

模板中的`allow_native_devtools`默认false，由应用替换为自己的可信原生决策；不是网页、CLI或环境变量开关。菜单与F12/Ctrl+Shift+I共用同一个授权处理器。快捷键只处理当前业务窗口和其子窗口，不注册全局F12。WebView2将浏览器内的匹配键投递为`devtools_request_message`：WPARAM是virtual key，LPARAM低三位分别为Ctrl/Shift/Alt；消息本身不授予许可。收到请求后仍核`host.devtools_shortcut(...)`并走应用授权。

结束菜单明确为“结束诊断并关闭当前窗口”。应用需要恢复业务时，在旧Host及后端退出屏障完成后建立新目标和新授权。SDK不猜测WebView2 inspector HWND或用模拟关闭按钮作为保证。

## 资源与交付边界

### 源码SDK与二进制安装目录

SDK当前版本以交付根目录的`VERSION`为准，具体候选通过自身清单与散列识别；既有0.5.0交付保留。通过`tools/sdk/soui-sdk.cjs pack`生成、由`New-Project.ps1`安装的源码SDK包含公共实现、`cmake/SouiDevTools.cmake`、Node合成/部署工具、正式前端源覆盖及本文档；两种模板从匹配版本的生成器取得。生成器在创建输出目录前拒绝SDK包与模板版本不同的组合。应用自己的`VERSION`可以独立变化，DevTools部署元数据始终取SDK头文件中的完整版本，并核对存在的`sdk-lock.json`。

`cmake --install <build-directory> --config Release --prefix <new-prefix>`提供另一种交付：已编译库、公共头、ActionJson头及许可证；构建过WebView2组件时也安装其静态库和Loader。它不安装模板、`SouiDevTools.cmake`、Node部署工具或离线inspector供应输入。原生消费者可只用导出目标编译链接：

```cmake
cmake_minimum_required(VERSION 3.24)
project(NativeConsumer LANGUAGES CXX)
find_package(SouiSdk 0.6.0 EXACT CONFIG REQUIRED COMPONENTS WebView2 ActionJson)
add_executable(native_consumer main.cpp)
target_link_libraries(native_consumer PRIVATE Soui::WebView2 Soui::ActionJson)
```

配置消费者时通过`CMAKE_PREFIX_PATH`选择安装目录；搬移后在新构建目录重新配置为新prefix。只使用基础SDK时可不要求WebView2，并链接`Soui::Sdk`。CMake的数字版本为`0.6.0`，完整预览身份由`<soui/version.h>`的`SOUI_SDK_VERSION_STRING`区分。新增虚函数及Host布局要求旧原生消费者重新编译；旧adapter不必实现新可选方法，默认返回不支持。二进制安装消费者的编译链接不证明已部署离线资源；完整应用的配对资源、原生策略及安装运行仍按下面的源码SDK交付路径处理。

### 配对离线资源

MiniBlink使用配对的内核和离线inspector。原生配置必须提供安装交付固定的内核image与`approved-manifest.json`完整SHA256；不能对待验目录现算一个值再当作信任pin。worker核实际加载内核文件、外部manifest pin及全部member散列，无任意网络读取，拒绝链接逃逸和超限资源。ABI兼容不能替代资源配对身份。本地配对、部署与源码诊断档已有各自产物证据；完整安装消费者、搬移及许可准入仍需按声明范围验收。

正式前端源覆盖位于`sdk/resources/devtools/miniblink/`。先用`node tools/internal/devtools-frontend.cjs compose`合成供应前端：显式传入`--baseline-root`（仅包含原清单650成员）、`--baseline-manifest`及`--baseline-manifest-sha256`、`--source-root`及当前`source.json`的`--source-sha256`、`--kernel-sha256`与全新`--output`。文件路径使用本地绝对路径；基线manifest可位于基线目录外。工具核对原清单、七份正式源覆盖及逐项基线pin，输出`payload/`、新的`approved-manifest.json`和`composition-receipt.json`。覆盖源使用LF，与源码SDK安装的文本规范化一致。此步骤只生成技术材料：不验证内核运行，不生成外部批准，不改变原生策略。将所选内核、合成的650成员及新清单、原许可材料和另行审查的配对文档组成完整供应输入，再显式选择其pin。

构建工具的`-WithDevToolsPackage`显式选择离线MiniBlink交付材料，必须同时给出`-DevToolsPackageRoot`、`-DevToolsKernelRelative`、`-DevToolsKernelSha256`、`-DevToolsManifestSha256`、`-DevToolsApprovalRelative`和`-DevToolsApprovalSha256`。前三个SHA值分别固定实际内核、`approved-manifest.json`和外部批准文档；来源是应用已审查的供应输入，工具不会自签或下载。这些构建参数只选择复制材料，不授予会话权限；应用源码的`allow_native_devtools`仍默认false。WebView2使用Runtime前端，不选择这份MiniBlink离线包。

当前本地未发布的`0.6.0-preview.9`源码SDK、生成项目锁及默认依赖目录仍保留原预编译MiniBlink身份；本轮WebCrypto与x86字体回退修复的受控组件另以完整、可搬移的本地供应目录交付，不自动替换默认组件。应用必须从该供应交付的索引选择对应架构的目录、内核相对路径、批准文档相对路径及三个已审SHA256，保留全部前端、清单和许可成员；不能只复制`node.dll`，也不能混用两架构或不同配对的pin。

在既有兼容模板项目中，原Web产物身份已核对时可用`tools/Build.ps1 -Architecture x64 -Configuration Release -SkipWeb`，同时附加上述`-WithDevToolsPackage`及六个明确值。公开入口会重新配置、准备固定头文件、选择受控内核来源并验证最终部署，不要求重建SDK四格或改变应用源码、`sdk-lock.json`、默认catalog。该选项仍不打开Inspector；默认禁用策略及每次原生授权要求不变。新的构建和运行包须记录实际内核、批准与完整payload身份，旧默认组件构建记录保持其原身份。

供应根目录的文件闭集为清单、清单运行成员、指定内核、批准文档及许可证。批准文档格式如下，摘要和字节数以实际供应输入为准；示例占位符不能直接使用：

```json
{
  "schema": "soui.devtools-package-approval/v1",
  "backend": "miniblink",
  "kernel": {"path": "kernel/node.dll", "sha256": "<approved-kernel-sha256>", "version": "<kernel-version>"},
  "frontend": {"manifest": "approved-manifest.json", "sha256": "<approved-manifest-sha256>", "version": "<frontend-version>"},
  "licenses": [{"path": "licenses/LICENSE.txt", "bytes": 1, "sha256": "<approved-license-sha256>"}]
}
```

`soui_deploy_devtools`在应用编译前校验并准备材料，生成`soui_devtools_package.h`，其中只有固定pin、版本及相对路径，没有策略或grant。普通runtime目标随后复制已核内核为`node.dll`；最终部署再核实际应用的内核，事务替换`devtools/package`，保留容器内其他数据。inspector仅能读取`devtools/package/frontend`中原批准清单成员；许可证与交付元数据位于其外。payload记录只收录经过`verifyDelivery`核对的精确文件闭集，失败即拒绝打包。未选择该选项的下一次构建显式关闭CMake缓存中的部署开关；已有输出目录内的旧配对材料不会自动删除，生成干净运行包时仍须核对闭集与版本。

源码诊断继续使用`tools/Build.ps1 -WebProfile source-diagnostic`，默认`distribution`不携带业务源码。两种档位都可使用Release原生构建。配对包和source map有各自身份；部署inspector不自动生成或恢复业务源码。已有已审本地配对供应输入、文件部署及上表明确范围内的真实Sources断点；这不等于完整安装/搬移通过或公开发布审批。其他架构、档位与安装消费者仍按各自身份核验。

兼容模板、auto模板的compatibility目标与Workbench在Vite配置加载时调用`tools/internal/compat-polyfills.cjs`。该构建步骤核对本项目实际安装的legacy插件、core-js及SystemJS身份，仅适配锁定的`Object.create`模块；Chrome 49已有原生实现，旧IE的iframe回退不适用于本地资源策略。其他自动polyfill继续保留，未知依赖或链接路径明确失败。不要手改构建后JS、放宽资源扫描或把普通Vite alias当作嵌套polyfill构建的替代。npm重新安装依赖后，下一次配置加载会重新核对并生成相同适配。

显式Fixed WebView2 Runtime的`std::filesystem::path`在实际Loader边界规范为Windows分隔符，包括自动宿主的前置探测；调用方路径不被修改，空路径仍选择Evergreen。2026-10-23的x64 Release试验保留历史身份；后续 `modern-first` 两架构在Fixed Runtime155.0.4283.62的实际范围见上表。Fixed/Evergreen、其他Runtime和OS仍分别认证，不从局部结果推断完整支持矩阵。

Node构建工具已覆盖普通符号链接、目录junction、硬链接、路径别名、受限读取及前后文件身份；未直接查询Windows的所有第三方reparse tag，不能以这些工具测试代替原生reader的Win32属性检查。

Console具有目标页面原有效业务能力；Network、Storage、源码及导出都可能包含敏感数据，不提供只读或自动脱敏承诺。默认日志不记录协议正文、bearer、Console或源码。

候选ADR见[0123](adr/0123-public-devtools-candidate.md)。运行与支持范围以主任务的D01–D14证据为准；Win7/PE、x86、Fixed/Evergreen及两个后端分别认证，不从单个编译或原型外推。
