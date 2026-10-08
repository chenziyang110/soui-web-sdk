# WebView2 实验消费者与生命周期回归

日期：2026-10-03（Asia/Shanghai）。原始证据保留真实 UTC 时间。此轮为本地实验验证，未发布；`releaseReady=false`。设计见 [ADR-0013](../adr/0013-experimental-webview2-consumer.md)，代码与复验入口见 [实验工程](../../experiments/webview2-consumer/README.md)。

## 实现与已验证结果

新增独立 CMake 消费者，通过安装后的 `Soui::Sdk` 使用 preview.9 公共 Host、C++ Bridge 和 Web 连接器。WebView2 在 UI STA 异步准备后挂载；业务进入有界后台队列，导航撤销旧会话，回送核对文档代标识。它未加入默认模板、Workbench 或生成项目的固定 SDK 包。

| 检查 | 实际结果 |
|---|---|
| 独立 Release 构建 | x86/x64 均通过；核对安装库、公共头文件、固定开发依赖、Web 输出和最终执行载荷 |
| WebView2 真实重载 | 每架构10轮，退出0；每轮业务就绪、独立文档会话，共20轮 |
| WebView2 真实销毁重建 | 每架构10轮，退出0；销毁并重建 Host、适配器、Controller 与 Bridge，共20轮；复用应用父窗口 |
| 每个 WebView2 新文档 | 真实 bootstrap→分页 discover→16个往返；错误传播及恢复；120ms原生延时期间页面计时器推进；业务就绪报告 |
| 现代页面探针 | ES Modules、CSS Grid、TextEncoder 多语言编码检查通过；不是完整浏览器兼容矩阵 |
| Miniblink 回归 | x86/x64 各运行 reload、recreate、multiwindow、bridge，每场景10轮；8次运行均退出0，共80轮；新文档与错误恢复后核对分页发现 |
| 针对此轮的协议与监督器测试 | 27通过、0失败、1跳过；包括文档隔离、重复ID/容量、卸载、超时/晚到回送、证据拒绝与系统时钟跳变 |
| 标准 x64 单元入口 | SDK包14/14、交付/ABI等12/12、Web/验收74通过/1跳过、原生 CTest 7/7；`-UnitOnly` 不包含模板启动 |
| 固定 SDK 源码核对 | 当前 SDK 内容仍匹配本地 `dist/sdk/0.3.0-preview.9`；实验独立记录来源，不修改同版本固定包 |

跳过项需要额外的 x86 调试器，不能计为通过。本轮两架构的独立原生消费者确实重新编译并实际启动；标准单元入口复用原有 x64 原生测试产物。前一轮模板、Workbench、仓库外生成项目的证据见 [基础验证](agent-foundation.md)，不冒充本轮重新执行。

## 运行环境与身份

开发机为 Windows `10.0.26200`，主机 x64；MSVC `19.44.35226.0`、CMake `4.2.3`、Windows SDK `10.0.19041.0`、Node `24.14.0`。官方 WebView2 开发包固定为 `1.0.4258.31`，恢复时验证 SHA-256；实际系统 Runtime 报告 `154.0.4258.48`。

| 架构 | WebView2 构建ID | 本地构建目录 |
|---|---|---|
| x64 | `0530a6aa-6a0f-418f-9503-3459b95ffc94` | `out/webview2/17f57773-1e9c-4152-9fa6-716a1f2931c4` |
| x86 | `93bd4af4-6e3b-4500-b6a7-f49e6f38454c` | `out/webview2/8a06df78-5981-4aff-8c7a-5992bb77547c` |

两架构使用相同 SDK 构建输入摘要 `d48bfb5b8901e5bbf7ce1b99928a26856cc26dc8746efa40cbbd4c998c46b868`、实验源码摘要 `27b4f76b3d4243d06a04045f2812391c14138d791b61e2e4565b4040aafc20fd`，合并输入摘要为 `0e4ed67eba0fcaff7bcfbb6ad6d17d17e7953baf2dc6b09daf714c0f639dc4fa`。x64载荷摘要为 `a8296cb3424dcae8a3917181c68c75b2324364f05020b90d695a8bc141e9097d`，x86为 `d94c7790bdea1e02b112659299a70c7a0339277bbe48d5689b47619ce6a46461`。摘要绑定实际文件集合；修改构建输入或输出必须重新构建。

