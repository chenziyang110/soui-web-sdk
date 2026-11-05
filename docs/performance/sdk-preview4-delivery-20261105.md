# SDK preview.4 交付复验（2026-11-05）

`0.7.0-preview.4` 修复了 preview.3 运行时部署清单仍引用旧 MiniBlink 散列的问题。新版 x64 SDK、源 SDK 安装和两架构运行库部署已通过限定验证；隔离桌面的 MiniBlink 短启动、页面请求、保存与正常关闭也通过。真实输入尾延迟、有效呈现帧率及性能提升仍未验收，`performanceVerified=false`、`releaseReady=false`。

旧[验收报告](sdk-input-frame-acceptance-20261105.md)和[preview.3 后续记录](sdk-input-frame-followup-20261105.md)保留当时身份与结论。当前机器记录为 `verification/performance/sdk-input-frame-20261105/preview4.json`，原始证据位于同一验收包的 `repair-r13/`。

## 修复、身份与回退

实际控制反例显示 preview.3 的 `runtime.deploy` 因旧 `runtime-files.json` 与新 DLL 不符返回 `E_RUNTIME`，目标目录没有创建。preview.4 更新 x86/x64 大小和 SHA-256，并将合同从文件名检查补为真实文件逐项字节/散列核验，没有绕过拒绝或修改旧收据。源码 SDK 补带 ADR0161、ADR0162、部署说明和补丁0034；Web/React/Vue 同步新版本与精确 peer tuple，不放宽未来版本。

| 对象 | 当前身份 |
|---|---|
| SDK | `0.7.0-preview.4`，x64 Release，MSVC v143 / Windows SDK 10.0.19041.0 |
| SDK build ID | `18e47fa2-69df-4530-b1f9-365f6b2aec28` |
| SDK source SHA-256 | `e1d2dd8b7699930f678e21a77772b8fa4e371f5b67a53937d98470ee1659e87c`，2,587 个构建源码输入 |
| x64 MiniBlink | 39,212,032 B，`56d460c0b4136d85d3fcb8d4e4f00c19a04ff56532228031b55368742ef54308` |
| x86 MiniBlink | 28,324,352 B，`bf6fd0bcb892f2997489a60d059671c9a618f4f99418d41f2c41f10ceae4dfb9` |

两份 DLL 复用 preview.3 已构建字节，本轮没有内核重编。来源仍是历史 `dd53b244…` 物理快照及两架构各一个 translation unit 重编、冻结 K1 输入重链接，见 `kernel-build-070-preview4.json`；不是全内核 fresh build 或 v143 内核资格。旧版本和原工作树保留，回退应选择原版本完整匹配的 SDK、运行库、消费者与收据，不把旧 DLL 覆盖进新版目录。现有共享预算、PREPARE_OUTPUT 等默认值保持原样。

## 实际执行结果

| 范围 | 结果与边界 |
|---|---|
| 官方 GUI 构建 | Web SDK、React、Vue 六条 ci/build 命令自然退出；后续原生 SkipWeb 核验同源码及产物，未重复安装构建 |
| 官方 SDK x64 Release | 构建及官方收据 verifier 通过，自然退出 0、进程树闭合；x86 SDK 消费者矩阵未执行 |
| CMake 独立安装 | 471 文件 / 80,244,800 B，仅静态库、头文件及配置，不含 DLL、应用运行包或 ZIP |
| 原生运行库部署 | x86/x64 各 59 文件；真实目录及所有大小/SHA 通过，不代表应用启动 |
| Node 契约 | runtime-deployment + GUI metadata 共 70 pass、0 fail、1 skip；skip 为环境无法创建文件符号链接，不称为全组通过 |
| Web SDK | 31/31 pass，0 skip |
| Performance Host | 当前 SDK4 安装消费者实际编译通过；42 项原生自测通过，无 GUI 输入 |
| InputUi Host | 已配置 cache 下首次实际编译通过；profile/scalar 与 19 项 manual-report 合同通过，无 GUI 输入 |
| npm audit | 当前 Web/React/Vue 三个包均自然退出 0；仅当时依赖审计，不是完整安全资格 |
| SBOM | 四份新交付清单已提升；对应七份 raw/delivery 文档通过官方 CycloneDX 1.5 离线校验，0 errors |

SBOM 重新读取 136 个交付原生文件；22 个静态 archive 只引用历史冻结身份，未重复散列。依赖完整性仍为 `incomplete`，授权审查未完成。关联修正前的 PREPARATION、Assembly 和成功 Validation 原件保留；修正最终关联后仅复验现有文档，没有重新生成 npm/BOM。

## MiniBlink 非输入运行复验

`repair-r13/preview4-functional-miniblink-r1` 在独立隐藏 desktop 运行当前消费者和上表 x64 DLL。专用功能页完成 bootstrap、身份查询、30 次 canvas 绘制调用、报告实际落盘，再通过原 Bridge 正常关闭。没有桌面输入，没有修改真实性能基准负载，也不把隐藏桌面调度作为性能样本。

