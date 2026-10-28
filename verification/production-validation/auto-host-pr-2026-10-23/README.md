# 自动宿主与交付工具 PR 验证范围

本 PR 使用独立候选 `0.6.0-preview.3`，从共享工作区提取自动宿主、三批打包能力
及必要关闭前置，不包含并行 DevTools 功能。内容与历史 `preview.2` 冻结快照不同，
历史通过不能作为本 PR 树已通过的依据。`releaseReady=false`，未发布。

## 本候选复验

已在隔离的 PR 工作树完成下列限定复验。
不将历史 12 次启动或 16 项原生测试记为 `preview.3` 的结果。

| 本候选检查 | 结果 | 本地原始日志 |
| --- | --- | --- |
| 17 文件交付/打包回归 | 335 通过、0 失败、3 跳过 | `out/pr-packaging/tests-final.log` |
| GUI/交付定向回归 | 20/20 | `out/pr-checks/gui-delivery.log` |
| AutoHost/SDK 定向交付回归 | 30/30 | `out/pr-checks/delivery.log` |
| Web SDK 回归 | 31/31 | `out/pr-checks/web-sdk.log` |
| 6 组 npm audit | 6 组均报告 0 项漏洞 | `out/pr-checks/audit-*.json` |
| x64/x86 Release Build + Install | 两架构通过 | `out/pr-checks/build-*-verified.log` |
| x64/x86 定向原生 CTest | 每架构 10/10，共 20 项通过 | `out/pr-checks/ctest-*.log` |
| x64/x86 安装版 CMake 消费者 | 配置、编译、链接、执行均退出 0 | 独立消费者原目录保留 |
| 仓库 AutoStarter：WebView2 / MiniBlink | 每架构 2/2，共 4 次通过 | `out/<架构>/auto-smoke-Release.json` |
| 既有 compatibility / modern 启动入口 | 两架构对应入口均退出 0 | `out/pr-checks/{compatibility,modern}-*.log` |
| 默认 auto ZIP 打包 | 两架构成功并核对归档身份 | `dist/SouiAutoStarter-0.6.0-preview.3-windows-*.zip.json` |
| 新生成外部 auto 项目 | x64 构建及两后端真实启动通过 | 独立消费者原目录保留 |

本候选构建输入 SHA-256 为
`9a401414e18016e26159d6829d5f4c7b7548fb7ff555d4ac2c691c8060e91d29`。
x64 构建 ID 为 `41ed553e-8f2f-4a1b-81d9-07753eff5828`，x86 为
`3da951e4-8999-40eb-af99-89f6c7c3e382`。原生回归包含 AutoHost 选择、profile
预检、回调异常、非阻塞 unmount、Host/Runtime 与 WebView2 准备生命周期。
安装版消费者使用 `find_package(SouiSdk 0.6.0 EXACT COMPONENTS AutoHost ActionJson)`，
实际引用各架构安装目录的库；执行只覆盖 STA 构造/关闭，没有启动浏览器。

本候选仓库应用的 4 次自动启动均取得页面就绪、业务往返与应用 Job 自然退出证据，
真实选择分别为 WebView2 和 MiniBlink。既有入口记录的
`controllerNaturalClosure=external-caller-evidence-required` 保持原值，不能据入口
退出 0 扩大为整个外层调用链已取得独立自然关闭证明。

另从本候选正式生成 SDK 包和仓库外 auto 项目，验证嵌套 `webTargets` 对象、
`publicAssets` 数组、SDK 锁及源身份；无手工业务修改。生成项目的独立 x64 Release
构建 ID 为 `fa5bc7dd-0b34-4ce5-9f7d-93189247c85a`，源码身份为
`bfd46e11c178050049de6975074d907975fe15ceb3789f06a551bb8e90fea5bb`。
其 WebView2 和实际空 Fixed Runtime 降级 MiniBlink 各启动一次，均完成原生
`app.greet`、ready、close 和进程组自然退出。当前共 6 次 auto 启动；生成项目
只复验 x64，未将历史生成项目 x86 结果迁移为本候选结果。

默认 auto ZIP 的 x64 大小为 16,906,361 字节，x86 为 14,291,421 字节，各含 273
个载荷文件。当前候选没有重新执行 ZIP 解压后的真实启动；历史解压启动仅属于
后文 `preview.2`。本轮也没有重新测量 `preview.3` 的三档压缩时间和内存开销。

上述回归存在重叠，不累加为独立测试总数。3 项跳过均因本机无法创建文件符号链接，
涉及运行时链接拒绝、Web 输出链接拒绝和业务源码链接拒绝。17 文件命令见
[复验命令](commands.md)，日志字节数与散列见 [identity-summary.json](identity-summary.json)。
测试日志本身未嵌入源码散列，不能凭它们推定任意后续改动也已覆盖。

