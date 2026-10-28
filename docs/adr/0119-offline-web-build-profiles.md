# ADR 0119：显式离线 Web 源码诊断构建档

日期：2026-10-22。状态：已实现待真实运行验证；本地构建与文件边界已验证。
不冻结 DevTools 公共 API，也不表示真实断点已通过。

## 背景与决定

Stage0 的 inline map 夹具与默认发行档已经分离，但公共 Build、模板和 Web
身份记录尚无档位。仅改变 Vite 的 sourcemap 选项，无法阻止 SkipWeb 跨档复用。

Build.ps1 增加显式 WebProfile：distribution 为默认，source-diagnostic 为应用
主动选择。它与原生 Debug/Release、后端、原生工具链相互独立。Build 只在本次
进程范围传递 SOUI_WEB_BUILD_PROFILE，并恢复原环境；该值不授予运行时调试权限。
两个模板及 Workbench 共用受管理的 Web profile helper，保留现有插件和目标。
默认不生成 map；诊断档生成 inline map 并关闭 JS minify，便于离线源码调试。

## 资源与身份

Vite 完成实际输出后写入 .soui-web-profile.json，绑定档位、真实脚本/map 散列
及当前应用/SDK 源码投影。诊断 map 必须自包含，不支持外置或在线 map；不增设
HTTP 服务，也不在客户机器改写资源。构建期核对业务 sourcesContent 与实际输入。
自有工具仅检查当前构建目录和获准的应用/SDK 源码根，不按 map URL 获取网络或
任意文件。第三方/虚拟源的观察与业务源码匹配分开，不宣称控制 Runtime 的读取。

模板默认只允许 app/src 和 SDK/web/src 业务来源。Workbench 在配置中显式
列出五个实际源码目录，投影保留目录前缀以区分同名文件。普通第三方来源仅可
归入应用或 SDK 的真实 node_modules 根；不因路径含 node_modules 就信任它。
legacy 插件的嵌套 polyfill 构建会产生偏移后的 core-js/systemjs 来源标识；
仅这两个固定包的规范子路径可回查应用当前安装文件，并要求源码文本完全一致。
Vite 对 MP4/WebM/GIF/PNG 的整段资产代理按生成的虚拟模块记录，原始媒体仍经
路径与字节预算检查；不能用该例外跳过 TS/TSX/Vue 原文检查。

单文件/map 16 MiB、累计 128 MiB、4096 文件和每 map 16384 sources 为预先设置
的工具预算。拒绝链接路径、未知 profile、外置 map、独立 TS/TSX/JSX/Vue 等源码
及不一致的 map/脚本身份。默认档额外拒绝 sourceMappingURL 和嵌入 map 内容。
这些检查不是任意内容的自动脱敏或秘密检测。

Web evidence 升级档位身份，Build start/finish 保持同一选择；SkipWeb 明确核对
选择与全部输出。部署复制前后核对 profile 和原始资源，保留现有原子替换、
合法材料和回滚。旧自定义应用的无 marker 纯 distribution 输出仍可经严格扫描；
无 marker 不得冒充 source-diagnostic，旧 Web evidence 必须重新记录。
record-web 和成功的 verify-web 都把 Web 身份绑定到当前 pending build；
SDK-only SkipWeb 也必须在原生阶段结束时复核。对已完成构建的单独读取保持只读。

标准 Build 在原生编译之后显式复用部署函数，即使 CMake POST_BUILD 因无需
重链接而没有执行，也刷新当前档位资源。finish 再核对已记录的 Web 输出及
实际应用资源，不能把旧档位载荷记到新构建。旧直接 API 的 distribution
调用仍作资源扫描；正常 Build 和所有诊断构建必须核对 Web evidence。

## 所有权、兼容与验证

helper 位于 tools/internal，随现有受管理工具安装；模板仍由 New-Project 的
相对路径规则重定位，不依赖开发目录。SDK 不依赖 Demo。旧 0.5.0 包和安装不改。
这项工作仅接上 Stage0C 与后续交付所需的构建基础，不开放调试会话。

验证只运行受影响的 profile、身份、部署边界及各改变的 Web 配置，不重跑原生
构建或 VM。两个后端的 Release TS/TSX 真实断点、懒加载与异常栈、安装消费者
和最终生产矩阵仍必须另行执行；静态 map 通过不能代替这些证据。

2026-10-22 已执行：三个实际 Vite 配置各有两档成功构建；最终 helper 重新
核对这六份未改写的输出，保留每次构建原源码身份。媒体代理修复后只重建受影响
的 Workbench 诊断档。诊断 map 数分别为 desktop 3、modern 1、Workbench 297。
定向 Node 边界批次 113 项通过、2 项因文件链接权限跳过；随后媒体代理 3 项
通过。独立审查发现 SDK-only SkipWeb 末次漏检，实际回归先失败，再经修复取得
受影响 integration/入口 18 项通过。失败样本、原命令与字节身份保存在
out/devtools-stage0/web-build-profiles/verified-local.json 引用的原始记录中。
未运行原生 Build.ps1、安装消费者、真实 Runtime 或断点；Stage0C 保持待验收。

## 2026-10-23 兼容构建适配

真实本地资源准入拒绝了core-js 3.50.0的旧IE `Object.create` iframe回退。
Chrome 49支持原生`Object.create`，但CommonJS打包仍带入该不可用分支。
在兼容Vite配置加载时，通过源控helper核对实际legacy/core-js/SystemJS版本、
模块原始身份及本项目普通文件边界，再派生只使用原生能力的模块；缺能力明确失败。
保留其他自动polyfill、SystemJS、Chrome 49目标、资源扫描和map来源核对。
普通Vite alias不会进入legacy插件的独立子构建，不能作为此处修复。
适配源和依赖身份随产物证据记录，npm重装后可重复生成，不手改发行JS。

相同日期的现代后端测试变体已真实观察到离线TS debugger暂停与单步。
兼容变体在VM期限前未完成业务启动，尚未证明断点。完整Stage0C与生产矩阵继续待验；
上述历史构建通过记录仍绑定原输入，不改挂到此次适配。