| 架构 | EXE字节数 | 应用载荷字节数（含应用本地CRT、页面、Web SDK和依赖声明） |
|---|---:|---:|
| x64 | 550,400 | 4,227,075 |
| x86 | 482,304 | 3,960,531 |

这些数字不含系统 WebView2 Runtime、运行后用户数据和缓存，不是完整安装体积，也不是与 Electron 的等功能体积或性能对照。

## 失败与修正

首次编译暴露 SDK 的旧系统宏隐藏 `BCryptHash` 和嵌套 lambda 捕获类型问题，改用可见的分步哈希接口并显式捕获后通过。两次编译失败日志保留在证据目录。

初次运行在业务就绪前失败，原生程序退出1；监督器记录 `timedOut=false`，不能写成监督器超时。增加有界页面状态诊断后，定位到原生延时返回120，但页面计时器 `ticks=0`、页面 `visibility=hidden`。启动监督器隐藏控制台时，首次 `ShowWindow` 继承隐藏启动状态，应用窗口仍不可见。修正显式窗口显示，并在启动证据中要求 `windowVisible=true` 后，原有“延时期间计时器继续推进”断言通过；没有降低断言或禁用后台节流。

另外，Miniblink 生命周期监督器的步骤/无进展期限改用单调时间，UTC 只用于诊断。时钟前跳、回拨均有回归测试；此改变避免监督器因系统校时误判，不构成历史 Miniblink 挂载超时已定位或已解决的结论。

原始事件和结果按字节保存，失败记录保留为失败；Git属性保留事件文件的原始换行。便于分享的编译与单元日志将工作区绝对根路径替换为 `<SDK_ROOT>`，统一 LF；编译失败日志另清理行尾空格和末尾空行，没有改变错误或计数。详见 [证据目录](../../verification/production-validation/webview2-experiment-2026-10-03/README.md)。

## 复验与未覆盖范围

从交付根目录执行，测试参数中的目录须替换为本次构建实际输出；两架构按顺序运行：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File experiments/webview2-consumer/Build.ps1 -Architecture x64
powershell.exe -NoProfile -ExecutionPolicy Bypass -File experiments/webview2-consumer/Test.ps1 -BuildDirectory out/webview2/<实际目录> -Scenario all -Cycles 10
powershell.exe -NoProfile -ExecutionPolicy Bypass -File experiments/webview2-consumer/Build.ps1 -Architecture x86
powershell.exe -NoProfile -ExecutionPolicy Bypass -File experiments/webview2-consumer/Test.ps1 -BuildDirectory out/webview2/<实际目录> -Scenario all -Cycles 10
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/Test-Acceptance.ps1 -Architecture x64 -Scenario all -Cycles 10
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/Test-Acceptance.ps1 -Architecture x86 -Scenario all -Cycles 10
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/Test.ps1 -Architecture x64 -Configuration Release -UnitOnly
```

实验必须在可交互 Windows 会话和实际系统 Runtime 下运行。真实生命周期结果包含父进程退出与文档会话检查；来源/卸载/超时等对抗传输用例主要由受控测试覆盖，不等同于真实浏览器全安全验证。监督器仅终止自己启动的直接子进程，尚未验证浏览器整进程树退出收敛。

两后端的 `resourceGate` 都为 `not-evaluated`。Miniblink 保留了进程资源样本，其中短循环也能观察到部分资源增长；此轮未定位、消除或对增长给出生产通过结论。WebView2 不含整进程树/GPU资源采样。尚未完成1,000轮、8小时混合负载、输入法/DPI/多屏/无障碍、旧系统/离线部署矩阵、公开长任务全事件链、稳定 Agent 界面语义、Electron 等负载对照、签名与正式发布门槛。R01/R02 按 [建设路线](../roadmap/agent-gui-framework.md) 继续验收。
