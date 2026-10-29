# Scratch 学习工作室

此独立少儿编程 Demo 使用 SOUI SDK `0.6.0` 系列的公共 WebView2 Host，页面位于 `web/`。
应用版本随当前 SDK `VERSION` 固定；运行时不要求安装 Node.js、CMake 或 Visual Studio。

从 SDK 根目录构建并打开：

```powershell
powershell -File demos/scratch/Build.ps1 -Architecture x64 -Configuration Release
demos/scratch/Start.cmd
```

独立本地包：`powershell -File demos/scratch/Package.ps1`。
该入口会使用 `out/scratch-x64-package/` 重新构建，核验导出契约，并生成仓库上级
`deliverables/SouiScratch-<版本>-windows-x64-rN/` 与同名 ZIP、SHA-256 文件。
完整解压后双击包内“打开演示.cmd”。包中包括本地素材、当前接入/SDK 构建输入、
原始 Scratch npm 分发包与许可证。显式 `-SourceVendor` 的包另含完整补丁源码、锁、构建记录与 dist 清单，核对部署身份后随包交付；实际源码版本与对照分发版本分别记录。
只交付本地未签名候选，不执行公开发布。

开发机需 Node.js、MSVC v143、Windows SDK 10.0.19041.0 和 CMake 3.24 以上。
构建入口复用主线的 MSVC compiler phase 自然收尾与 node-reuse 策略；已有其他 compiler/VCTIP 进程时会拒绝此次独占阶段，并保留阶段记录。
用户机器需安装 Microsoft WebView2 Runtime。此 Demo 的现代浏览器要求独立于 SDK 的兼容 MiniBlink 档位。
构建输出为 `out/scratch-x64/bin/Release/SouiScratch/`；迁移时复制整个输出目录。

Scratch 官方编辑器与完整离线资源超出通用小型页面的默认体积档位，此 Demo 使用专属 `deploy.mjs`：
每文件上限 32 MiB、全部 Web 与授权材料合计 256 MiB、最多 4096 文件。它拒绝目录链接与不安全路径，
在独立暂存目录复制和校验 SHA-256，保存 `.scratch-resources.json` 后发布整个 `resources/`。
SDK 与 Scratch 授权材料随目录保留；这项演示档位不修改 SDK 的通用默认资源限制。
部署会独占锁定目标 EXE，正在使用此输出的应用必须先关闭，避免运行中替换资源。

原生窗口在 UI STA 运行，使用 SDK 的异步 WebView2 初始化与退出观察器。
关闭期间保留 Host、Bridge、窗口和消息泵，完成浏览器退出后释放；不能通过直接结束进程代替正常关闭。
浏览器 profile 和日志保存在系统 Local AppData 的 `SouiScratch/`，`resources/` 仅用于部署文件。

页面可调用受当前文档会话授权的 `app.info`，获得实际 SDK 版本、后端与应用名称。
页面就绪应在编辑器完成初始化后调用 `bridge.probe.report`，以支持 `--smoke-test` 的真实就绪与退出检查。

项目导出通过 Demo 专属 `app.saveProject` 方法分块传入最多 16 MiB 的 `.sb3` 字节；原生端限制每个 Base64 分块不超过 24 KiB。
原生端检查大小、严格 Base64、顺序、会话和 ZIP 文件头，只在最后一块打开本机保存对话框。
网页不指定文件路径；保存取消明确返回 `cancelled`，已有目标文件只在临时文件完整写入后替换。
这是 Demo 导出范围，不是通用文件系统权限。

原生导出边界回归可从 SDK 根执行：

```powershell
cmake --build out/scratch-x64 --config Release --target SouiScratch_export_contract --parallel 2
out/scratch-x64/Release/SouiScratch_export_contract.exe
```

该目录的 `app.*` 动作是演示业务契约；资源模式属于本地 SDK 配置扩展。上游 Scratch 版本、资源授权及已执行验证见下文。

## 可以体验什么

右上角“性能实验”提供三个场景，每个场景轻/中/重三档：

| 场景 | 轻 | 中 | 重 |
|---|---:|---:|---:|
| 多角色运动、边缘反弹、旋转、造型切换 | 40 角色 | 120 角色 | 240 角色 |
| 克隆体粒子、运动、颜色效果 | 60 克隆 | 150 克隆 | 280 克隆 |
| 并发计算与绘制 | 20 角色 × 12 次/轮 | 60 角色 × 32 次/轮 | 120 角色 × 64 次/轮 |

