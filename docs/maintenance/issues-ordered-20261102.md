# 开放 Issue 顺序处理记录，2026-11-02

本轮基于 `main` 的 `ce9757e33e52ac680802691ff7e3a042eca86d52`，在独立工作树
处理九个开放 Issue。新源码候选为 `0.6.1-preview.1`，未发布；已发布的 0.6.0
和历史原件保持原身份。机器状态引用见
[本轮事实](../../verification/production-validation/issues-ordered-2026-11-02/RESULTS.json)。

## 按编号核对

| Issue | 已核实的事实与本轮处置 | 保持开放的条件 |
|---|---|---|
| [#23](https://github.com/chenziyang110/soui-web-sdk/issues/23) | 总验收依赖下列专项；采用本表逐项记录，不以单项编译替代整体结论 | 完整生产能力、独立交付、资源/性能、目标平台和采用价值仍缺 |
| [#27](https://github.com/chenziyang110/soui-web-sdk/issues/27) | 0.6.0 双架构真实 Explorer 恢复、显式移除后不复活及自然退出已有原件，不重复该通过输入 | 焦点归还、剩余故障/所有权边界、Win7 仍未闭合 |
| [#29](https://github.com/chenziyang110/soui-web-sdk/issues/29) | 双架构有限三窗、重载、空窗驻留和最终退出已有通过；保留 SDK 与纯 Win32 对照的 `E_TRAY_FOCUS` | 失败根因未定，真实菜单/快捷键完整返回、业务隔离、模态关闭和 Win7 仍缺 |
| [#31](https://github.com/chenziyang110/soui-web-sdk/issues/31) | 续期及独立容量通道已有实现；现夹具仅覆盖任务/订阅，最新交接未提供真实 52 分钟成功原件 | 先补目录/媒体与双层满载的夹具、schema 和分析器，再冻结 52 分钟及八小时输入；不以模拟时钟补证 |
| [#39](https://github.com/chenziyang110/soui-web-sdk/issues/39) | 本轮重跑 0.6.0 精确 DLL/MAP 校验；OpenSSL 实际成员为 x64 486、x86 489；已有 24 组件 crosswalk 和材料核验 | 文件/嵌套条款、来源冲突、审核责任与维护者审批仍缺，工程校验不构成法律批准 |
| [#42](https://github.com/chenziyang110/soui-web-sdk/issues/42) | 本轮重验历史 V11 的 43 份原件和 54 帧串口；80×60 缩放通过只属于原 Win7 x86 输入 | 当前载荷完整滚轮、中文 IME、公共任务并行和关闭缺证；尚无可启动并已确认身份的 Win7 SP1 x86 环境 |
| [#77](https://github.com/chenziyang110/soui-web-sdk/issues/77) | 字符串界限早已修复；本轮用现有读取器重验两架构候选，各 4 文档、32 行为、40 sidecar，原进程自然退出 | x64 基线在原 20 秒 Runtime 关闭期限失败，x86 基线未执行，四臂整体仍失败；不延长期限或改写旧内核结果 |
| [#96](https://github.com/chenziyang110/soui-web-sdk/issues/96) | `main` 已修复 0.6.0 的机器状态版本；本轮通过正式状态记录入口登记新候选的未验收状态，保留历史报告 | 完整 Collector、发布矩阵、目标系统、长期和许可条件仍缺；本轮不生成发行包 |
| [#133](https://github.com/chenziyang110/soui-web-sdk/issues/133) | 参考现有 PR #134，补齐默认关闭的动态 Loader、原子导出检查、缓存切换、安装/部署、认证和构建输入 pin；验证范围如下 | 维护者评审、真实 Win7/完整 AutoHost 页面启动和最终交付仍待执行；代码回归不自动关闭这些条件 |

上表引用的原件与缺口详见 [0.6.0 专项收尾](open-issues-20261029.md)和
[长测交接](../testing/v0.6.0-long-run-handoff.md)。本轮另重算四组历史档案的
1,161 个索引文件；只读校验不等于重新运行产品，不将其结论升级到新候选。

## #133 的行为与验证

静态 Loader 保持默认。`Build.ps1 -WithAutoHost -WithSharedWebView2Loader` 显式
启用动态档，统一入口对应 `--with-auto-host --with-shared-webview2-loader`。
不带动态开关的标准构建明确选回 OFF；直接 CMake 使用
`-DSOUI_WEBVIEW2_LOADER_SHARED=ON`。源码与安装消费者通过
`soui_deploy_webview2_loader(target)` 部署同架构 DLL，内置 Modern/Auto 模板已接线。

发现 Runtime 之前同时核对两个必要导出；DLL/导出缺失可表示可选后端不可用。
权限、坏映像、策略和未知错误仍失败。模块和首次结果保持到进程退出，不在运行中
换后端或重放业务。路径仅来自主 EXE 目录，拒绝 CWD/PATH 搜索；真实损坏映像
测试发现错误对话框阻塞后，增加本线程错误模式的有界作用域处理，原 10 秒超时保留。
决定与 Win7 安全加载标志边界见 [ADR-0138](../adr/0138-optional-webview2-loader.md)。

| 实际检查 | 范围与结果 |
|---|---|
| SDK 编译、安装及公开消费者 | MSVC v143 `14.44.35207`、Windows SDK `10.0.19041.0`，x86/x64 Release，静态和动态两档；独立仓库外消费者编译、真实空 Fixed Runtime 发现和可选 DLL 移开后运行通过 |
| PE 导入与部署身份 | 动态 EXE 无 `WebView2Loader.dll`、`EventSetInformation` 硬导入，部署 DLL 与认证包逐字节一致；静态 EXE 对照确有 `EventSetInformation` 硬导入 |
| 官方动态 DLL | 两架构普通导入表无 `EventSetInformation`，延迟导入表中存在该符号；没有据此认证实际 Win7 装载或现代 Runtime |
| 原生 Loader | 双架构 SDK19041 各 6/6；缺 DLL、各自缺导出、完整 ABI 调用、坏映像、文件锁；另检验并发、首次失败固定及 CWD 拒绝 |
| CMake 与认证输入 | 两架构实际 OFF→ON→OFF 各 3/3；认证包派生的静态/动态输入检查 28/28 |
| 构建/入口读取器 | 集成回归 81/81；模式或 Loader 字节被改写、删除、换路径，以及动态档缺少 pin 均拒绝；保留旧静态报告原范围 |
| SDK/Web/版本 | Web SDK ci/build；客户端、续期与状态记录回归 68/68；SDK 包 smoke 15/15；版本/GUI 原轮 36/37，受并发输入影响的一项独立重跑通过；三个受影响包 npm audit 均 0 项 |

SDK 编译使用显式 CMake 入口，不冒充标准 Build/Owned 过程或完整发布验收。未运行
Debug、应用全矩阵、浏览器创建/页面启动、真实 Win7、完整性能或长测，不创建新的
发布资产。原生模拟 DLL、Node 读取器和安装发现消费者各有独立范围。

保留的失败包括首次坏映像错误 UI 超时、PS5 认证夹具准备的两次失败、版本测试中
输入改变的一次失败，以及 CLI 用例缺 runner 的初次准备失败。导入检查的首次配方
错误把延迟导入也当作硬导入；保留原输出，随后明确分别核对两张表，不将延迟导入
改写为不存在。历史产品失败继续保留原结果。

## 审查与回退

本轮仅生成新候选源码及评审材料，不合并 PR 或发布。关闭动态开关并重新编译可
回到静态档；两档使用独立安装前缀和应用目录，避免覆盖现有载荷。已发布的 0.6.0
与其 SDK 锁不变。生产、目标系统、许可及完整功能资格继续为 false。