首次 338 项运行保留为 322 通过、13 失败、3 跳过：其中 11 项由新 worktree
缺少 desktop Vite 依赖导致，另外 2 项为 GUI 入口测试夹具仍按旧构建入口和
PowerShell 模块环境假设运行。安装既定依赖、修正夹具后重新运行全部 17 文件，
取得上表结果。GUI 夹具定向补测亦为 5/5，没有删除原断言或跳过失败用例。

`tests/gui-bindings` 的首次 npm audit 网络连接失败仍保留，重试才取得 0 项结果。
初次原生构建在测试夹具并行修改后被源码身份门禁拒绝；随后 `-SkipWeb` 因没有
可用成功 Web 证据被拒绝。这两次没有记为成功或绕过门禁，后续使用完整 Web
构建重新验证。对应失败日志和身份也保留在摘要中。

随后完整 Web 构建通过，WebView2 NuGet 下载因 TLS 连接失败中断；从已有缓存
取回依赖清单锁定的 nupkg，先核对 SHA-256，再由正式 Restore 入口重新校验。
没有更换依赖版本或关闭完整性检查。

## 历史自动宿主证据

2026-10-23，`0.6.0-preview.2` 冻结源码身份为
`add361cae615eff855260ddc1b6addf971c3b97e10e6668b0decb39d09121401`。
环境为 Windows NT 10.0.26200.0，WebView2 Runtime 155.0.4283.62，MSVC143，
Windows SDK 10.0.19041.0，Release。历史结果如下：

| 范围 | x64 | x86 |
| --- | --- | --- |
| 原生定向 CTest | 8/8 | 8/8 |
| 仓库 AutoStarter：WebView2 / MiniBlink | 2/2 | 2/2 |
| 全新外部生成项目：WebView2 / MiniBlink | 2/2 | 2/2 |
| 默认 ZIP 解压后：WebView2 / MiniBlink | 2/2 | 2/2 |
| modern 必需且 Runtime 不可用 | 预期退出 3 | 预期退出 3 |
| 安装版 CMake AutoHost 消费者 | 配置、链接、执行通过 | 配置、链接、执行通过 |

12 次正常/降级启动均完成原生 `app.greet` 往返、页面就绪、宿主清理和进程组
自然退出 0。交付定向 Node 测试为 30/30。两次预期拒绝保留真实退出 3，
没有把通用进程监督失败字段改为成功。既有 modern、Starter、Workbench
亦有双架构短启动回归；上述结果均绑定历史载荷。

| 架构 | 历史 SDK 构建 ID | AutoStarter ZIP 字节数 |
| --- | --- | ---: |
| x64 | `768ea38e-b6cd-4a36-b46c-31281c6e9694` | 16,978,177 |
| x86 | `da05ce90-def0-43db-bea7-ca2838117d81` | 14,356,671 |

两个 ZIP 均包含 273 个文件，解压前后逐文件核对。ZIP、构建、外部消费者、
原始测试日志和原索引的 SHA-256 保存在 [identity-summary.json](identity-summary.json)。
这些大小仅对应 SDK AutoStarter 示例包，不是 JZWinPassClear 安装包测量。

## 历史失败与修复边界

- 生成器曾用 JSON 默认深度将嵌套 `webTargets` 写成字符串；修复为显式深度，
  后续通过全新生成项目验证，没有手改生成结果。
- 模板自检曾等待尚未启用的按钮，形成初始化等待环；已调整 ready 顺序。
- 完成回调抛异常后曾仍允许运行操作；修复为保留 `E_CALLBACK` 并拒绝就绪和操作。
- 无效 profile 父路径为文件时，x64 曾在 Environment 后返回 Controller 错误，
  旧测试要求 Runtime 版本为空的断言不成立；x86 及两个独立非法路径探针出现
  无回调超时，残余进程由监督器清理。这些尝试保持失败，不能算自然退出。
- 最终实现只读预检明确不合法的路径，当前测试名称和断言为“profile 路径预检”。
  它没有证明所有合法配置下的异步初始化故障、立即取消或 Runtime 不回调均被解决。
- 首次生成目录重命名失败、构建时源码改变被证据工具拒绝等阶段失败仍保留。

## 记录与限制

原自动宿主归档中的 682 件机器记录保留在维护者的原验证目录，没有被改写或
复制进本 PR。这里发布的是派生身份摘要，原索引 SHA-256 为
`e4c1a4090eae496d7ea2129016ffdc8530613df6994bb05de8c2b54e9cd32494`。
摘要保留原始文件散列供核对，不替代原始日志，也不是可独立验证的完整证据包。
绝对开发机路径和用户信息未带入提交。历史压缩测量另见
[历史压缩测量](historical-compression.md)。

降级测试用真实 Loader 探测现有空 Fixed Runtime 目录返回的缺失结果；没有卸载
整机 WebView2。未完成 Win7/PE 实机、高刷、长时间稳定性、内存预算或生产准入。
统一选核没有直接解决 MiniBlink 高刷与流畅度问题，也没有实现下游 NSIS 安装与
卸载运行时共享。默认 ZIP、已有项目行为和固定 0.5.0 发行内容保持兼容。