Host 自然退出 0，owned Job 无强制终止、无遗留进程。`runtimeShutdown=true`、`allHostsClosed=true`、`inputTraceExportSucceeded=true`、`inputTraceFinalComplete=true`；SDK/kernel 来源均完整、无拒绝原因。最终 attempted 分别为 252/2,389，unregistered writes、causal lost、dropped、registration rejections 均为 0，active producers 均为 0。

该样本未复现关闭阶段未登记写入，但没有覆盖旧基线 3,750 次真实鼠标输入，不能证明完整因果链、实际帧率或 input-to-photon。保存报告的 native snapshot 仍为 `applicationReady=false`、`windowVisible=false`、`document=ready,bridge=waiting`；专用页的请求/保存/关闭成功不能扩大为标准应用 ready 或可见呈现通过。原 K0 source-loss 失败保留。此前物理 Esc 停止后的界面输入没有恢复，也没有用隐藏桌面输入绕过。

## 源 SDK 包

首次官方打包在目录发布 rename 阶段以 EPERM 失败，原始日志与 retained stage 保留。诊断确认 stage 的 1,592 个载荷文件及散列完整、无额外成员；Restart Manager 事后没有找到当前锁拥有者，历史失败原因仍未知。随后只执行一次官方 retained-stage recovery，成功后 verify-source 与 fresh fixture project 的 install / verify-project 通过，fixture 自有文件保留。原打包仍记失败，恢复单独记成功。

ZIP 的 1,593 个成员、CRC、大小/SHA 与恢复目录一致；安装来自恢复目录，未额外执行 ZIP 解压安装演练。源 SDK ZIP 为 38,452,424 B，SHA-256 `e7fa6a221c0c31b9aa765f5d53789389f1058b7be5c55a275a766f6b7c76a4bd`；载荷 1,592 文件 / 113,401,780 B。包内为源码、固定原生依赖和已编译 GUI bindings，不含首方原生静态库，不是完整应用运行包或完整 Git 源码归档。身份对应打包时 manifest，后续验收报告另存；源 SDK ZIP 大小不能代替应用 runtime / ZIP 对照。

## 同源应用运行包与 ZIP 体积

两侧用同一 14 文件消费者源码集 `53bad5158f3dc7290919717f3c93f0015ad9b6040544602d2e577ec46651d4cb` 分别链接独立 SDK 0.6.0 / preview.4 安装头库。旧基线 EXE 没有复用；只复用冻结的 R10 运行依赖和资源。两侧页面均为同一 3 文件 / 63,554 B，法律文件 203 项 / 886,164 B，CRT 同为 53 个且逐项一致；没有采用消费者工程后来增加的自动测量页面作为这一体积对照资源。

| x64 Release 范围 | SDK 0.6.0 | preview.4 | 增量 |
|---|---:|---:|---:|
| Host EXE（两后端同字节） | 1,573,376 B | 1,607,680 B | +34,304 B |
| MiniBlink node.dll | 39,204,352 B | 39,212,032 B | +7,680 B |
| MiniBlink 完整应用 runtime | 46,869,801 B | 46,911,785 B | +41,984 B |
| MiniBlink 应用 ZIP | 17,015,120 B | 17,034,622 B | +19,502 B |
| WebView2 应用 runtime | 6,156,027 B | 6,190,331 B | +34,304 B |
| WebView2 应用 ZIP | 2,614,443 B | 2,631,079 B | +16,636 B |

四个 ZIP 使用同一 7-Zip 25.01、ZIP `-mx=5 -mmt=2 -mcu=on`，全成员内容 SHA 与完整目录闭集一致。两侧分别为 MiniBlink 267 文件、WebView2 261 文件；没有增删载荷或新增 DLL。变化只有 Host、MiniBlink node.dll 和同长度运行库 receipt 的内容。WebView2 浏览器 Runtime 外置且未计入表中，不能把外置 Runtime 当作零大小或本次已完整重测。用户允许小量增长，但原严格零增长比较器仍返回 `pass=false`，没有改阈值或把它记成通过。

两个最终候选目录还各执行一次隔离启动、功能页保存与 Bridge 正常关闭，均 Host/Runtime 自然退出 0、Job 清空、trace final complete；MiniBlink kernel 的未登记写入为 0，WebView2 没有请求 MiniBlink 来源，记为未请求。随后复核两候选目录全部文件大小/SHA，均与压缩前快照相同，没有复压或把运行数据混进 ZIP。这些仍是非输入功能检查，页面 ready/物理性能资格没有扩大。

## 剩余门槛

实现和限定运行证据可供开发评审，R01 与整个性能 Goal 尚未完成。最终匹配载荷仍需真实输入与帧间隔 A/B、正常关闭边界以及适用 IME/焦点/多窗口/DPI/多屏回归。原噪声、漂移或 source-loss 结果不能继承为本版本通过；不声称达到 Electron 水平、生产就绪或全部流畅度问题解决。
