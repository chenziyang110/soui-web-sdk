# preview.11 同文档授权续期与独立接入预检

日期：2026-09-26。版本：`0.3.0-preview.11`。来源为 preview.10 提交 `21ca319894f45196ef022905f6431baf87c3ce5b` 加本切片；711 份编译输入 SHA-256 为 `a0c1d7900d39555508288d5459e65d3852e543cf7ec3879126bd6af59d2c2550`，见 [source-inputs.json](raw/source-inputs.json)。本记录的通过范围限于编译、契约、安装和生成项目；没有当前版本的有效 GUI、Win7 或实际长稳通过结论。

## 行为与证据

按 [ADR-0014](../../../docs/adr/0014-same-document-session-renewal.md)，应用在 bootstrap 前显式启用同文档续期。凭据采用系统随机源；30 分钟期限、25 分钟后轮换、旧凭据短暂重叠和同操作重取均有边界。任务、扫描及订阅归属稳定授权对象，撤销先于关闭清理。Web 保持普通业务调用同步发送，续期使用独立单飞通道，不自动重放业务操作。Starter 和 Workbench 两个页面入口接入。

Demo 另补齐订阅上限、外部事件监听器异常隔离，以及非 bootstrap 发行会话和 worker 的销毁顺序。这些原生回归覆盖实际扫描/CAD worker；预置媒体记录的归属测试不等于执行媒体引擎。

| 已执行项 | 实际范围 |
|---|---|
| x86/x64 Release 编译 | 完整 SDK、Starter、Workbench，以及 SDK-only 均退出 0；MSVC v143、Windows SDK 10.0.19041.0 |
| 每架构 10 项 CTest | contract、session-renewal、task-service、bridge-budget、task-store-capacity、tray-service、menu_service、global_shortcut、Demo public-task/session-renewal 全部实际运行，无跳过；授权续期含 14 组契约 |
| 独立 SDK 安装消费者 | 每架构 35 个安装文件逐字节核对；仓库外 `session-consumer` 仅链接安装后的 SDK，编译与运行退出 0，20 次模拟轮换保留结果/订阅且业务只调用一次 |
| Web 与交付 | Web SDK、Starter、Workbench 构建通过；最终 TAP 62 项、包回归 14 项、交付身份 TAP 7 项通过；五个 npm 锁文件全依赖审计均为 0 已知漏洞 |
| SDK 源包 | 首次打包 418 文件，包完整性与当前源码文件集合、字节身份匹配；没有因此关闭前版 rename `EPERM` 的未确证根因 |
| 新项目 | 由上述包生成仓库外 `RenewalStarter`，x86/x64 Release 独立编译与项目锁校验退出 0；源码与载荷见[独立校验摘要](raw/summary-generated.json)，尚未执行其 GUI |

主汇总见 [summary.json](raw/summary.json)。[只读校验器](raw/collect-summary.cjs)严格比较归档和当前构建身份、精确测试名和状态、安装文件集合、消费者链接、PE 架构及包内容；38 个拒绝负例与正式汇总均退出 0，见 [collector-results.json](raw/collector-results.json)。实际模拟时间不作为实际运行时长。Demo public-task 入口虽不创建业务窗口/WebView，仍初始化 COM STA，不能声称系统绝无隐藏窗口。

## 失败原样本和桌面等待

- 冻结前的 x64 诊断编译因未定义 `MAXULONG` 退出 1；改用 `std::numeric_limits<ULONG>::max()` 后诊断重试成功。诊断记录不充当冻结输入的正式构建证据。
- Web 设计和复核期间的失败、修正及最终测试日志均保留，不能描述为所有尝试一次通过。主汇总列出非零退出样本。
- 桌面空闲后曾运行 x86 窗口线程停滞探针。其 stdout 显示成功，但原监督器取得的 `exitCode=null`，因此[首次桌面记录](raw/desktop-attempt1/x86-window-thread-timeout.result.json)不计为通过。已将外部监督器改为持有 .NET Process、读取原始退出码，原记录未覆盖；归档的 `run-desktop.ps1` 是修正后的待运行脚本，不是首次执行脚本。
- 随后其他 QEMU 再次启动，[门禁记录](raw/desktop-gate-deferred.json)保存此事实。没有继续目录、托盘、驻留或应用 GUI 测试，也没有终止其他虚拟机。两架构当前安装 SDK 的托盘消费者已另行编译，运行仍待空闲。

## 复现与边界

从交付根按架构串行构建并筛选当前契约：

```powershell
powershell.exe -NoProfile -File tools/Build.ps1 -Architecture x86 -Configuration Release
powershell.exe -NoProfile -File tools/Build.ps1 -SdkOnly -Architecture x86 -Configuration Release
ctest --test-dir out/x86 -C Release --output-on-failure -V --timeout 35 -R '^(sdk.native.(contract|session-renewal|task-service|bridge-budget|task-store-capacity|tray-service|menu_service|global_shortcut)|demo.native.(public-task|session-renewal))$'
node --test --test-reporter=tap tests/web/sdk-client.test.mjs tests/web/native-resolvers.test.mjs tests/web/session-renewal.test.mjs
```

x64 替换架构；外部安装消费者见 [说明](../../../tests/session-consumer/README.md)。生成项目使用 `tools/New-Project.ps1` 和该项目自己的 `tools/Build.ps1`，不得引用原构建目录。`raw/` 保存执行参数、UTC 时间、退出码、日志、构建/安装/载荷身份和校验脚本；[raw-manifest.json](raw-manifest.json)绑定归档字节。大体积 DLL/LIB/EXE、安装树、SDK 包和生成项目保留在原外部证据目录，未重复提交；原路径是历史来源，不是可移植构建配置。

尚缺：实际超过 30 分钟及八小时的续期与任务/订阅连续性、休眠/阻塞后的到期行为；当前版窗口/对话框/托盘/菜单/快捷键与驻留；Win7 SP1 x86；V02 中文 IME、缩放、滚动与公共任务并行；Debug、当前最终完整源码搬移及底层重编、隔离资源趋势、同机 Electron 呈现与价值比较。历史受干扰八小时及其他 preview 的通过不迁移成本版通过。

`sourceDeliveryVerified=false`、`benchmarkValid=false`、`releaseReady=false`。本预检不批准生产使用。
