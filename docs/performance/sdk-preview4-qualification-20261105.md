# preview.4 追加验收与交付状态（2026-11-05）

当前候选可供开发评审；真实输入延迟和有效呈现帧率尚未验收，不能作为性能目标已达成的交付。`performanceVerified=false`、`releaseReady=false`、整个 Goal 未完成。本记录追加 `repair-r14` 的实际结果，保留 [repair-r13 交付报告](sdk-preview4-delivery-20261105.md)、旧失败和已冻结 ZIP，不覆盖旧测量。机器记录为 `verification/performance/sdk-input-frame-20261105/qualification-r14.json`，证据路径相对于 `PR129-performance-20261104` 验收包。

## 新增通过的限定验证

产品源码仍为 `0.7.0-preview.4`、`e1d2dd8b7699930f678e21a77772b8fa4e371f5b67a53937d98470ee1659e87c`，2,587 个构建输入。本次没有产品代码变化，没有重复 x64 SDK、前端 ci/build 或已经核对的四份运行包压缩。

| 实际项目 | 结果 |
|---|---|
| 官方 x86 Release SDK | MSVC v143 14.44.35207 / Windows SDK 10.0.19041.0；`Build.ps1 -Architecture x86 -Configuration Release -SdkOnly -WithWebView2 -WithGuiBindings -SkipWeb` 及官方收据验证通过，自然退出 0、Job 清空 |
| x86 SDK build ID | `d010788e-0390-4fa4-89ee-5a9912459583`；收据 SHA `9b67f7123b5f948c617b6ba91e7755c40596923604095d0667be08279df98b98` |
| x86 独立安装 | 471 文件 / 58,116,494 B；仅静态 SDK 库、头文件、配置，不包括运行库和应用 ZIP |
| x86 PerformanceHost | 与 x64 相同 14 文件源码集；真实编译链接及 42 项原生自测通过；EXE 1,426,944 B / SHA `896082da87efbbf6926bde5fa6a5ded2cf1cb7e221fba15a49277200579568d3` |
| 当前源码包工具 smoke | `node tests/sdk/sdk-package-smoke.cjs`：16/16，通过所有权、完整性、升级回退、档位、IO 恢复和路径检查，自然退出 0、无强清 |
| x86 MiniBlink 功能 | 独立隐藏 desktop 中正常 bootstrap/probe ready、30 次 canvas 绘制调用、实际保存及原 Bridge 正常关闭；EXE 与加载的 node.dll 均确认 x86；Host/Runtime 自然退出，Job 清空 |

上述功能页这次使用正常 `bridge.probe.report`，确认 `applicationReady=true`、document/bridge ready 和 HWND visible/non-iconic 标记；最终 SDK/kernel trace 来源完整，没有记录到 unregistered writes、dropped 或 causal lost。可见标记和隐藏 desktop 运行不证明物理显示、持续前台、真实输入或 FPS，也不证明跨所有计数器的原子全局快照。x64 旧自定义功能页未发送 probe 而留下 ready=false 的记录保持原样，不把这一夹具差异归因产品缺陷。

## 基线测量来源的修复边界

旧 K0 内核 `d888637d…` 的 3,750 次鼠标输入样本曾在 Runtime 关闭阶段报告两次未登记写入，仍为失败。源码核查定位到 V8 线程池回调没有进入 trace producer scope。现在在独立覆盖目录只重编 Gin 一个 translation unit，保留原 K0 的 `InputFrameTrace` 对象及其他冻结链接输入；3,536 个实际输入逐项核验后，编译和链接均自然退出 0。

新 **诊断变体** 为 39,205,888 B / SHA `76df23c64507f3e8fa6d9bf1a2417f2ed04b812443511feec582089c80a98253`，比原 K0 增加 1,536 B。它没有替换已发布 SDK 0.6.0、原 DLL 或 preview.4 默认运行库，也不是全内核重建或 v143 内核资格。独立运行目录以 `EXPERIMENT.json` 标识，不冒用发布 runtime 收据。

该诊断变体完成一次正常 probe ready、30 次绘制、保存及正常关闭，来源终态完整且无已记录丢失；尚未重做原 3,750 次输入场景，不能声称修复了该场景或证明因果链完整。独立审查只接纳其为后续同 scope、同模式的 trace 诊断对照候选，不能称为原发布版 SDK 0.6.0 性能基线。producer scope 有首次线程注册分配、回调登记/退出同步及有限槽位复用成本；kernel 默认 scalar，`off` 也不等于零成本，不能靠关闭来源或忽略丢失制造改善。

为取得相同 10 文件的输入消费者，已准备当前 InputUiHost 源码链接 SDK 0.6.0 的驱动。首次实际执行在编译准入时发现其他编译进程，配置和目标编译均未发生；自然退出 1、Job 清空，原始失败保留。没有结束其他任务的活动编译，也没有将旧 EXE 重标为同源新基线。

