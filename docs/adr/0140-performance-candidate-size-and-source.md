# ADR 0140：双后端性能候选的体积与来源门禁

日期：2026-10-29。状态：代码候选，未编译/运行，性能与大小均未验收。

## 背景和决定

MiniBlink 与 WebView2/Scratch 共用发布体积不增长约束。使用 Node 内置能力记录压缩 ZIP、完整运行目录与 SDK/内核运行载荷，冻结架构/配置/工具链/功能/资源/打包参数；实际 ZIP 解压内容必须逐文件匹配运行清单，防止旧 ZIP 搭配新目录。新依赖、删文件或任一体积增加均阻止自动通过。源代码没有新增 DLL 不构成体积证据。

正式运行包不包含测试工具、训练产物和完整上游构建树；已有交付能力不得借此移除。相同功能的去重/算法修改优先于更多线程、buffer 或替换引擎。LTO/PGO/GPU/Worker/Composition 等条件变化只有在相应数据与兼容证明后才能采用，当前没有数据不默认开启。

Scratch 源码候选绑定 vendor-lock 指定的上游提交、依赖 lock 和修改前文件散列，提供 reducer 源级改动与真实 VM/draw 可选计数器。以原依赖锁和生产配置重建，保留完整对应源码、根构建配置、许可证及差异记录。Web 构建显式选择 SourceVendor，校验 dist 全文件清单和构建驱动；对照两端必须选择同一载荷。不直接修改缓存/bundle，不将源码版本字段冒充发布 artifact 版本。

MiniBlink 新的资源唤醒失败在实际 void post 拒绝路径显式标记；Host 通过版本 capability 和可选 getter 低频读取，粘滞失败使 application_ready=false，诊断为 E_RESOURCE_WAKE 并保留既有关闭/排空路径。不能把失败静默归为正常 waiting。旧 DLL 不声明新 capability 时不调用。

## 兼容、验证和回退

默认 WebView2 HWND 与 MiniBlink 软件路径继续使用既有权限、来源、线程归属及关闭契约。源码重编才改变运行行为；新头和旧 DLL 的可选能力由表内 bits 探测。动态适配器私有布局变化要求 SDK 与应用匹配重编。

相关回归入口见 `docs/performance/size-gate.md` 和各 lane 交接。当前仅静态检查，统一候选由专人集中编译、输入/像素/生命周期与大小/性能验收。源码候选未改变任何 production readiness 结论。回退使用原源码/内核及固定 npm artifact，不能将未通过大小门禁的候选替换到正式发布。
