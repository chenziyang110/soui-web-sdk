# ADR 0051：三种页面共用参考业务

状态：候选，未采用，未执行。

## 背景

W6 / 05-06 要求现代和兼容页面在万行列表操作期间执行真实 C++ 任务，并完成表单、图表、对话框、取消、完成与明确重载结果。现有 Workbench React 局部具备列表/任务，Vue 和最小 Modern starter 尚非同一业务。扩大两个 starter 或复制三个行业产品会改变模板边界及验证负担。

## 决定

新增可选 `demos/reference-workbench`，一份固定数据、pure 业务模型、任务观测控制与原生 Bridge 注册；React 18/19 和 Vue 3 只适配页面。使用原公共 actiongen、任务服务和 SouiGui，不增加 wire 协议、权限、任务服务或反向 SDK 依赖。数据量、草稿、选择、任务时间及观察都有上限；普通消息仅携带固定样本标识与条件。

任务 worker 不改 UI。业务对象、Bridge、Host 明确由外到内持有，关闭先完成 Host destroy，Bridge 析构覆盖真实任务 join。导航撤销原 scope；重载 UI 草稿可恢复，旧任务必须解释为不能跨新文档续接，不能自动重放或制造终态。普通浏览器明确禁用原生动作，不模拟 C++ 成功。

现代与兼容锁保持当前已冻结版本；现代使用语义 HTML / SVG，兼容只复用已有 PrimeReact / Element Plus 有限控件及当前转换边界。特定页面实际可用不等于所有新 JS/CSS 可用。PE、Win7、GPU、真实 IME/键盘、长期资源和呈现资格独立待验。

## 备选与迁移

不扩张 Modern starter，不移走或修改 W2d7 固定源码，不复制媒体/文件扫描等行业产品。新增模块显式 CMake 选择，不加入默认 Core。Root 执行 actiongen 后检查生成物，并在采用时更新源码清单、许可/打包边界和当前验证输入。Agent endpoint 采用已封存的 Bridge-owned stopped 工厂后再接入，不借私有 store 或第二套任务服务。

## 验证

先测共用纯边界及实际 Bridge 任务，再对三页面执行相同业务步骤并绑定各自真实 Host / Owned 退出。程序化中文赋值不是 IME；业务探针和 rAF 不是实际呈现证据。候选未执行，所有资格保持 false。

## 固定工具链的实际选择

`CMAKE_VS_PLATFORM_TOOLSET_VERSION` 表示 MSBuild props 文件名版本；CMake 会转换三段工具版本，不能用它判断 `VC/Tools/MSVC/14.44.35207` 目录。完整参考入口采用已独立审查的业务测试入口同一窄检查：请求必须精确为 `v143,host=x64,version=14.44.35207`，compiler ID 必须为 MSVC，SDK 必须为 `10.0.19041.0`；实际 CXX 路径规范化后必须等于所选 Visual Studio instance 下的 `14.44.35207/bin/Hostx64/x64/cl.exe` 或 Win32 对应 `Hostx64/x86/cl.exe`。不接受其他架构或未固定版本回退。Root 仍负责工具原件 hash、环境、fresh IntDir/OutDir、实际项目 SDK/Release CL/LINK 及执行记录，目录比较本身不等于完整系统头文件或编译器 provenance。

这个 V4 只更正工具链检查及相应命令说明，原业务、任务关闭顺序、三个前端、锁文件与测试保持 V3 原字节；完整参考 CMake 尚未执行。有限纯算法/真实公共任务入口记录不代替三视图 Host/GUI/键盘/重载、安装示例、性能资源或发布资格。

## 兼容前端输入的独立闭合

V2 前端实际运行在 React Vite 阶段因未复制的旧 Demo runtime 相对路径失败，不能追认为 reader 假拒绝或三视图通过。参考目录现在拥有 web-compat/src/compat-runtime.ts 与 tools/vue-miniblink-compat.ts；入口和 Vite 精确引用这些文件，不再要求相邻 Demo tree。runtime 放在 Web 项目内，唯一 bare import resize-observer-polyfill 使用原锁已有 1.5.1 模块及对应 node_modules；不是新的库或版本升级。Vue 转换保持原 whole bytes、原 runtime-dom hash 及三处 patch 约束。

运行时保留全部 feature guards 和现有 shim 执行逻辑。迁移仅删除与 ES2022 lib 重复的可选 Array.at/String.replaceAll/Object.hasOwn/Promise.any 声明，为既有回调增加明确 this 类型，并用类型断言/URL 参数类型适配 TS5.9 已有构造器声明；不增加 ts-nocheck，不关闭 strict/typecheck，不称这些局部降级为完整最新标准。原 before bytes 和每个类型修改记录另存。Root 需真实重新 typecheck、三次 Vite/classic/dist 和原锁审计；既有失败原件保留，全部 GUI/IME/Agent、资源、目标 OS 与发布资格仍 pending。