之后只读确认编译通道已空闲，沿用相同驱动、源码及 SDK 0.6.0 安装库，在新 R2 目录第一次实际配置和编译通过：自然退出 0、Job 清空，profile/scalar 与 19 项 manual-report 合同通过，没有启动窗口。新基线 EXE SHA `efefa2de23f33e6f4efb85db2788ced1f7a80c41bb64f8c2c0a8ff2767671b90`，SDK build ID `e3ecec57-2ec7-48ba-817e-5a749b8e2e17`，未覆盖新版头文件。两侧当前输入消费者的 10 文件源码集均为 `6950caccc479f823e034beb2e2ad2b4b279ca757f4c1b9aa0086548af2e0b1e6`；候选 EXE 为既有 `03d38044cf611ebf50d60946dc22fdf60801ee1e27572e03294d3c0de556f537`。这是对照准备，尚不是一轮输入测量。

## 已测体积及百分比

下面只补充上一份报告已完成的同源 x64 Release 对照的算术百分比，没有重新测量、压缩或改变载荷。两侧同一 14 文件消费者、页面、CRT 和法律文件，百分比为增量 / 基线。

| 范围 | 基线 B | 候选 B | 增量 B | 增幅 |
|---|---:|---:|---:|---:|
| Host EXE | 1,573,376 | 1,607,680 | +34,304 | +2.180280% |
| MiniBlink node.dll | 39,204,352 | 39,212,032 | +7,680 | +0.019590% |
| MiniBlink 完整应用 runtime | 46,869,801 | 46,911,785 | +41,984 | +0.089576% |
| MiniBlink 应用 ZIP | 17,015,120 | 17,034,622 | +19,502 | +0.114616% |
| WebView2 应用 runtime | 6,156,027 | 6,190,331 | +34,304 | +0.557243% |
| WebView2 应用 ZIP | 2,614,443 | 2,631,079 | +16,636 | +0.636311% |

WebView2 浏览器 Runtime 外置且未计入；x86 只有当前候选，无匹配整包基线，增量保持 unknown。用户允许小量、可解释增长，但没有冻结数值预算；旧严格零增长判定仍为 false。源码 SDK ZIP 及其散列沿用上一份报告，不因追加文档重打包同版本内容。

## 全部任务的当前状态

| 任务 | 当前结论与缺口 |
|---|---|
| P00 | 隔离分支、原改动保护、源码/载荷身份及 ADR 已有证据；PR 保持 draft，未合并 |
| M01 | 测量夹具、来源与身份检查已修；当前最终前台输入/帧率配对仍未取得 |
| I01 / F01 / W01 | 实现及针对性正确性证据已有；不把减少无效工作直接换算成 FPS 或尾延迟改善 |
| I02 | 当前共享 deadline 默认策略采用证据不足；默认预算保持 0 |
| W02 | 当前输出准备/持续 owner 公平性触发与收益尚未闭合；PREPARE_OUTPUT 默认关闭 |
| I03 | provider 前缀和长回调的当前因果成本仍需有效热点数据，不凭计数推断 CPU 占比 |
| F02 | 仅默认 opaque DIRECT_HWND 路径有不采用的限定依据；特殊复制路径仍未知，不能全局宣告不适用 |
| F03 | 帧相位与 GPU 呈现的触发/采用尚未闭合，未新增默认 present owner |
| R01 | 两架构限定构建、安装消费者、功能及 x64 体积已补充；真实输入/帧率与适用交互验收仍待完成 |

物理 Esc 停止后的桌面控制未恢复，没有向隐藏 desktop 注入输入绕过停止。下一次实际输入须在可独占且操作者明确准备好的目标环境进行；沿用 PID/创建时间、run、窗口尺寸/前台与原期限保护。原生 BurstInput 本身没有 Esc 终止检测，不能声称它具备该保护。基线噪声和阈值必须在查看候选前冻结；来源丢失、窗口失焦、身份不符或观察丢帧不得记为通过。

最终报告仍缺当前有效输入尾延迟、帧间隔/长帧、CPU/完整进程树内存配对，以及适用 IME、焦点、滚动拖动、多窗口、DPI/多屏和关闭交互。rAF、draw call、提交与桌面内容变化须分开；物理 input-to-photon 和唯一物理帧率不可观测时记 unknown。条件任务须依有效数据继续判定，不能仅因尚未测试就标为不采用。

交付和回退使用完整版本匹配的 SDK、运行库、消费者及收据。现有候选可交开发复核，但不能以本追加报告关闭性能目标、宣称达到 Electron 水平或生产就绪。

独立只读审查 `repair-r14/final-report-review-r1.json` 未发现阻断声明或证据矛盾。其非阻断建议是单独标明 R2 Host 输出身份；机器记录已补 EXE 1,171,968 B、相对输出路径及 SHA，明确 owned build 的 `60c9…` 是 PowerShell 包装进程身份，不是 Host。该审查没有运行构建、GUI 或性能采样。