选择场景和负载，点击“载入并运行”；等角色/克隆数量稳定后，点击“记录 30 秒”。
记录中可拖动积木、操作舞台、切换造型，观察负载下的交互。点击“停止全部”可停止脚本。
“导出 JSON 报告”使用本机保存对话框，包含 SDK/构建/Runtime 标识、场景种子与项目配方摘要、
负载配置、rAF FPS、P50/P95/P99 帧间隔、长帧、Long Tasks、活动线程和可用的 JS 堆指标。
不支持的指标记为 null。页面隐藏或最小化会中断并标记无效；不足 30 秒的手动停止记录不作为完整对比样本。

这是 Web 层负载与调度观察工具。rAF 不是屏幕实际呈现 FPS，也不表示鼠标到屏幕的物理延迟；
JS 堆不包含整个原生/GPU进程内存。计量保留最多 120002 个间隔，覆盖最长 120 秒、最高声明 1000 Hz 的记录；分位数在结束后一次计算，运行时显示 FPS 与计数。显式缩小容量造成滚动窗口时，报告标为诊断样本，不能通过完整窗口对照门禁。

报告的 `workload.vmSteps/drawCalls` 依赖本项目固定源码补丁的计数能力；原始 vendor 包不支持时为 `null`，不能用 rAF 次数冒充 VM 逻辑步或绘制次数。`targetsUpdates` 单独统计 VM 目标状态通知。可取得时，`graphics` 读取现有渲染上下文的 WebGL/ANGLE 标识；它不证明 GPU 实际呈现或物理显示帧率。
不同版本比较时固定机器、Runtime、显示刷新率、窗口尺寸和负载档位；先预热、再记录至少三次，
比较各次报告，不把本机单次数据当成性能提升结论。性能场景目标步进为 60 次/秒，普通学习示例保持默认模式。

真实 Scratch `15.3.0` 积木编辑器与执行引擎，默认语言为简体中文。
“小猫画圆”包含旗帜事件、循环、移动、旋转、等待和说话；“按键漫游”练习方向键事件。
可拖拽修改积木、运行/停止、切换造型与声音、从本地素材库选择角色与背景。
“自由创作”提供清空程序的默认角色。切换示例或打开作品前会在页面确认。

窗口上方“打开作品”和“保存 .sb3”支持不超过 16 MiB 的工程；保存通过原生另存为对话框，
页面不传入文件路径。上传分块为 16 KiB，Base64 与字段合计低于 Bridge 的 32 KiB params 限制，原生校验顺序、会话、Base64 和 ZIP 文件头，
写入完整临时文件后再替换用户选中的目标。原生文件按钮取代 Scratch 文件菜单，
以适配 SDK 默认禁止浏览器下载的策略。联网教程、云变量、在线社区与设备扩展不在演示范围内。

官方 npm 包与 SHA-512 固定于 `web/vendor-lock.json`。构建缓存位于 `.scratch-cache/`，
1,347 份角色/声音/背景等文件按官方文件名 MD5 校验，并记录 SHA-256 清单。
Web 构建无需安装 Scratch 的整棵开发依赖；需要当前 SDK Web 客户端已有 TypeScript 构建工具。
若缺少工具，从 SDK 根先执行 `npm --prefix sdk/web ci`。首次构建需网络获取固定包与素材；完成后日常使用本地资源。

Scratch 采用 AGPL-3.0-only，名称和角色图形的商标归 Scratch Foundation。
原始包中的 LICENSE、TRADEMARK、依赖声明和来源清单保留于运行目录的 `resources/scratch-legal/`
及 bundle 的 `.LICENSE.txt`。本演示不表示 Scratch 官方背书，公开再分发前需按实际组合代码完成许可交付。

Scratch 使用隔离 Paper 绘图沙盒；该 Demo 显式采用 SDK 的 `isolated_local_editor` 本地候选模式，
新增的公共配置及约束见 `docs/api.md` 和 [ADR 0142](../../docs/adr/0142-isolated-local-editor.md)，必须连同当前 SDK 重新构建；不能链接已发布的 0.6.0 二进制混用新增配置。

## 验证与范围

本次基于 `main` 的源码集成保留 SDK 版本入口 `VERSION`，没有生成或验证新的 0.6.0 发行包。编译、运行、体积门禁及 E/W/S 性能对照交由专人执行；下列历史结果不能作为当前源码通过证据。

浏览器功能检查：`node demos/scratch/verify-web.cjs`（需开发环境提供 Playwright）。
它覆盖程序驱动角色、工程序列化/重新载入、绘图沙盒及本地资源；报告在 `out/scratch-verification/`。
此检查不替代原生 Host、系统保存对话框或真实输入验证。
实际执行结果以该目录报告和 `verification.md` 为准。
