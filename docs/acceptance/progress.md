# SDK 生产验证执行记录

## 2026-09-28 Win7 V02 v6 客体启动中断

[v6 原始记录](../../verification/production-validation/win7-sp1-x86-v02-interaction-v6-interrupted-2026-09-28/README.md)绑定当前 Demo 的正式 x86 Release 构建，修正了上一份未执行冻结输入的越界滚轮坐标；新 13 文件输入冻结与只读验证通过。我方 QEMU 启动后，外部 QEMU 在客体命令发送前出现，实时门禁仅停我方客体。原始驱动退出 1、QEMU 退出 -1，串口 0 字节，无命令发送；基础盘和冻结输入前后身份一致。17 份非专有快照原件已逐字节归档。此次不能判定 Win7 应用兼容性，真实中文 IME、30 秒公共任务并行及隔离性能仍未通过，`win7CurrentV02Verified=false`、`benchmarkValid=false`、`releaseReady=false`。

## 2026-09-28 当前 V02 Win7 SP1 x86 v4 功能探索

[第二次 v4 客体原始记录](../../verification/production-validation/win7-sp1-x86-v02-interaction-v4-attempt2-2026-09-28/README.md)绑定当前 Demo 正式 x86 Release 构建和已冻结的 13 文件输入。真实 Win7 桌面确认后只发一次命令；96 帧串口通过严格校验，客体 6.1.7601 SP1 32 位/PowerShell 2，资源及 legal 前后身份一致。V02 万行数据页出现，原生探针发送 29 次输入，缩放后窗口边界变化，应用原生退出 0、正常关闭，客体及基础盘检查退出 0。外部 QEMU 在最后一次五秒门禁采样后 71 毫秒、我方客体退出前启动；串口写入早于外部负载，但整段宿主隔离不成立，资源/时延不能作隔离结论。中文 IME 组合、公共任务确实启动及并行响应尚未证明，`win7CurrentV02Verified=false`、`benchmarkValid=false`、`releaseReady=false`。

## 2026-09-28 当前 V02 Win7 SP1 x86 v4 交互的隔离中断

[v4 原始记录](../../verification/production-validation/win7-sp1-x86-v02-interaction-v4-interrupted-2026-09-28/README.md)绑定当前 Demo 的正式 x86 Release 构建：x86 探针真实编译，静态 MSVC 运行库命令已核验；13 文件输入冻结且验证。客体进入真实 Win7 桌面后只收到一次命令，但串口没有输出。另一台 QEMU 在命令执行期间启动，门禁只终止我方客体；我方驱动原始退出 1、QEMU 退出 -1，基础盘和冻结输入复核正常。20 份复制原件已核对外部原件，客体临时磁盘快照只留外部。此次既不证明应用通过，也不证明应用失败；中文 IME、缩放和任务并行仍未验收。新客体尝试必须用新编号，`win7CurrentV02Verified=false`、`releaseReady=false`。

## 2026-09-28 当前 V02 Win7 SP1 x86 交互第二次隔离中断

[第二次新输入尝试](../../verification/production-validation/win7-sp1-x86-v02-interaction-gate-interrupted-attempt2-2026-09-28/README.md)使用不变的 13 文件冻结输入和正式 x86 Release 构建。我方客体还在 Win7 欢迎界面，尚未发送应用命令，另一台 QEMU 随后启动；门禁只停止我方客体。驱动退出 1、QEMU 退出 -1、串口 0 字节，基础盘和冻结输入前后不变。19 份复制原件含欢迎界面截图与关闭后的 monitor 错误；两份可能包含客体磁盘片段的临时快照只留在外部。此样本不证明应用兼容性成败；下次用新的尝试编号，`win7CurrentV02Verified=false`、`releaseReady=false`。

同一轮归档后的源码清单刷新也在执行时遇到新外部 QEMU，门禁停止我方清单子进程并记录原始退出 1；原 `source-manifest.json` 未变，四份失败原件单独复制复核。该次既不算清单生成，也不算完整源码验证通过，后续空闲时须用新尝试编号重做。

对未执行的 v3 客体脚本和冻结的 `V02InputProbe.exe` 源码作静态核查，发现脚本会调用探针不支持的 `wheel-up` 和 `key-space` 动作；且拼音动作自身已经发送空格提交。即使客体启动，v3 也无法完成预定的 30 项输入序列。两次已冻结输入和中断原件保持不变，停止将 v3 作为完整交互验收候选；另建 v4 输入与探针，重新冻结、编译和验证后再运行客体。此发现属于测试夹具缺陷，当前没有应用兼容性结论。

新一轮空闲窗口中，完整源码清单生成原始退出 0、覆盖 36,840 个文件；随后的独立验证遇到新出现的外部 QEMU，门禁只停我方验证子进程，原始退出 1。八份原始命令/门禁记录已单独复制和核查；清单虽写入工作树，尚无本轮完整验证通过结论，不能据此放行源码交付。v4 夹具源码静态检查已退出 0，仍待真实 x86 编译、全新输入冻结及客体运行。

之后一次空闲时的清单验证原始退出 1，错误为 `Source file set differs from manifest`：清单创建后追加了本轮原始记录，暂存文件集比清单多十项。该失败和空闲门禁的四份原件已另存并复核，不能解释为验证成功。待此次全部原件和文档暂存后，从最终暂存输入重新生成、验证清单，再提交。

再一次刷新先被新外部 QEMU 保护性中断；空闲后生成 36,856 项清单并独立验证，两个驱动原始退出均为 0。该成功只对应当时的暂存输入，随后本说明更新使文件集再次变化，提交前须重新生成、验证最终暂存集。v4 交互探针的静态验证已通过，并改为静态链接 MSVC 运行库以避免 Win7 缺失动态运行库；真实编译、客体输入和中文 IME/缩放/任务并行仍未通过。

本次提交的源码清单仍须对应最终暂存输入重新生成和独立验证，不跨输入集复用成功结果；各次原始命令保留在外部验证目录。清单验证不代表 v4 探针已编译，也不代表 Win7 交互验收通过。

## 2026-09-28 当前 V02 Win7 SP1 x86 IME/缩放/任务探索的隔离中断

[第一次新输入尝试](../../verification/production-validation/win7-sp1-x86-v02-interaction-gate-interrupted-2026-09-28/README.md)已绑定当前 718 文件 Demo 源码、正式 x86 Release 构建和 13 文件冻结输入。我方客体启动后，另一台 QEMU 在客体命令发送前出现；宿主门禁只停止我方虚拟机。驱动原始退出 1、QEMU 退出 -1，串口 0 字节，无应用或中文输入/缩放/任务并行结果。47 份复制原件和严格中断复核保留；两份 QEMU 临时快照只留在外部原件，不进入公开仓库。后续须在环境空闲时用新尝试编号执行，`win7CurrentV02Verified=false`、`releaseReady=false`。

## 2026-09-28 当前 V02 Win7 SP1 x86 滚轮前后截图

[新客体原件](../../verification/production-validation/win7-sp1-x86-v02-layout-wheel-before-after-2026-09-28/README.md)绑定当前 718 文件 Demo 源码、正式 x86 Release 构建和新 13 文件只读输入。Win7 SP1 x86 普通可见 Workbench 在 1024×768 快照桌面中，从万条列表顶部显示移动到下方的中文输入状态、动效及公共任务区；前后原始 PPM/PNG 时间线、十二次绑定进程及自然前台的系统滚轮、61 帧串口均保留。应用原生退出 0、自然关窗，载荷及法律声明前后匹配，宿主 60 次采样未见外部桌面负载。客体与宿主时钟存在偏差，不据此计算精确输入延迟或逐帧因果。中文 IME 组合、窗口缩放及 30 秒公共任务并行仍未在 Win7 执行，`win7CurrentV02Verified=false`、`releaseReady=false`。两份应用 ZIP 原字节仍在外部冻结输入，仓库不能独立重建完整冻结输入。

## 2026-09-28 当前 V02 Win7 SP1 x86 原生滚轮探索

[八次系统滚轮的客体原件](../../verification/production-validation/win7-sp1-x86-v02-layout-wheel-2026-09-28/README.md)绑定当前 718 文件 Demo 源码、正式 x86 Release 构建和新 13 文件冻结输入。Win7 SP1 x86 的 1024×768 快照桌面中，普通 React Workbench 以 950×640 实际窗口显示 V02 页面；原生输入探针绑定进程创建身份、PID 和 HWND，在自然前台发送八次真实系统滚轮。应用原生退出 0、自然关闭，50 帧完整串口、运行载荷及法律声明前后核对通过，宿主 56 次采样未见外部桌面负载。29 帧时间线截图虽显示中文输入、动画和公共任务区域，首张已渲染截图晚于滚轮，缺少滚前基线，**不能证明页面实际滚动**；中文 IME、缩放和任务并行也未执行。仓库没有归档两份应用 ZIP 的字节，仅对其外部原件和先前已提交的大小/散列元数据交叉核对，不能从仓库独立重建完整冻结输入。首次归档校验因误以为旧归档含 ZIP 字节而退出 1，失败原件保留；修正后才归档。`win7CurrentV02Verified=false`、`releaseReady=false`。

首次更新完整源码清单时，外部 QEMU 在执行中启动，门禁仅中断我方清单进程并保留原始退出 1 与负载采样；该次不算清单验证通过。随后空闲时第二次更新和校验完整源码清单均原始退出 0，覆盖 36594 文件。客体滚轮试验已在外部负载出现前独立完成，两个时间段不合并为隔离性能结论。

## 2026-09-28 当前锁文件的提交前依赖公告核查

[六次原始核查及锁文件散列](../../verification/production-validation/preview15-precommit-npm-audit-2026-09-28/README.md)覆盖 SDK Web、Starter Web、Workbench Web 的生产依赖和完整依赖：每次 `npm audit` 原始退出 0、stderr 为空、报告漏洞数 0。此结果只反映执行时 npm 公告服务答复，不覆盖原生内核、依赖再分发条款或未来公告；授权及生产门槛继续未关闭。

## 2026-09-28 当前 V02 Win7 可见页面显示模式探索

[显示模式探针与冻结输入](../../verification/production-validation/win7-sp1-x86-v02-display-preparation-2026-09-28/README.md)绑定当前 718 文件源码和正式 x86 Release 构建。Win7 客体预检桌面为 800×600，而 Workbench 的 `--height` 最小被限制为 640；因此本轮准备了静态 x86 探针，仅在快照客体临时尝试 1024×768，再观察正常可见窗口。13 文件冻结输入、探针编译及直接依赖核查已通过；准备本身不证明客体显示模式可用。

[第一次可见探索原始记录](../../verification/production-validation/win7-sp1-x86-v02-display-interrupted-2026-09-28/README.md)显示，我方 QEMU 启动后只到 Win7 欢迎界面，另一台独立 QEMU 随后出现。宿主门禁只终止我方客体；驱动原始退出 1、QEMU 退出 -1，基础盘和冻结输入前后不变。客体命令未发送，串口 0 字节，显示探针和 Workbench 均未执行。该样本不能判定 Win7 可见页面或 V02 交互成败；下次空闲需使用新尝试编号，`win7CurrentV02Verified=false`、`releaseReady=false`。

[第二次启动前拒绝原件](../../verification/production-validation/win7-sp1-x86-v02-display-prelaunch-refusal-2026-09-28/README.md)表明，短暂空闲后再次出现外部桌面负载，驱动在创建客体目录和启动 QEMU 前原始退出 1。该轮没有自身虚拟机、串口、显示探针或应用，不能计为客体兼容失败；后续使用第三个独立尝试编号。

[第三次客体原样本](../../verification/production-validation/win7-sp1-x86-v02-display-failure-2026-09-28/README.md)证明 Win7 SP1 x86 快照桌面可临时切换至 1024×768，传输、完整载荷前置核查和宿主隔离门禁通过；但普通 Workbench 启动后第 10 秒未找到可见的无 owner 顶层窗口，客体 `run-exitcode.txt=1`、严格成功校验退出 1。没有应用原生退出码、自然关闭或 V02 页面可见证据；我方 QEMU/外层驱动退出 0 只表明虚拟机流程正常结束。保留 24 帧串口与截图，不把此失败覆盖或解释成生产通过；需以新冻结输入查明可见窗口出现时序。

[新夹具冻结与首次中断](../../verification/production-validation/win7-sp1-x86-v02-display-v2-interrupted-2026-09-28/README.md)保留原 60 秒应用期限，拟在 10–40 秒持续观察窗口并捕获探针错误；13 文件新输入已冻结验证。其第一次客体启动后外部 QEMU 出现，门禁只停止我方虚拟机，命令未发送、串口 0 字节；因此尚无新夹具的客体执行结论，下一次使用独立尝试编号。

[第二次 v2 客体原始记录](../../verification/production-validation/win7-sp1-x86-v02-visible-page-2026-09-28/README.md)在 Win7 SP1 x86 1024×768 快照桌面执行了普通可见 Workbench。第 10、15、20、25、30 秒的窗口探测均未发现目标，第 35 秒探测成功；截图可见 V02 数据交互页、万条载入指标及虚拟列表。应用在原 60 秒期限内原生退出 0，窗口正常关闭，完整载荷与法律声明前后核对通过。此结果解释了先前第 10 秒单次探测的局限，只证明当前构建在该分辨率下页面可见；真实滚动、中文 IME 组合、缩放及公共任务并行仍未执行，`win7CurrentV02Verified=false`、`releaseReady=false`。

## 2026-09-28 当前 V02 Win7 SP1 x86 Workbench 短测

[当前构建的客体短测原始记录](../../verification/production-validation/win7-sp1-x86-v02-current-app-smoke-2026-09-28/README.md)绑定 718 文件 Demo 源码与正式 x86 Release 构建，在 Win7 SP1 x86 用 `--react --page=data --width=700 --height=500` 启动 Workbench 的 60 秒短测入口。真实客体中应用原生退出 0，日志按序记录 `application-ready`、Bridge 就绪、adapter 卸载、host 完整关闭和窗口销毁；载荷 71/475 文件及各 5 份法律声明前后均与冻结清单逐字节匹配。29 帧串口严格校验，62 次宿主门禁未见外部桌面负载，QEMU/驱动退出 0，基础盘及 11 文件只读输入冻结前后不变。78 份原始文件独立归档，包括随后一次源码清单生成因新出现的外部 QEMU 中断的原始退出码；该中断不改变先前客体短测结论。短测日志的 `--page=data` 参数不证明 V02 页面在屏幕可见，也未执行滚动、中文 IME、缩放和长任务并行；`win7CurrentV02Verified=false`、`releaseReady=false`。

## 2026-09-27 当前 V02 Win7 SP1 x86 客体环境预检

[冻结输入与客体原始记录](../../verification/production-validation/win7-sp1-x86-v02-current-preflight-2026-09-27/README.md)完成当前 718 文件源码、正式 x86 Release 构建绑定的 563 文件预检输入冻结；宿主观察桌面后仅发一次命令。客体报告 6.1.7601/SP1/32 位/PowerShell 2，静态输入探针实际加载至自身参数校验；9 帧串口严格校验、115 次宿主门禁、基础盘与冻结输入前后检查均通过，QEMU/驱动退出 0。客体显示为 800×600，中文键盘布局已预载但真实 IME 组合未验证。此次**没有启动 Workbench 或运行 V02 交互**，Win7 当前版本、DPI 多屏及生产支持继续未通过；先前一次源码清单刷新受外部 QEMU 中断的原样本保留。

## 2026-09-27 当前 V02 Win7 x86 客体输入前置准备

[当前输入的载荷和静态探针原始记录](../../verification/production-validation/win7-sp1-x86-v02-current-preparation-2026-09-27/README.md)绑定 718 文件源码与正式 x86 Release 构建，Starter/Workbench 分别 71/475 文件并各含 5 份法律声明，完整载荷和 ZIP 创建/测试通过；静态编译的 x86 输入探针只直接导入 USER32/KERNEL32，配置/编译及 `dumpbin` 原始退出 0，35 份宿主原始记录归档。该准备阶段尚未运行客体；后续环境预检见上节，当前应用 V02 真实交互仍未通过。旧 717 文件 Demo 的 Win7 短测不能继承。

## 2026-09-27 当前 V02 x86 真实交互短测

[当前正式构建的 x86 可见交互原始记录](../../verification/production-validation/v02-current-visible-x86-2026-09-27/README.md)核对同一自然前台窗口的 10,000 行页面、真实系统滚轮、30 秒公共 SDK 任务期间拼音输入法汉字“中文”提交、1100×720→1180×780 边框缩放及万行列表内滚动。内层滚轮发生在任务启动后 15,568 ms，列表首行 1→4；`WM_CLOSE` 后窗口销毁、原生退出 0。驱动/应用及运行载荷前后身份一致，89 份原始记录归档。结合此前 x64 单轮，当前版本在本机双架构各有一次可见交互短测；Win7、长期资源与响应、DPI 多屏、Electron 对照和生产支持仍未通过。

## 2026-09-27 当前 V02 x64 真实交互短测

[当前正式构建的 x64 可见交互原始记录](../../verification/production-validation/v02-current-visible-x64-2026-09-27/README.md)核对同一自然前台窗口的 10,000 行页面、真实系统滚轮、30 秒公共 SDK 任务期间的拼音输入法汉字“中文”提交、1100×720→1180×780 边框缩放及万行列表内滚动。内层滚轮发生在启动任务后 14,651 ms，列表首行 1→4；窗口 `WM_CLOSE` 后自然退出 0。驱动、原生进程和运行载荷前后身份均核对，88 份原始记录归档。该结果只证明 x64 当前候选的一次短时真实交互；x86 单轮见上节，Win7、长期资源与响应、Electron 对照和生产支持仍未通过。

[外部负载中断原始样本](../../verification/production-validation/v02-current-visible-interruptions-2026-09-27/README.md)保留 x64 一次启动后中断，x86 一次启动前拒绝及两次在任务开始后因外部 QEMU 进程出现而清理自有应用。x86 第三次已发送真实拼音按键，但没有汉字提交的证据，缩放、内层列表滚动和自然退出也未完成；其中驱动清理导致的非零退出不得解释为隔离应用故障。四次尝试不增加 x86 支持范围。

## 2026-09-27 当前 V02 x64 可见交互局部证据

[第二轮可见交互原始记录](../../verification/production-validation/v02-current-visible-partial-2026-09-27/README.md)显示当前正式 Release 应用在自然前台的 V02 数据页实际接收外层页面滚轮、中文输入法组合与边框拖拽。第二次 30 秒公共任务运行期间，截图同时可见任务进度 40%、组合事件 17 次及内容区从 1100×720 变为 1180×780。首次输入已得到汉字“中文”，但发生在前一个任务完成后；同任务汉字提交和万行列表内层滚动尚未证明。自定四分钟监督期限到达后只清理自己的应用，缺少自然关闭；该轮不能作为完整 V02 或生产通过。此前一次新尝试因外部 QEMU 启动前门禁拒绝，原件一并保留。

## 2026-09-27 当前 V02 x64 可见页面首轮中断

[首轮原始记录](../../verification/production-validation/v02-current-visible-interrupted-2026-09-27/README.md)绑定当前正式 Release Workbench，真实启动 `--react --page=data` 并通过目标进程、窗口和自然前台核对。截图可见 10,000 行已载入，系统滚轮输入后的列表首行由第 1 行变为第 4 行。随后外部 QEMU 启动，监督器仅清理自己的应用，外层驱动退出 1；本轮受干扰且未执行中文 IME、30 秒任务并行、缩放或自然关闭，不计完整 V02 通过。后续另用新 attempt，旧原件不覆盖。

## 2026-09-27 当前 V02 输入探针双架构拒绝路径

[x64/x86 原始结果](../../verification/production-validation/v02-current-input-probe-rejections-2026-09-27/README.md)显示已编译探针各自真实执行五项错误输入，原生均退出 1，拒绝原因与预期一致，不存在意外结果文件，预存文件前后散列不变。一次性等待器在其他 QEMU 运行时仅查询负载，连续空闲后串行执行两个架构，外层驱动均退出 0。该结果关闭错误目标/输出的局部拒绝路径，不证明任何正向 GUI 或中文输入。

## 2026-09-27 当前 V02 输入探针双架构编译

[原始构建和首次拒绝路径记录](../../verification/production-validation/v02-current-input-probe-build-2026-09-27/README.md)显示已在其他 QEMU 退出后串行编译冻结的 x64/x86 操作系统输入探针，四个配置/编译阶段原始退出 0、EXE 散列已绑定。首轮 x64 拒绝路径测试因测试夹具门禁文件重名在探针启动前退出 1，原样本保留；修正版运行结果见上节。新外部 QEMU 随后启动，未并行做桌面输入。探针编译不证明 `--page=data` 真实滚动、中文 IME、窗口缩放或公共任务并行，生产准入仍为 false。

## 2026-09-27 当前 V02 应用双架构本机短测

[x86/x64 三入口原始记录](../../verification/production-validation/v02-current-desktop-smoke-2026-09-27/README.md)已绑定当前 718 文件源码、两份正式 Release 构建 ID 和完整载荷：Starter 默认、Workbench 默认/React 在两个架构各原生退出 0，就绪及完整关闭日志齐全，前/中/后负载门禁为空。一次性等待器于宿主空闲后串行执行，另一台 QEMU 在全部短测结束后才出现。短测使用隐藏窗口且未进入 V02 数据页，真实滚动、中文 IME、缩放、公共任务并行和 Win7 x86 仍未验证；生产准入保持 false。

## 2026-09-27 当前 V02 GUI 输入验收准备

[输入探针和 GUI 预检](../../verification/production-validation/v02-current-gui-preparation-2026-09-27/README.md)已保存宿主中文输入法、当前双架构应用身份、V02 页面观察点，以及只对已核对 PID/HWND 自然前台发送 Win32 输入的辅助程序源码。准备时独立 QEMU 仍运行，探针未编译、当前 GUI 未启动，中文 IME 组合和任务并行没有获得通过证据。`releaseReady=false`。

## 2026-09-27 当前内核直接依赖复核

[当前 x86/x64 内核导入记录](../../verification/production-validation/preview15-current-kernel-imports-2026-09-27/README.md)绑定待审二进制清单中的精确 SHA-256，`dumpbin /dependents` 均原始退出 0，分别列出 17/19 个直接 DLL 依赖。它不揭示静态链接成员或运行时加载，更不能替代再分发条件审核；依赖授权仍为 `unverified`，生产准入未关闭。

## 2026-09-27 V02 持续任务双架构正式应用构建

[原始构建记录](../../verification/production-validation/v02-continuous-task-official-build-2026-09-27/README.md)显示当前 718 文件源码的正式 `tools/Build.ps1` x64 Release（含完整 Web）与随后 x86 Release（复用已构建 Web）及各自外层驱动原始退出码均为 0。两次构建 ID 分别为 `5ddcec93-d244-456c-877b-1bdeabee568d` 和 `f0c38056-058a-456e-be99-17446127f7ec`，各自前后源码 SHA-256 一致，两个架构的 Starter/Workbench 完整运行载荷各有独立散列。两次前后负载门禁均为空；x64 结束后三秒才有其他 QEMU 启动，x86 在它退出后执行。当前 Demo 真实 GUI/Win7 V02 验收仍待执行；这两项只证明正式构建，不放行生产门槛。

同一载荷清单的未压缩目录字节和为 x64 Starter/Workbench `42,046,881` / `53,059,977`，x86 为 `28,584,049` / `39,522,905`。这不是压缩包大小，当前尚缺同口径 Electron 对照，不能据此计算 40% 候选目标是否达到。

## 2026-09-27 V02 持续任务两架构原生回归

[本次原始记录](../../verification/production-validation/v02-continuous-task-native-2026-09-27/README.md)保存 x64 两次未通过的原样本、修正测试夹具后的 x64/x86 真实 adapter/Bridge 双测试退出 0，以及 Web TypeScript 类型检查退出 0。第二次失败来自同一适配器重复可信 bootstrap，测试改用具备有效权限的另一会话验证任务归属；SDK Core 和 Demo 运行实现没有因此修改。当前输入 718 文件 SHA-256 `15152ef430d70b8cfbb0682c2a5adb02cdbf5ca64c64e6e93252be9b0708e46d`。在该阶段，新正式应用构建、真实 V02 交互与 Win7 x86 尚未执行；后续 x64 构建见上节。旧应用短测不能继承，生产门槛未关闭。

## 2026-09-27 V02 持续任务首次准备（历史状态）

[V02 持续任务准备](../../verification/production-validation/v02-continuous-task-preparation-2026-09-27/README.md)保留原快速任务，新增 Demo 自有的 30 秒原生计时任务，通过公开任务接口观察进度、查询与取消。先编译的新 x64 回归在旧实现上实际退出 1，确认缺少该任务；源码仍匹配冻结 SDK Core，Demo 生产依赖审计退出 0、报告漏洞 0。

修复后的两架构回归、新应用构建、Web 检查和桌面验收尚未执行。其他 QEMU 运行时未启动并行编译或桌面负载。既有 preview.15 Win7 与本机短测绑定旧应用输入，不能自动继承给新 Demo；当前 V02 与生产门槛仍未通过。

## 2026-09-27 真实续期构建门禁与 V02 输入核查

[外部构建门禁修正](../../verification/production-validation/preview15-renewal-build-gate-2026-09-27/README.md)复现了 CIM 查询非终止错误可能返回 0、误判桌面空闲的问题。原驱动六项中三项失败，修正后六项通过；实际构建命令又因仍有其他 QEMU 返回 2，没有启动编译。23 份原始记录保留，SDK 内容和已验客体输入不变，真实续期仍待执行。

V02 源码核查发现同页公开任务固定为 200 步，每步请求等待 5 ms，只有约一秒的名义等待量，实际时长取决于调度。它不足以稳定安排连续滚动、中文候选组合和缩放全过程的并发验收。下一步需为 Demo 增加明确有界、可取消的持续任务，保留原快速任务，随后绑定当前应用构建分别验证真实输入。现有 `data-v02-ready`、滚动位置、组合事件计数和 RAF 诊断可辅助观察，但不能把 Unicode 注入当 IME、窗口尺寸变化当多屏 DPI 或 RAF 当物理呈现。该核查不是 V02 桌面通过证据。

## 2026-09-27 preview.15 Win7 当前三入口短测完成

[Win7 SP1 x86 attempt3](../../verification/production-validation/win7-sp1-x86-qemu-preview15-apps-attempt3-2026-09-27/README.md)在同一 v3 冻结输入和原 60 秒期限下完成 Starter、Workbench 默认/React 三项，原生均退出 0。完整就绪/卸载/关闭、客体身份、传输及运行前后 resources/legal 通过；37 帧串口、63 次隔离采样、基础盘和冻结输入前后核验均保留，89 份原始记录已归档。只读严格校验器、宿主复核和客体驱动均退出 0。

attempt2 的宿主模块路径启动失败也原样保留，没有启动客体。修正仅限宿主驱动进程环境。旧 preview.11 关闭停滞在本次当前输入中未重现，但短测不能替代 V02 真实交互、重复关闭、长稳、物理机或性能验收；生产状态仍为 preview。

## 2026-09-27 preview.15 解压版桌面完成与 Win7 中断

[无 Git 解压版桌面验证](../../verification/production-validation/preview15-relocated-desktop-2026-09-27/README.md)补齐两架构新编 EXE 的六个入口：Starter、默认/React Workbench 原生均退出 0、就绪和关闭完整，构建 ID、完整载荷/资源/legal 和运行期间门禁通过。初次外部 QEMU 门禁拒绝仍保留，31 份原始文件独立归档。短测不关闭源码交付、长稳或生产总门槛。

[Win7 当前输入 attempt1](../../verification/production-validation/win7-sp1-x86-qemu-preview15-apps-attempt1-2026-09-27/README.md)已显示桌面，但命令发送前另一个 QEMU 启动，驱动保护性停止自己的客体。驱动退出 1，QEMU 退出 -1；没有 SDK 命令或串口结果，不算应用失败或通过。基础盘和冻结输入前后不变，36 份原始记录保留。等待其他负载结束后使用新的 attempt2，不覆盖失败原件或扩大结论。

## 2026-09-27 preview.15 原动画真实续期准备

[当前版本续期夹具准备](../../verification/production-validation/preview15-real-renewal-preparation-2026-09-27/README.md)已匹配实际 419 文件 SDK 包，暂存 720 文件；构建身份仍为同一 717 文件输入。保留原 HTML/CSS 动画和 Web 逻辑，以及已修正的监督器取快照/计时顺序。新校验器 106 项合成边界测试通过；实际运行器校验代码的两个独立命令正确拒绝旧冻结和错误来源，122 份原始记录保留。

SDK 独立编译入口因其他 QEMU 运行被门禁拒绝，原始退出 2，没有启动编译器或应用。当前两架构 SDK 安装、消费者编译、自检、实际输入冻结/绑定和 35 分钟真实续期均未执行；合成测试不作为真实续期或生产通过。

## 2026-09-27 preview.15 完整源码重编与 Win7 新冻结

[完整源码 ZIP 的无 Git 独立解压和四配置构建](../../verification/production-validation/preview15-full-source-build-2026-09-27/README.md)已完成：34,612 文件完整清单、同版 419 文件 SDK 匹配在构建前后通过；x86/x64 × Debug/Release 原始退出 0，完整单元/契约入口和合计 48 项 CTest 通过。八份运行载荷、每份 60 个 PE 及实际编译命令复核通过。解压版 GUI 启动被外部 QEMU 门禁拒绝，退出 2、未启动应用；239 份原始文件保留。

[当前 Win7 x86 的 v3 输入](../../verification/production-validation/win7-sp1-x86-qemu-preview15-apps-v3-preparation-2026-09-27/README.md)已冻结到 SHA `47e463d7746dd9824a6083d3e12b26d0910ac79a35bd7d69060b1ffe62b9e005`，绑定 preview.15 原仓库 x86 Release 构建。串口校验器 1 个完整合成样本和 27 个拒绝样本、传输/进程诊断宿主辅助函数自测均通过，84 份原始记录归档。60 秒期限不变，新增一次 55 秒未退出时的线程状态记录。客体尚未运行，原关闭失败未关闭；等其他虚拟机退出后串行执行。完整生产门槛继续待验收。

## 2026-09-27 preview.15 本机桌面短测

[当前候选的串行桌面证据](../../verification/production-validation/preview15-desktop-2026-09-27/README.md)完成两架构窗口线程超时、目录取消、新编 SOUI/内核组合的模板与 Workbench React，以及固定包的模板、默认/React 六入口，合计 14 次原生退出 0，页面就绪与关闭、完整运行载荷核验通过。第一次命令因编译节点尚未退出被拒绝，未启动 GUI；原样保留。717 文件源码输入不变。Win7、真实长会话、V02、驻留/多窗、资源和生产门槛继续待验收。

## 2026-09-27 双架构原动画修复与新包候选

[x64 完整重编与原动画实测](../../verification/production-validation/scheduler-fairness-x64-rebuild-2026-09-27/README.md)补齐另一架构：构建退出 0、六份 DLL 身份通过，原动画消费者 76 条事件、退出 0，任务结果、取消、退订及关闭完成，完整载荷和门禁复核通过。首个命令被外部 QEMU 门禁拒绝，未启动 GUI；失败样本保留。结合既有 x86 记录，实际修复覆盖两架构的这一启动复现。

[SDK Core 包验证](../../verification/production-validation/sdk-core-native-backups-2026-09-27/README.md)发现两份历史内核备份进入活动包，修正后旧包安装、新包升级、旧包回退和再次升级保留业务内容。内部 preview.14 的源码匹配仍失败，原件保留；preview.15 统一文件选择后，实际新包与源码匹配通过。两份新内核及授权核查对象、版本和清单已同步；[四配置应用构建和 48 项 CTest](../../verification/production-validation/preview15-app-matrix-2026-09-27/README.md)已退出 0，当前 GUI、Win7、真实续期和生产门槛继续待验收。

[数值格式警告的限定诊断](../../verification/production-validation/kernel-number-format-diagnostic-2026-09-27/README.md)完成 833,668 次 x86 ASan 调用，没有报告内存错误；它只覆盖提取函数和记录的输入/区域设置，未关闭完整内核的安全或编译警告风险。

## 2026-09-26 x86 实际内核修复复现通过

[x86 完整底层重编及原动画实测](../../verification/production-validation/scheduler-fairness-x86-rebuild-2026-09-26/README.md)已完成：`-Component All` 原始退出 0、28,163 文件输入不变，六份 DLL 身份通过。冻结消费者只替换新内核，保持原页面动画、EXE、JS 与期限；真实运行完成两类任务、结果 42、取消、退订和关闭，76 条原生事件、退出 0，载荷和门禁复核通过。新内核 SHA 为 `2ed793ccccb0642df9916128e36aece733efd34ad6acb4e50caa0622a4c25ef3`。

这关闭了该 x86 页面在旧策略下 60 秒未就绪的复现；它仍是旧独立消费者配新内核的短测。x64 完整重编已启动，固定包未替换；新 SOUI DLL 的应用运行、完整当前 SDK、Win7、V02、真实续期与生产门槛继续待验收。构建中其他第三方警告未完成风险排除，不按退出 0 自动批准安全门。

## 2026-09-26 原生加载任务饥饿与策略候选

[匹配符号的 x86 队列快照](../../verification/production-validation/preview11-renewal-scheduler-stack-2026-09-26/README.md)确认主线程加载任务相对上一帧时钟已迟约 10.28 秒，队列仍有已就绪、更高优先级的页面提交任务。原动画、二进制和期限未改，原生仍退出 1、bootstrap/任务为 0。全部原始记录和失败保留，调试器样本不作性能证据。

[候选策略回归](../../verification/production-validation/scheduler-fairness-policy-2026-09-26/README.md)在旧策略实际失败；加入就绪任务等待补偿后，x86/x64 各十二项通过。设计见 [ADR-0016](../adr/0016-scheduler-ready-task-fairness.md)。完整依赖重编仍在进行，尚未替换固定 DLL、增加交付版本或证明原动画恢复。源码候选与既有 preview.13 发布输入分别保留，续期、V02 和生产门槛不放行。

## 2026-09-26 外部监督器计时竞态修正

[新的独立夹具](../../verification/production-validation/preview11-renewal-supervisor-clock-2026-09-26/README.md)已确定性复现“先读时钟、后取快照”导致无符号差值下溢，旧顺序运行退出 1。改为先取快照、再读单调时钟后，x86/x64 各 8 项计时边界检查、45 项阶段自检及 8 条日志检查通过；真实消费者也重编完成，SDK 与父冻结未改。新冻结 SHA 为 `b58de0c44a2ae96a12aa144f4d8607b800415c40b1311591e05b91f67486faef`。

桌面空闲后两架构串行运行关闭动画的诊断副本，各 77 条日志、退出 0，任务结果/取消/取消订阅与关闭完成，载荷及门禁复核通过。监督器修正没有解决原动画开启时的加载问题，没有扩大时限；真实续期、V02、长稳与生产门槛继续未通过。

## 2026-09-26 冻结 preview.11 动画与监督器诊断

[五份诊断副本](../../verification/production-validation/preview11-renewal-animation-diagnostic-2026-09-26/README.md)保留原二进制、JS 和期限。字符集位置不变仍未就绪，1 秒及 20 秒都只解析出两段诊断脚本。相同页面仅关闭旋转动画后，bootstrap/ready、两类公共任务、订阅和取消完成，原生退出 0；这是诊断短测，不是生产修复。另一次定时关闭动画的运行已就绪，却在新页面记录后 15 ms 报“30 秒无记录”退出 1。源码轮询先读时钟后读跨线程快照存在无符号减法下溢风险，需另建修正夹具；原失败仍保留。

连续动画与低优先级加载任务饥饿尚待原生调度证据确证，不能直接删除动画或宣称 DOMException 为根因。35 分钟及八小时真实续期仍未执行，本轮证据不转移为当前 preview.13 通过。

## 2026-09-26 preview.13 完整源码四配置构建完成

[修正后的新完整源码包](../../verification/production-validation/preview13-full-source-build-2026-09-26/README.md)在独立无 Git 解压目录通过同版 419 文件 SDK 匹配；x86/x64 × Debug/Release 的 SDK、Starter、Workbench 四次构建均退出 0。44 项原生契约测试和解压目录内 8 项交付身份测试通过，完整源码清单在构建及测试后保持一致。八份运行载荷及各 60 个 PE 架构已独立复核，实际编译/链接无 vcpkg 路径。

本轮绑定 ZIP SHA `bbd0aa35d66ae85662fff9e4047598f82507974fb9ee48bffd2c99d9d95de9fd` 和 714 文件输入 `a1007f2188102dfa4de4e79f73a4e0b2c2b8b99cf3e892a3d1e1e3794fd51e68`。未执行这些新载荷的 GUI，也未重编本候选全部底层依赖或完成干净机器复现；完整交付、设备与生产门槛继续未通过。以下记录保留各自执行时点。

## 2026-09-26 源码交付身份修正与页面脚本定位

[完整源码解压预检](../../verification/production-validation/preview13-legal-identity-2026-09-26/README.md)发现 Element Plus 许可证被 Git 从 CRLF 转成 LF，同版 SDK 匹配失败，尚未编译。全源码清单自身通过不能替代 SDK 包匹配。Git 规则已覆盖全部许可证原始字节，交付身份回归先失败、后 8/8 通过；现有 419 文件 SDK 包与工作区匹配退出 0，SDK 实现及版本不变。新增回归后的 714 文件验证输入 SHA 为 `a1007f2188102dfa4de4e79f73a4e0b2c2b8b99cf3e892a3d1e1e3794fd51e68`，下一轮须用新完整源码包重新解压并构建，原失败目录不改。

[冻结 preview.11 页面诊断](../../verification/production-validation/preview11-renewal-page-stages-2026-09-26/README.md)在桌面空闲后真实运行 x64，仍未就绪退出 1。两秒/四十秒截图显示兼容补丁前标记和一秒定时器，未出现补丁后标记，缩小了观察范围；尚未确定 DOMException 内部停止点。进一步分段副本已准备，其他 QEMU 再次运行时门禁拒绝启动。当前 GUI、长稳和生产结论不扩大。

## 2026-09-26 preview.13 旧现场副本恢复与四配置构建

[本轮证据](../../verification/production-validation/preview13-legacy-recovery-build-2026-09-26/README.md)在独立副本中从逐项核验的备份恢复 7 个文件，旧 preview.10 的 417 文件校验通过，再升级 preview.13 的 419 文件包；22 个业务文件保持原字节，原失败现场 858 文件完全未变。旧无 journal 的自动恢复仍拒绝，本结果仅适用于这一已知现场的手工恢复。

仓库外账本项目 x86/x64 × Debug/Release 四次编译退出 0。发现宿主全局 vcpkg 输入后，新副本只在子进程中关闭自动接入并重编四组，均退出 0；实际 CL/link 命令已无 vcpkg 路径。八份 71 文件运行载荷及每份 60 个 PE 架构复核，完整原始日志归档。SDK 输入仍为 714 文件、SHA `f2ca9f2ca470c5886cda1456b31b4260795667eb4df65d4c679d8acd80f2a788`，没有改源码或版本。其他 QEMU 仍占用，GUI 未执行；本候选完整应用、底层依赖、目标平台和生产门槛继续待验收。

## 2026-09-26 preview.13 安装事务恢复

[新候选证据](../../verification/production-validation/preview13-install-recovery-2026-09-26/README.md)记录持久事务、`recover` 和启动恢复：真实终止子进程回归及交付身份共 30 项通过，原包行为 14/14；419 文件包与当前源码匹配。两个完整包业务副本分别在文件替换后和锁提交后被终止，新的恢复进程核验为旧 preview.11 / 新 preview.13，再执行升级、回退和再升级，22 个应用/数据文件保持原字节。

内部 preview.12 真实包回退的目录退休 EPERM 及路径形态负例失败均保留；preview.13 增加有界重试和提前拒绝文件/目录形态变化。旧无 journal 失败现场迁移、当前原生编译及 GUI 尚未验证。下一步继续旧失败现场受控恢复、本版安装后独立编译与业务运行；Win7 关闭、续期夹具启动、V02/V04、长稳、Electron 与发布责任等完整门槛保持未通过。

## 2026-09-26 preview.11 新增实际桌面失败

其他 QEMU 退出后，串行执行 [Win7 第三轮](../../verification/production-validation/win7-sp1-x86-qemu-preview11-apps-attempt3-2026-09-26/README.md)：v3 传输集合、546 个运行前载荷文件及 legal 身份通过，Starter 就绪并退出 0；Workbench 默认入口就绪后停在关闭开始，达到原 60 秒期限，被客体驱动终止。React 和运行后载荷检查未执行。29 帧完整串口和冻结校验器退出 1 保留，QEMU 正常退出且基础盘及输入未变。下一项是关闭阶段/线程栈诊断，不放宽期限。

随后 [v2 续期夹具](../../verification/production-validation/preview11-real-renewal-v2-smoke-2026-09-26/README.md)本机 x86/x64 首次 GUI 均因初始 60 秒未就绪退出 1，bootstrap、ready、任务调用为 0；一次初始生命周期退役分类正确，关闭完整、无外部桌面负载、输入前后一致。独立页面诊断副本仍只显示静态初始化内容，尚未确定 classic script/polyfill/System.import 的实际停止点。原冻结失败不覆盖，35 分钟和八小时不启动，生产结论保持未通过。

## 2026-09-26 preview.11 四阶段独立升级构建与中断缺陷

[V06 原始证据](../../verification/production-validation/preview11-upgrade-interruption-2026-09-26/README.md)记录仓库外账本项目的 preview.10 初始、升级 preview.11、回退和再升级：八次 x86/x64 Release 构建均退出 0；22 个应用及数据文件在每段安装前后、构建后保持原字节，八份 71 文件运行载荷另存并复核。其他 QEMU 持续占用，尚未执行这些新载荷的 GUI，不能称四阶段业务运行通过。

独立副本的真实进程终止探针发现安装恢复缺陷：旧文件重命名到事务备份后终止子进程，正式项目校验与重新安装均退出 1。业务数据与旧锁未改，417 个旧 SDK 文件可从现有内容和备份逐项恢复；CLI 未提供自动恢复。当前安装器只在进程内 catch 回滚，内存中的进度不能跨退出恢复，已有模拟 I/O 异常测试不覆盖该场景。失败现场原样保留，后续须补齐持久事务与冲突/恢复契约后再验收。完整 V06、源码交付、生产准入仍未通过。

## 2026-09-26 preview.11 续期夹具 v2 编译与输入绑定

[v2 独立构建记录](../../verification/production-validation/preview11-real-renewal-v2-build-2026-09-26/README.md)修正外部夹具的阶段观察：bootstrap 调用前最多记录一次生命周期退役；唯一成功 bootstrap 响应返回给页面前绑定可信 session/origin，绑定后包括 describe/ready 之前的意外关闭、身份变化和重 bootstrap 均失败。未改变 SDK 的 711 文件输入，也未覆盖 v1 失败样本。

两架构各 13 个构建步骤及外层构建均取得原始退出码 0；每架构 native self-check 完成 45 项解析/阶段检查及 8 条日志写入检查。106 项分析器合成自检通过；运行脚本的生产输入校验代码完成 2 个真实 v2 输入正例、6 个旧版/篡改记录负例，全部符合预期。运行脚本已隔离 v2 结果目录并验证 freeze 和两个 build 的 revision/父冻结身份。最终 freeze SHA `80d19d4e2008a505bbb8a9293849f3756d4bf07ad7838ad38aa5bb054ac0ee3a`，独立复核两架构各 70 个运行文件和 PE 架构、安装 SDK 及新头文件的实际编译输入。

首次命令采集器在只读日志描述符上 fsync 失败，未保存子进程数值退出码，原样保留且不计通过；修正采集后另建样本重试。当前工具链审计因 TLS 建连中断退出 1，没有漏洞统计，不能写为 0 项漏洞；两架构锁文件一致且工具链前后未变。宿主 vcpkg 未冻结链接输入的限制仍在。其他 QEMU 持续占用，本轮尚未执行 v2 GUI、35 分钟或八小时真实续期；上述自检不能关闭资源、性能或生产门槛。

## 2026-09-26 preview.11 Win7 客体首次执行与传输目录诊断

桌面空闲后串行运行了两次独立 Win7 SP1 x86 snapshot：[attempt1](../../verification/production-validation/win7-sp1-x86-qemu-preview11-apps-attempt1-2026-09-26/README.md)与带诊断的 [attempt2](../../verification/production-validation/win7-sp1-x86-qemu-preview11-apps-attempt2-2026-09-26/README.md)都在传输集合检查退出 1，三个应用均未启动。两次均确认桌面及 D: 盘后只发送一次测试命令，完整串口分别有 4/8 帧；QEMU 正常关闭且基础磁盘、冻结输入未变。第二次明确枚举到卷根 `$RECYCLE.BIN`，实际 11 项中有一个目录，触发预期 10 个普通文件的严格检查；不能据此断言目录创建者，也不能称其他普通文件已逐项散列通过。

[v3 准备](../../verification/production-validation/win7-sp1-x86-qemu-preview11-apps-v3-preparation-2026-09-26/README.md)把完全相同的 10 个传输文件放入卷内 `soui-input` 子目录，原集合与逐文件散列规则不变；两个 ZIP 和 546 个应用文件保持原字节。冻结 SHA `10c9a4e90cf5e25a2c98d0f6107eb7881a5ab39caf768973d761a1045566826d`。第三次启动前新出现其他任务的 QEMU，门禁拒绝且未创建 attempt3；尚不能宣布布局修正或当前 Win7 三入口通过。已有真实失败均保留，不重跑旧版通过样本。

## 2026-09-26 preview.11 续期夹具真实启动失败

双架构编译后，首次[本机 x86 续期夹具短测](../../verification/production-validation/preview11-real-renewal-smoke-attempt1-2026-09-26/README.md)真实启动 GUI，但在页面就绪前因 session shutdown 退出 1，ready 和两类任务调用计数均为 0；非超时，无外部桌面负载，退出清理完整。冻结分析器退出 1，原始样本保留。源码审查确认夹具把初次 Host 生命周期退役与实际授权会话关闭混为一谈，但实际日志缺少回调身份，尚不足断言本次精确调用来源。需在新夹具中从唯一成功 bootstrap 时立即绑定真实会话，保持绑定后的所有关闭、换会话和重 bootstrap 严格失败；不修改 SDK 或原冻结输入。x64 尚未运行，真实超过 30 分钟及八小时均未开始。

## 2026-09-26 preview.11 真实时钟续期消费者编译

本节保留编译与冻结时点，后续 GUI 失败见上一节。

[独立消费者构建证据](../../verification/production-validation/preview11-real-renewal-build-2026-09-26/README.md)新增两架构真实编译及无窗口自检：`x86-attempt4`、`x64-attempt1` 各 13 个原始步骤均退出 0，包含严格 TypeScript、Chrome 49 页面构建、安装后 SDK 链接及八条日志的无窗口自检。前三次 x86 失败及其源码快照完整保留，修正仅位于外部验证夹具；SDK 的 711 文件输入 SHA `a0c1d7900d39555508288d5459e65d3852e543cf7ec3879126bd6af59d2c2550` 未变。

两架构运行输入已经冻结，`freeze.json` SHA 为 `7e45bc6158db50362e088ba8a97a08e42e7e63e398dde62df1dbe6ce6721620d`。链接记录仍包含宿主 vcpkg 搜索路径及未冻结的外部库参数，因此不把此结果称为干净机器可复现构建。已有其他 QEMU 运行期间未启动该消费者的 GUI；30 秒夹具 smoke、真实超过 30 分钟保留原任务/订阅及八小时续期均未执行。分析器 88 项合成自检只验证分析规则。完整 SDK 交付、隔离性能及生产门槛保持未通过。

## 2026-09-26 preview.11 本机桌面执行

[本轮原始证据](../../verification/production-validation/preview11-desktop-execution-2026-09-26/README.md)沿用同一 711 文件编译输入：x86/x64 窗口线程停滞与目录取消四项均退出 0；Starter、Workbench 默认/React 两架构六入口及仓库外生成项目两架构启动/就绪/关闭短测通过。首次无退出码记录及生成项目测试入口路径错误均保留；修正外部启动器没有改变 SDK 内容。

发现两项真实失败：x86 公共菜单自动请求返回 `E_MENU_FOREGROUND`、退出 1；x86 Workbench 三窗口驻留的第二窗未在 45 秒内达到页面就绪、退出 3。两者清理完整但功能未通过，根因尚待带诊断复测，x64 对应场景尚未执行。启动 Win7 前其他 QEMU 再次占用，门禁阻止平行客体。当前版 Win7、V02 和实际续期长测仍待执行；短测不关闭生产、资源或性能门槛。

该次桌面归档时另已准备[真实时钟续期独立消费者](../../verification/production-validation/preview11-real-renewal-preparation-2026-09-26/README.md)及完整输入校验、监督和分析脚本。88 项校验器合成自检通过；当时尚未编译消费者或运行真实 GUI，后续编译结果见本页首节。旧混合驱动反复重载、V02 短任务都不能证明同一页面超过原 30 分钟期限后仍保留任务/订阅，此准备专门补该缺口，不改变 SDK 冻结输入。

## 2026-09-26 preview.11 同文档授权续期预检

以下保留预检当时的结果；后续本机桌面与消费者编译证据见本页前两节。

当前候选另已完成 [Win7 SP1 x86 三应用输入冻结](../../verification/production-validation/win7-sp1-x86-qemu-preview11-apps-2026-09-26/README.md)：Starter 71 文件、Workbench 475 文件与 legal 材料复制核对完成，串口校验器拒绝 17 个负例；尚未运行客体，等待其他 QEMU 空闲。准备检查不计为 Win7 通过。

当前开发版本为 `0.3.0-preview.11`。按 [ADR-0014](../adr/0014-same-document-session-renewal.md)新增显式启用的同文档授权续期、稳定授权资源归属和有界确认重取；任务与订阅保留身份，普通业务调用保持同步发送，不因续期自动重放。Demo 同时补齐订阅容量、事件监听器异常隔离及所有已发行会话的撤销与 worker 关闭顺序。

[本轮预检归档](../../verification/production-validation/preview11-session-renewal-preflight-2026-09-26/README.md)记录 x86/x64 Release 的 SDK、Starter、Workbench 完整编译、独立 SDK 安装及仓库外 `session-consumer` 编译/运行，均退出 0；每架构 10 项 CTest 通过，其中原生授权续期包含 14 组契约。最终汇总已核验 Web 62 项、包行为 14 项、交付身份 7 项通过，以及五份锁文件的全依赖审计漏洞总数均为 0；418 文件 SDK 首次打包成功，包内散列及源码匹配通过。由包生成的仓库外 `RenewalStarter` 也完成 x86/x64 Release 编译与项目校验，均退出 0；尚未运行该新项目的 GUI。

可控时钟的八小时轮换仅验证期限、授权隔离和固定容量契约，不是实际运行八小时的稳定性证据；原生扫描与 CAD worker 的故障回归也不代替媒体引擎或真实页面验收。本版真实桌面有效结论、超过 30 分钟的同文档任务/订阅保留、八小时资源趋势和 Win7 SP1 x86 证据仍待补齐。首次 x86 窗口线程超时桌面探针输出成功标记，但监督器未取得退出码；原样本保留且不计通过，退出码采集修正未改变冻结源码。外部 QEMU 再次占用，后续补测等待空闲后串行执行，桌面/客体结果另档绑定输入，不能并入本次无 GUI 预检。`sourceDeliveryVerified=false`、`benchmarkValid=false`、`releaseReady=false`。

## 2026-09-26 preview.10 公共菜单、快捷键与显式驻留

新增 [ADR-0013](../adr/0013-public-menu-shortcut-and-residency.md)、公共 `menu_service` / `global_shortcut`，独立安装消费者接入三项原生服务。Workbench 提供三窗口控制器、`--resident` 与有限 `--resident --resident-smoke-test`，关闭按业务窗口消息栈和代际延迟释放，日志在载荷外独占创建。默认单窗路径保留。

[冻结预检](../../verification/production-validation/preview10-residency-preflight-2026-09-26/README.md)包含同一输入的 x86/x64 Release SDK/Starter/Workbench 编译、独立 SDK 安装、仓库外消费者编译、各8项非桌面交互 CTest。菜单15组、快捷键9组故障契约通过；Web35、包测试14、身份测试7及五锁文件全依赖审计通过。417 文件 SDK 包校验与源码匹配通过；首次 rename `EPERM` 失败保留，原因未确证。

当次预检时其他 QEMU 仍运行，preview.10 尚无真实桌面或 Win7 结果。下一步串行补消费者菜单/快捷键、Workbench 三窗及模态关闭/空窗驻留，以及 V02 IME/缩放/任务同时响应。旧版本、短测和受干扰八小时数据不迁移为本版通过；完整目标和三个准入标志未关闭。

## 2026-09-26 preview.9 公共托盘切片

新增 [ADR-0012](../adr/0012-public-tray-service.md) 与公共 `tray_service`、安装后独立消费者。双架构 SDK Release 独立构建、各 5 项无桌面契约、仓库外消费者编译通过；托盘包含 14 组边界/失败测试，故意恢复旧清理逻辑的反例按预期失败。其他 QEMU 退出后，当前输入的 x86/x64 窗口线程停滞、目录取消及托盘真实 Shell 短测串行退出 0，详见[冻结记录](../../verification/production-validation/preview9-public-tray-preflight-2026-09-26/README.md)。短测每架构 12 个关闭顺序事件完整，但未产生真实点击或 Explorer 重建通知。411 文件 SDK 包校验与源码匹配通过，首次改名 `EPERM` 失败仍保留。

真实托盘键盘/Explorer/Win7、公共菜单/快捷键、完整 Workbench 驻留、V02 和同文档长会话续接均未完成；preview.8 的全量源码、应用及 Win7 记录保持原身份，不转移为 preview.9 通过。完整目标及三个生产准入标志仍未关闭。

## 前序执行记录

关联：[完整方案](../roadmap/0.3-sdk-validation.md)、[中期准入判定](interim-decision.md)、[ADR-0002](../adr/0002-public-sdk-acceptance.md)、[ADR-0007](../adr/0007-rebuilt-miniblink-preview-binary.md)。开始日期：2026-09-22。完整目标持续进行，未生成正式生产准入结论。下文早期“当前候选”描述为当次实验的历史身份；preview.4 的新固定包证据在文末单列。

逐能力的公共入口、当前证据与剩余条件见[能力映射](capabilities.md)；设备和内核维护分别见[支持矩阵](support-matrix.md)、[内核准入核对](kernel-maintenance.md)。

| 要求 | 当前工作与完成证据 |
|---|---|
| 公共 SDK 独立接入 | 已新增 `tests/acceptance-app`，从独立安装的 SDK 编译；x86/x64 Release 已真实运行，见[生命周期报告](lifecycle.md) |
| V01 通信与长任务 | preview.5 的连接器及 Host 容量完成短测；待审查 PR #26 已加入公共任务服务、模板、Demo 公开任务场景和独立消费者。双架构原生契约、Demo 无窗口适配器测试与外部构建通过；真实页面和长稳仍待验收 |
| V02 数据与交互 | Workbench 已有共享万条数据的虚拟滚动、中文输入事件、动效、resize 和公共任务页面，严格类型与现代/legacy 构建通过；真实 WebView、后台、IME 与 Win7 x86 待验收 |
| V03 桌面工作台 | 待将通用注册归入公共 SDK，完成对话框/剪贴板/多屏/shell 验收 |
| V04 多窗口与驻留 | 已复现旧版第二窗口挂载卡住并修复进程级内核所有权；三窗口隔离及关闭后重建短测试通过；公共窗口/托盘/菜单/快捷键仍待完成 |
| V05 资源与故障 | 已修复逐窗口初始化导致的访问冲突，以及 ToolTip、拖拽区域和线程唤醒事件释放遗漏；preview.4 双架构四种模式千次操作通过。资源所有权、完整故障恢复与诊断仍待完成 |
| V06 接入与维护 | preview.4 的[仓库外生成项目](../../verification/production-validation/preview4-independent-2026-09-23/README.md)完成包校验、x86/x64 Release 独立编译及真实启动；[业务小样四段升级回退](../../verification/production-validation/preview4-upgrade-2026-09-23/README.md)完成本机 x64 Release 的逐段重新编译、实际启动及业务文件保留；Win7 SP1 x86 QEMU 独立消费者四模式各三轮通过。Debug、x86 升级链和真实目标设备仍待完成 |
| 每种模式 1,000 次 | preview.4 固定包的软件路径 x64/x86 重载、销毁重建、三窗口及 Bridge 各 1,000 次完成并正常退出；资源门槛仍未评估 |
| 100 次重载资源趋势 | preview.4 固定包 x64/x86 均完成预热 3 次、静置 1 秒的 100 次重载；末点 Private Bytes 分别增加约 7.0/4.4 MiB，低于 16 MiB 数值线。分段趋势与对象所有权尚未验收 |
| 8 小时混合负载 | [独立消费者混合生命周期短测](../../verification/production-validation/preview5-mixed-soak-preflight-2026-09-24/README.md)已在 x86/x64 通过；公共任务业务链、外部输入和 8 小时正式结果尚未完成，不能算生产长稳门槛通过 |
| 系统、DPI、多屏、IME | 已登记本机精确系统与驱动；[Win7 SP1 x86 QEMU 公共消费者短验收](../../verification/production-validation/win7-sp1-x86-qemu-2026-09-24/README.md)完成四模式各三轮；[模板与 Workbench 短验收](../../verification/production-validation/win7-sp1-x86-qemu-apps-2026-09-24/README.md)完成三项应用启动/关闭、载荷校验及两种模式的可见界面；物理设备及[支持矩阵](support-matrix.md)中的 DPI、多屏、IME 等仍未验证 |
| 内核维护、安全、授权、发布责任 | preview.4 固定包已换入重编译内核，x86/x64 回环正负向各六项再次通过；实际信任根与代理、完整安全维护、授权及发布责任仍未关闭 |
| Electron 性能与体积对照 | 共享页面、双端最小宿主和[结果落盘探针](../../verification/production-validation/benchmark-host-smoke-2026-09-23/README.md)已实现；[x64 同页面最小运行包测量](../../verification/production-validation/benchmark-size-preview5-2026-09-24/README.md)已归档完整文件清单、两份 ZIP 和各自交付目录启动探针。真实输入、呈现、进程树性能原始样本及完整业务包体积待完成 |
| 接入、兼容和维护成本 | 待通过实际小样与升级记录采集 |
| 最终交付与三个结论 | SDK、模板、Demo、测试、benchmark、支持表、迁移/诊断手册和工程/生产/价值判定均须逐项验收 |

本文件记录执行进度，不能代替机器报告或修改 `releaseReady`。新增阶段证据应链接实际 run-id、源码与载荷身份，并列出未执行项。

以下为 preview.9 托盘切片及更早阶段的历史记录，preview.11 当前范围见本文件首节。根 `verification/acceptance.json` 仍是 preview.2 基础切片的历史结果；后续记录在 `verification/production-validation/`，不能互相替代。下述模板和 Demo 默认/React 的 x86/x64 Release 回归按各自原版本判定。此前 x86 完整构建有一次资源目录替换 `EPERM`；旧目录恢复后，未改源码重试通过，失败日志保留且根因未关闭。

preview.7 的[源码编码修复诊断](../../verification/production-validation/preview6-relocated-dependency-encoding-2026-09-25/README.md)确认 preview.6 无 Git 源包在 CP936 环境重编 x86 Miniblink 失败；定点 BOM 补丁后，修改过的仓库外目录里 x86/x64 SOUI、Skia、Miniblink 源码重编与两架构真实应用启动均通过。当前 preview.7 的双架构 Release 构建、契约测试和三入口 smoke 也通过，但它们使用固定运行包，不能和新内核结果合并。[Win7 SP1 x86 QEMU 重编内核短测](../../verification/production-validation/win7-sp1-x86-qemu-preview7-rebuilt-source-2026-09-25/README.md)完成三入口启动/关闭，24 份串口文件、来宾身份和载荷散列复核通过。诊断目录不是最终 preview.7 原封整包；最终整包重建、Debug、物理设备、长期资源、实际呈现性能与安全授权仍未完成，`sourceDeliveryVerified=false`、`releaseReady=false`。

[preview.7 无 Git 完整源码快照](../../verification/production-validation/preview7-full-source-stage-2026-09-25/README.md)经源清单逐文件验证，在仓库外完成 x86/x64 Release 应用重编、完整测试和三项桌面 smoke；SDK 新包的 408 个文件也通过包内完整性、源匹配及 14 项包行为测试。该快照使用固定运行依赖；最终原封整包的底层依赖全量重编继续待验收，不改变 `sourceDeliveryVerified=false`。

后续已从 SHA-256 `A4F0703A4C6F6E13DC4A07BC59174D347487622143AAB51E82B3D3F4190D802E` 的[最终 preview.7 原封完整源码 ZIP](../../verification/production-validation/preview7-final-source-bottom-rebuild-2026-09-25/README.md)分别全新解压 x86/x64 无 Git 目录。编译前两目录清单验证均通过 30,719 文件；各自重编所有底层依赖、应用 Release、完整测试和重编 DLL 启动，均退出 0。[同一 x86 来源的 Win7 SP1 x86 QEMU 三入口](../../verification/production-validation/win7-sp1-x86-qemu-preview7-final-source-2026-09-25/README.md)也通过 24 份串口结果校验。旧 SOUI 工程会在源码树产生 112 个额外编译产物，故编译后原封文件集合复验失败，但逐文件比对确认清单源码内容未改；交付流程仍需修正。Debug、目标设备、资源、实际呈现、完整业务与安全授权未通过，`sourceDeliveryVerified=false`、`releaseReady=false`。

preview.8 的[无 Git 候选完整源码包](../../verification/production-validation/preview8-isolated-source-build-2026-09-25/README.md)在 x86/x64 全新目录完成全部底层重编后，30,794 文件清单依然通过；四组 Release/Debug 应用构建、完整测试与三入口桌面短测、双架构重编 DLL 短启动均通过。[仓库外新项目](../../verification/production-validation/preview8-isolated-source-build-2026-09-25/README.md)双架构 Release 实际运行，[独立业务项目四阶段升级/回退](../../verification/production-validation/preview8-independent-upgrade-2026-09-25/README.md)双架构通过。[同源 x86 重编载荷的 Win7 SP1 x86 QEMU 三入口](../../verification/production-validation/win7-sp1-x86-qemu-preview8-isolated-source-2026-09-25/README.md)已核验 24 份客体文件。[最终源码包生成的独立项目 Win7 SP1 x86 QEMU 短测](../../verification/production-validation/win7-sp1-x86-qemu-preview8-independent-consumer-2026-09-25/README.md) 从最终包生成模板工程、在来宾逐项校验 71 文件并正常启动/关闭；第一次脚本失败被保存，修正后复测通过。最终源码包与候选的代码等价性已核对；物理目标设备、八小时资源、Bridge/GPU、Electron 呈现和安全授权仍需收敛；`sourceDeliveryVerified=false`、`benchmarkValid=false`、`releaseReady=false`。

ToolTip 修补后的 x64 内核及全部原生依赖已重新编译；候选 SHA-256 为 `32d37e6399bc04e2a8d909c4021f49e3b8f21521b805f9f118f15e111ea0e59f`，三模式各 30 轮加 3 次预热全部完成。重建 USER 不再增长，但 GDI/普通句柄仍增长，缺陷未关闭。随后定位到拖拽区域和工作线程唤醒事件的释放遗漏，保留为两个后续补丁。

v2 驱动对应 SDK/工具源码指纹为 `e29dc0d908c741642615b27d98f0d50d262131b10d0094d8b8c140660021027f`；它同时冻结消费者源码和实际候选内核输入，区别于历史 SDK 构建指纹。固定 prebuilt 始终未替换；候选压力验收仍待完成。

前两个补丁的 x64 候选 `980339823c0d25522c236e1b9f44fa36146a969d3aa8624f6ad63271e134dcfa` 已完成 30 轮重建：USER/GDI 无增长，普通句柄仍增加 30，见[分步报告](lifecycle.md)。三个补丁组合的 x64 候选 `2ed34e54c26fe074e020cee6d179ba2330dff7865db6fb3b75df039f4c69e565` 随后完成三模式各 30 轮；重建普通句柄保持 280，三窗口预热后到末轮仅增 1，未出现此前逐 WebView 累积。短回归不能关闭内存、线程残留和长期资源门槛。源码交付清单、固定依赖更新、PR 与完整交付尚未收敛。

x86 三补丁组合 `7ebac616b06aae2c86bc6a7775efed5afd6c86a542754e0dbe4e0179935131cc` 也已完成全依赖源码重建，以及独立消费者三模式各 30 轮；重建 USER/GDI/普通句柄增量均为 0。两架构的完整模板/Demo 与重编译依赖组合回归、候选长循环继续执行；不把短回归扩大为生产通过。

`e29dc0d908c7` 对应的两架构完整应用构建及三入口真实启动已通过，见[回归归档](../../verification/production-validation/regression-e29dc0d908c7/x64-observation.json)。重编译依赖的模板和 Workbench React 也已启动并正常退出；该旧依赖 smoke 工具仍输出 v1，只保存 DLL 散列，未达到完整载荷/源码身份的 v2 证据要求。此缺口明确保留，不能用本次短启动提前放行新固定依赖。

三补丁 x64 候选完成 100 次重载加 3 次预热、每轮静置 1 秒：末次 Private Bytes 比预热末点增加 9,846,784 字节，普通句柄由 296 到 295，USER 保持 23。数值内存增量低于 16 MiB，但资源与线程残留尚未整体验收，见[候选观察](../../verification/production-validation/96823023-6bef-437f-b623-447093d95089/reload-c4134f7c-1363-4579-ba6c-99ffcc195ada/resource-observation.json)。

候选 x64 的三模式各 1,000 次循环已开始。长循环运行在冻结副本中：复制全部 623 个 SDK 构建输入（指纹与原构建一致）、已安装库、消费者源码及已构建载荷；仍由原 v2 驱动逐次核对。开发树可继续变更，冻结副本不允许修改。该副本只供已构建候选验证，不是完整第三方源码交付包，不产生 `sourceDeliveryVerified`。长循环完成前不更新通过结论。

冻结 `e29dc0d908c7` 候选已完成三种模式各 1,000 次，均正常退出。reload/recreate/multiwindow 的 Private Bytes 末点相对预热分别增加 71,544,832、41,148,416 和 57,774,080 字节；recreate 的普通句柄/线程/USER 分别增加 18/4/5。原始记录和端点观察已归档到[生命周期报告](lifecycle.md)，整体资源门槛没有通过。这些是旧最小夹具的冻结结果，不包含新增 Web 连接器。

连接器这一批单元验证共 43 项：Web 客户端 31、Host 实际 resolver 脚本 4、监督器 8；模板类型检查和 Chrome 49 构建通过。新增原生请求预算测试及既有原生契约在 x64 执行通过。SDK/模板实际桌面集成需要另行执行，编译和模拟端口不替代该证据。

首次 v3 消费者 Web 构建在经典入口转换时失败：控制工具的工作目录与独立页面根目录不同，导致 Vite 虚拟 polyfill 的清单键发生变化。已让工具从独立页面根目录执行构建；首次失败目录 `out/acceptance/e913fb88-9df1-460a-9c8a-0f0a3044eadc` 保留，随后 `59c34fe00fee` 对应消费者准备成功。该失败属于测试构建链，未计作原生 SDK 运行缺陷。关闭准入屏障随后又有补充，最终真实运行应绑定新的完整指纹。

`f148b650e328` 的 x64 完整构建、本地契约测试以及两架构独立消费者编译通过，但实际 `bridge` 探针在两架构均失败。补充只读页面诊断后，[诊断运行](../../verification/production-validation/9cd2f5b9-5ad6-4994-9033-02bb3ff4d92a/bridge-082cf0ad-6ef2-4bc6-9378-ed2c9c216565/result.json)显示原生返回空值。根因是注入脚本的前导换行使原生包装的 `return` 触发自动分号插入；脚本原有测试未覆盖这层包装。测试改为真实包装方式后先复现失败，移除前导换行后恢复通过。修复后的原生集成继续验证，失败记录不覆盖、不计通过。页面诊断也改为共享结果所有权，避免等待超时后仍写入已离开的栈变量。

修正注入包装后，`2cf29a065477` 的公共 Web SDK 消费者在 x64 的三种生命周期短测均通过，但通信容量探针失败。[细化诊断](../../verification/production-validation/076187ed-66f1-4e0d-ae12-e48da99be5e4/bridge-59165ceb-51f4-4a86-a507-9b55fcd4b899/result.json)确认返回 2 次 `E_BUSY`，没有其他错误或错配。V8 在原生回送 lambda 退出前执行已完成请求的后续回调，旧请求因此仍占用一个槽位。已改为通知 JS 前幂等归还槽位，并测试旧回调对象析构不会释放同 ID 的新请求；容量断言仍要求只拒绝真正多出的 1 项，没有放宽。

当前 `8d75a05a2683` 的 [x64 v3 消费者](../../verification/production-validation/7865f62e-1992-4bb6-be47-733b21e4dc1f/build.json)四场景各 3 轮均正常退出：实际标准连接器、32 并发、原生 256 上限/控制容量、200 ms 等待期间页面定时器推进及错误恢复已在本范围通过。幂等预算归还的 x64 原生单元测试也通过。新 SDK 尚未完成模板/Demo 的最终整组回归和新的长循环。

同指纹的 [x86 v3 消费者](../../verification/production-validation/56fc6fee-15b9-407c-a6b1-7178c27111c7/build.json)编译成功，但四场景均在首次窗口身份调用处等待超时；页面已完成 bootstrap，停留在“检查窗口身份…”。失败记录保持不变。后续诊断确认交付动态绑定头把实际导出为 C++ `bool` 的 `jsIsString` 等 54 个函数声明成了 Win32 `BOOL`；x86 返回 ABI 不一致把 Promise 误判为字符串，请求因此未入队。直接改用 `jsTypeOf` 的反证实验访问冲突，最终按内核 `wkedefine.h` 对齐全部共享符号，并增加逐符号契约测试。[根因归档](../../verification/production-validation/x86-bool-abi-root-cause/README.md)保存修复前、反证和修复后的运行及 trace。

当前源码指纹 `3dcd7b97ea5f75deb752b1940ac34a30875eba6e851b00603ea426e88e1da699` 的 [x86](../../verification/production-validation/468fb9ac-9426-43e6-a878-19135c469d3e/build.json) 与 [x64](../../verification/production-validation/ad180ad6-6287-4966-930e-5223fb7378f2/build.json) 公共消费者均完成 reload、recreate、multiwindow、bridge 各 3 轮，全部正常退出。x86 的修复后 trace 已确认 resolver 创建、原生队列、工作线程响应及 JS Promise resolve 完整贯通。该结果关闭当前短回归的架构差异，不关闭 1,000 次、8 小时、资源趋势、平台矩阵、桌面模块及 Electron 对照门槛，`releaseReady` 保持 false。

随后完整应用 smoke 暴露模板和 Workbench 默认开启 GPU compositor 时的关闭崩溃：x64 三个入口都在 `adapter-events-drained` 之后、`adapter-webview-destroyed` 之前以 `0xC0000005` 退出；对已通过的独立消费者单独设置 `SOUI_GPU_COMPOSITOR=1` 也能复现，direct 为 1 或 0 均失败。[失败归档](../../verification/production-validation/regression-3dcd7b97ea5f/x64-gpu-default-failure/smoke-Release.json)保持原始退出码。模板及 Workbench 默认改为稳定软件路径后，源码指纹 `6649c3929622` 的 [x86/x64 回归](../../verification/production-validation/regression-6649c3929622/observation.json)均完成 Starter、Workbench 默认页、Workbench React 页三入口并正常关闭。GPU 模式继续作为实验缺陷，不计入生产通过。

GPU 专项继续验证了公共 SDK 消费者的 x64 重建 30 轮：固定内核及三项资源修补候选均正常退出，但 Private Bytes、USER 与普通句柄持续增长。把 GPU 表面移到合成线程析构并补齐 Skia 接口引用释放后，30 轮仍增加约 75.9 MiB Private Bytes、30 个 USER 对象和 206 个普通句柄；WGL 删除与 DC 释放在这组样本中均返回成功。[逐候选原始证据和限制](gpu-lifecycle.md)已留存。旧 GPU 关闭访问冲突不因当前短测未复现而关闭；GPU 资源门槛明确未通过，固定 prebuilt 不更新，`releaseReady` 仍为 false。

同一候选的 x86 GPU 30 轮也出现 USER 每轮约增 1；x64 软件路径 30 轮虽有句柄与进程内内存上升，但随后 100 轮显示周期性回收，不能误判为持续线性泄漏。最小 WGL 探针同机 33 轮 USER/句柄稳定，完整路径的 GPU 剩余对象所有权未定位，见[专项原始样本](gpu-lifecycle.md)。当前源码指纹 `b565bd098865` 的 [x86/x64 完整应用回归](../../verification/production-validation/regression-b565bd098865/)均通过构建、单元/契约及三个真实 smoke 入口；x64 首次构建又遇资源目录改名 `EPERM`，失败日志保留，未改源码重试通过。新诊断和短回归均不改变 preview/生产判定。

当前标准连接器的软件路径 [x64 重建 1,000 轮](lifecycle.md#当前标准连接器的-1000-次重建)与 [x86 重建 1,000 轮](lifecycle.md#x86-当前连接器的-1000-次重建)均已完整结束并正常退出；每百轮中位数和关闭后资源另行记录，两边 `resourceGate` 均为 `not-evaluated`。这只关闭该候选两个架构的重建操作门，不替代其他模式、资源或 8 小时业务门槛。

同一 x64 冻结载荷的 [100 次重载](lifecycle.md#当前连接器的-100-次重载)也完成：每轮静置 1 秒，末点 Private Bytes 较预热增加约 6.0 MiB，低于 16 MiB 数值线；USER 保持不变，句柄仅增 5，分段样本有回收。`resourceGate` 仍未评估，不把这项数值观察扩大成整体资源或生产通过。

当前 x86 冻结载荷的 [100 次重载](lifecycle.md#x86-当前连接器的-100-次重载)也完成：同样每轮静置 1 秒，Private Bytes 末点增加约 7.2 MiB，低于 16 MiB 数值线；普通句柄增加 47、USER 增加 2，Runtime 关闭后仍有 284 个普通句柄与 6 个 USER 对象。数值线通过不等于资源所有权解释完成，`resourceGate` 仍未评估。

当前连接器的 x64/x86 [1,000 次重载循环](lifecycle.md#当前连接器的-1000-次重载循环)也都正常退出；20 ms 静置下内存与句柄大幅波动并有回收，不用它替代 1 秒静置的 100 次资源数值观察。当前两个架构的重建与重载操作门已覆盖，其他模式、GPU 和资源门仍未关闭。

当前三个 JavaScript 锁文件分别执行了 [SDK](../../verification/production-validation/regression-b565bd098865/npm-audit-sdk-web.json)、[模板](../../verification/production-validation/regression-b565bd098865/npm-audit-starter-web.json)和[Workbench](../../verification/production-validation/regression-b565bd098865/npm-audit-workbench-web.json)的 `npm audit`，报告均为 0 条。这个结果仅覆盖 npm 依赖，不替代内核、TLS、授权及发布审查。

当前包的[仓库外生成项目](../../verification/production-validation/independent-project-b565/README.md)已分别完成 x64/x86 Release 编译、真实启动和正常关闭，包清单、项目锁、构建身份及原始启动日志均已归档。这证明模板不靠原仓库直接链接运行；仍未完成业务小样与升级/回退运行链。

[HTTPS 身份校验候选](../../verification/production-validation/tls-kernel-2026-09-23/README.md)在 x86/x64 各完成六项本地正负向测试，重编译 DLL 与公共消费者 Bridge 短回归绑定。旧固定 prebuilt 尚未替换；系统信任根、代理、动态子资源和旧 TLS 组件维护没有因该小范围测试而通过。

随后按 [ADR-0007](../adr/0007-rebuilt-miniblink-preview-binary.md) 将相同散列的双架构候选 DLL 放入 `0.3.0-preview.4` 固定包。[新包回归](../../verification/production-validation/preview4-fixed-2026-09-23/README.md)绑定 x86/x64 完整 Release 构建、SDK/Web/原生契约、模板与 Workbench 三入口真实 smoke、独立公共消费者四模式各三轮，以及固定 DLL 的回环 HTTPS 各六项测试，均通过。新包[仓库外项目](../../verification/production-validation/preview4-independent-2026-09-23/README.md)通过包/锁校验、双架构 Release 编译和真实启动关闭。旧 preview.3 千次与 100 次资源结果仅作历史比较；preview.4 的长循环在下文单独列出，完整资源和 8 小时门槛未通过，`releaseReady` 保持 false。

[preview.4 完整源码包搬移实验](../../verification/production-validation/preview4-source-2026-09-23/README.md)在无 Git 的仓库外解压目录通过逐文件校验、x64/x86 Release 完整应用编译与三个入口启动关闭；这是当次源码快照的证据。最终源码清单和 ZIP 必须匹配纳入新证据后的提交；底层 Miniblink/SOUI 在搬移目录的源码重编译仍待执行。

preview.4 固定包的[新生命周期归档](../../verification/production-validation/preview4-lifecycle-2026-09-23/README.md)已保存 x64/x86 各 100 次重载及四种模式各 1,000 次，均正常退出且无监督超时。100 次重载 Private Bytes 末点相对预热增加 7,368,704 / 4,648,960 字节，分别低于 16 MiB 数值线；20 ms 高频运行的峰值和句柄明显更高，不用它代替资源验收。各次 `resourceGate` 均为 `not-evaluated`。为使后续长测在开发树变化时仍绑定同一载荷，已另建隔离副本并核对两架构源码指纹、SDK build-id 及公共消费者身份；该副本仅服务测试，不等同完整第三方源码交付。8 小时混合业务仍需执行。

同一 preview.4 冻结 x64 载荷的[三窗口 1,000 轮](../../verification/production-validation/preview4-lifecycle-2026-09-23/x64-1000-multiwindow/observation.json)也完成并正常退出、无监督超时；预热末点到末轮 Private Bytes 由 82,006,016 增至 148,246,528 字节，普通句柄由 408 增至 483。逐百轮中位数后段高于前段，`resourceGate` 仍为 `not-evaluated`。该结果只增加多窗口操作证据，不能当作资源回收或业务长稳通过。

相同冻结源指纹的[x86 三窗口 1,000 轮](../../verification/production-validation/preview4-lifecycle-2026-09-23/x86-1000-multiwindow/observation.json)亦正常退出且无监督超时；Private Bytes 由 57,827,328 增至 116,793,344 字节，普通句柄由 406 增至 781。分段内存中位数总体抬高，资源所有权未解释，`resourceGate` 保持 `not-evaluated`。当前开发机运行 x86 进程不等于在目标 32 位或旧版 Windows 上验收。

[x64 Bridge 千次](../../verification/production-validation/preview4-lifecycle-2026-09-23/x64-1000-bridge/observation.json)和[x86 Bridge 千次](../../verification/production-validation/preview4-lifecycle-2026-09-23/x86-1000-bridge/observation.json)也在同一冻结身份上正常退出、无超时；预热末点到末轮 Private Bytes 分别增加 47,562,752 和 54,177,792 字节，分段中位数总体抬高。它们证明公开连接器循环操作和关闭路径，不证明内存回收、真实业务长任务、任务背压或 Win7 SP1 x86 兼容。

共享性能页面及双端最小宿主已落到 [`benchmarks/`](../../benchmarks/README.md)；页面 TypeScript 检查与构建、Electron 精确版本 `44.4.4` 安装/语法检查、公开 SDK 的 x86/x64 C++ 宿主编译已执行。[结果落盘探针](../../verification/production-validation/benchmark-host-smoke-2026-09-23/README.md)实际运行了两架构 SOUI 和 Electron，三份 S01 输出可校验，但自动完成探针没有真实交互、帧样本或完整采样区间，不能作为同机同负载性能对照。

[独立业务项目四段升级回退](../../verification/production-validation/preview4-upgrade-2026-09-23/README.md)在仓库外完成 preview.3 → preview.4 → preview.3 → preview.4，每段锁校验、x64 Release 重新编译、实际启动/关闭及业务标记均通过；五个业务文件散列不变，四个 build-id 与载荷身份已交叉核对。第一次尝试误用未随生成项目交付的 `tools/Test.ps1`，首段编译虽通过但无有效启动结论；失败日志保存，全新目录修正验证驱动并重跑四段。该证据不覆盖 Debug、x86 升级链、安装签名及目标机。

[Windows 7 SP1 x86 QEMU 目标系统短验收](../../verification/production-validation/win7-sp1-x86-qemu-2026-09-24/README.md)使用相同 preview.4 冻结源码指纹的独立公共消费者，在 6.1.7601/32-bit 上完成 Bridge、重载、销毁重建、三窗口各三轮，退出码 0，串口导出 17 个文件的长度和散列均通过，并验证每个事件流以 `run.complete` 结束。初次 QEMU 共享 FAT 盘报告截断后，所有有效结果均在来宾 NTFS 上重新运行；短验收只说明软件路径启动与基本生命周期可工作，不覆盖物理设备、资源门槛、真实业务、IME/DPI、TLS/代理、安装升级或 8 小时。

[同一 Win7 SP1 x86 虚拟机的模板与 Workbench 短验收](../../verification/production-validation/win7-sp1-x86-qemu-apps-2026-09-24/README.md)进一步使用同一冻结源码指纹的 Release 载荷。模板及 Workbench 默认/React 三项烟测均退出 0，日志确认页面和桥接就绪、关闭完成；两个 ZIP、541 个载荷文件、中文文件名和四个关键二进制散列经来宾校验，24 个串口导出文件经宿主重验。正常模式下的默认/React 界面已留截图。该结果尚不足以宣布 Demo V01–V06 或指定平台的生产通过。

preview.5 的[任务容量修复证据](../../verification/production-validation/preview5-task-capacity-2026-09-24/README.md)绑定源码指纹 `16d1dccc47ebacff83bf0a94abeed21c75cd7bcd049559802598a5e44656f237`：x86/x64 Release 完整构建、三项实际应用 smoke、原生任务容量/并发/取消/扫描错误传播测试均通过；由当前 SDK 包生成的仓库外项目也在双架构完成 Release 编译与实际启动/关闭。无 Git 完整源码解压目录经逐文件验证后也在两架构重新完成应用构建、测试和三项 smoke。三个 Web 锁文件本轮 `npm audit` 均为 0 项。打包发布目录的 Windows 短暂 `EPERM/EACCES` 有界重试经过注入测试与本机反复打包；实际锁持有者仍未确认。[preview.5 Win7 SP1 x86 QEMU 应用短验收](../../verification/production-validation/win7-sp1-x86-qemu-preview5-apps-2026-09-24/README.md)进一步核对 24 个串口导出文件，模板与 Workbench 默认/React 三入口均退出 0；preview.4 的四模式公共消费者及长循环不能升级为 preview.5 的通过结论，完整业务链和资源门槛继续待验收。

[preview.5 Win7 SP1 x86 QEMU 公共消费者四模式短测](../../verification/production-validation/win7-sp1-x86-qemu-preview5-consumer-2026-09-24/README.md)随后使用独立安装的 SDK 与公共 Web 连接器重新编译，在 6.1.7601/32-bit 来宾分别完成 Bridge、重载、销毁重建、三窗口各 3 轮，退出码均为 0。26 个串口文件的长度与 SHA-256、来宾身份、运行载荷、完整事件步骤及资源采样顺序经宿主复核。该结果只关闭新版的四模式短操作门；任务容量满载业务、千次循环、资源所有权与 8 小时混合负载尚未通过。

preview.5 固定载荷的[x86 Bridge 长循环复测](../../verification/production-validation/preview5-lifecycle-2026-09-24/README.md)有两次千轮目标失败、三次同载荷诊断成功；失败时未抓到线程栈。为缩小下次复现范围，公共验收消费者增加可选的挂载阶段日志。另一源码指纹的[x86 1,000 轮阶段诊断](../../verification/production-validation/preview5-mount-phase-2026-09-24/README.md)完成全部操作且无超时，10,030 条阶段事件和观察器报告均归档；它不替代原载荷失败样本。生产零超时、内存/句柄所有权及 Win7 客体长稳仍未通过。

随后监督器增加[超时现场保存](../../verification/production-validation/preview5-timeout-diagnostics-2026-09-24/README.md)：记录已读日志偏移、未读内容和最后原生事件；配置 x86 调试器时，在终止进程前尝试抓所有线程栈及完整转储。受控进程的抓取测试通过。该新来源的 x86 Bridge 长测连续两次完成 1,000 轮，均未触发故障抓取，因此原两次超时的根因和生产门槛没有改变。

原失败源码在独立目录重新编译后又完成一次 x86 Bridge 1,000 轮，外部 8 秒观察器未见停滞；新构建载荷 SHA-256 与失败载荷不同，不能合并为原载荷成功样本。原失败保持两个，故障线程栈仍缺失。

[preview.5 新诊断源码的 Win7 SP1 x86 QEMU 100 次重载](../../verification/production-validation/win7-sp1-x86-qemu-preview5-reload-2026-09-24/README.md)进一步在来宾 NTFS 完成 3 次预热 + 100 次测量、109 个生命周期步骤，退出码 0；15 个串口文件及系统/二进制身份经宿主复核。Private Bytes 预热末点到最后一轮增量 14,876,672 字节，低于 16 MiB 数值线，但句柄与线程分别增加 57、4，资源门仍未评估。这是虚拟机与不同源码指纹的重载证据，不能替代原 Bridge 失败定位或物理设备验收。

[preview.5 原生任务容量测试](../../verification/production-validation/win7-sp1-x86-qemu-preview5-task-capacity-2026-09-24/README.md)进一步在 Win7 SP1 x86 QEMU 客体执行：来宾 6.1.7601/32-bit/SP1 身份、测试 EXE SHA-256、退出码 0 与标准输出由五个串口文件逐项复核。它覆盖原生任务表 1,024 上限、取消回收、并发准入和扫描拒绝；首次临时载荷缺 DLL 的加载失败已保留并在补齐后重跑。公共任务业务链、长稳与物理目标机仍未通过。

[preview.5 最小宿主包体积测量](../../verification/production-validation/benchmark-size-preview5-2026-09-24/README.md)把同一份页面装入 x64 Release SOUI 和 Electron 44.4.4 的独立运行目录，逐文件核对散列，使用相同 ZIP 算法并测试压缩包。SOUI/Electron 解压大小为 38,765,828/386,055,337 字节，ZIP 为 14,533,916/154,312,842 字节。两端从打包目录执行 S01 结果落盘探针并退出 0。探针不含真实交互或呈现，Electron 标准错误有 GPU 状态告警；该测量只支持最小宿主体积差异，不能作为性能或生产通过结论。

公共 SDK 长任务服务开发分支新增已认证任务启动、查询、订阅、协作取消与有界工作队列，并在模板接入可取消计数任务。x86/x64 Release 原生任务契约、模板 x86/x64 编译、模板 Chrome 49 页面构建、TypeScript 检查和 Web 客户端单元测试通过；这些是开发树验证，不属于 preview.5 冻结包。真实页面操作、独立安装工程、Demo V01、Win7 SP1 x86 客体及长时负载尚未对这份新源码执行，`releaseReady` 保持 false。宿主 preview.5 的 8 小时诊断与已准备的客体混合场景也仍分别按原载荷身份判定，不合并为新功能的通过结果。

独立验收消费者已增加真实公共任务处理器和页面进度/取消探针，x64 与 x86 均在 `-PrepareOnly` 路径完成安装 SDK 后的外部原生编译、Chrome 49 页面构建及运行载荷身份核对；该路径不启动桌面窗口。首次 x86 准备在 F 盘空间耗尽时失败，保留失败 suite，随后将 20 份旧验收 suite 共约 1.77 GB 完整移到 D 盘归档并重跑准备成功。新探针实际运行及客体回归待宿主长测结束后串行执行，不把编译成功计作功能通过。

外部消费者构建进一步加入固定 TypeScript 5.5.4 的严格检查，对复制到 suite 的 SDK 声明文件解析，并将检查配置/日志散列写入 Web 构建记录，由监督器运行前复核。x64 与 x86 新 suite 的检查、Chrome 49 构建、外部 C++ 编译与载荷准备均通过；尚未启动桌面场景。

Demo 开发树现用公开 `task.start` 接入一个有界计数任务，并以独立前缀路由公开任务的查询、订阅、取消和会话回收。x86/x64 Release 编译和 `demo.native.public-task` 无窗口适配器测试通过；React 页面构建和项目内固定 TypeScript 5.9.3 严格检查通过。原有 TypeScript 5.5.4 无法解析当前 Vite 插件声明中的字符串模块导出，故此处只升级 Demo Web 的编译器锁定版本，独立消费者仍按自身的 5.5.4 锁文件检查。Demo 真实 WebView 页面未运行，新公共任务不能据此宣称完成 Win7 或生产验收。

双端 benchmark 的历史落盘探针复核发现内容区不一致：Electron 为 1100×720，SOUI 页面实际为 1084×681。SOUI 最小宿主已改为按内容区创建窗口，并加入创建后的尺寸检查；页面结果前置校验器会拒绝旧探针的视口失配、短时长及空操作。新宿主尚未实际运行，校验器只检查页面侧基础条件；外部输入、物理呈现和进程树资源仍未采集，Electron 性能结论继续未测。

双架构 benchmark 宿主已在开发树重新编译；新页面结果校验器的三项测试通过。独立 Win32 进程树采样器还完成 x86/x64 编译，并在 x64 主机用真实父子进程、父进程先退出的后代、正常退出、时长超限和输出冲突验证原始 NDJSON、退出码及追踪行为。它未在正式 SOUI/Electron 轮次运行，物理呈现和配对报告也未完成，因此资源与流畅性仍未测。

外部 Win32 输入轨迹工具已完成 x86/x64 编译与拒绝无效 PID、场景、输出路径和模式的边界测试。它按相同内容区坐标通过系统输入队列注入点击、滚轮与 Unicode 键，并记录 QPC；正式宿主尚未执行该工具，S03 的 Unicode 注入也不等于中文 IME composition。页面事件/动作、外部输入轨迹及实际呈现三者尚未形成可用的配对轮次。

新增逐轮原始轨迹复核器，将外部输入与整进程树采样按 PID、创建时间及采样区间关联，拒绝短探针、采样长缺口、宿主异常退出和未采到真实 IME composition 的 S03。合成正常/故障轨迹单元测试通过；当前仍无真实宿主的双轨迹轮次，更无物理呈现或完整 Electron 配对数据，性能和生产门槛继续未测。

宿主 preview.5 八小时混合测试运行期间，2026-09-25 02:33 本机出现另一台独立 QEMU 虚拟机，约 4.9 GB 工作集；该时点以后的宿主资源趋势不能当作隔离环境结论。原始测试继续运行并在其外层目录保留外部负载观察记录。两个待执行的 Win7 验收启动器增加对 x86/x64 QEMU 进程的互斥检查；宿主终态及其他虚拟机退出前不启动该目标客体。

同一次测试又在 02:39 和 03:26 观察到另外的 x64 QEMU 启动，逐次进程/时间/内存观察已归档于[外部负载记录](../../verification/production-validation/preview5-mixed-soak-interference-2026-09-25/README.md)。这些观察不证明完整干扰区间，但足以使重叠时段的隔离资源与时延结论失效；原生操作和最终退出仍按原始长测结果单独判定。

后续在 04:41 和 04:52 又观察到两台独立 x64 QEMU，04:53 同时运行；其进程身份、启动时间和内存快照追加到同一[外部负载记录](../../verification/production-validation/preview5-mixed-soak-interference-2026-09-25/README.md)。本仓库在长测期间还进行了增量构建和审计；隔离资源/时延门槛须另起完整运行，本次只保留操作完成率与终态证据。

本地 `tools/Test.ps1` 的非桌面测试阶段现纳入页面配对与逐轮原始轨迹核验测试；连同已有 Web、验收监督器测试实际执行 57 项，56 项通过、1 项因未配置 x86 调试器而跳过，0 失败。该命令未启动桌面宿主，不能代替真实双端交互或客体验收。

V03 公共桌面服务的路径 Unicode 转换在源码审查中发现输出缓冲区少计了终止符，选择中文目录时可能得到错误文本；公共转换现使用精确输入长度和所需输出长度，标题/剪贴板写入拒绝无效 UTF-8 与嵌入 NUL。x86/x64 Release 的 SDK 静态库和新增原生边界测试已分别编译、通过；目录选择器真实 UI、Win7 客体、多屏与服务注册仍未验收。

V03 剪贴板进一步发现原实现以空窗口所有者打开剪贴板，按 Win32 规则会使 `SetClipboardData` 失败。公共服务现要求同进程有效窗口，Workbench 传入其宿主窗口；x86/x64 SDK 与 Workbench Release 重新编译，通过空/无效窗口及无效文本拒绝测试。实际中文写入/读回的可选测试已加入测试程序，须等宿主长测结束后在隔离的 Win7 x86 客体执行，尚未将该服务计为可用公共 Web 模块。

公共剪贴板写入现可由新项目在首次 bootstrap 前显式启用；模板项目包含实际点击调用及独立类型声明。x86/x64 的 SDK 契约和模板 Release 编译、SDK Web 构建、模板类型检查及 Web 构建均通过；完整非桌面测试 57 项 JavaScript 中 56 通过、1 项因缺 x86 调试器跳过，x86/x64 原生各 6 项通过。已在仓库外安装两个架构 SDK，独立消费者只通过安装包公开头文件与库启用剪贴板模块，分别编译和退出 0。契约确认 JSON 假手势不能替代原生手势、无手势和无窗口均有明确拒绝；尚未运行真实页面按钮与 Win7 客体读回，所以 V03-03 仍为“已实现待验收”。为恢复构建空间，将历史 `out/source-snapshot-03-final` 57,599 个文件、1,318,952,268 字节原样搬移到 `D:\SOUI-SDK-QEMU-validation\host-out-archive-20260925\source-snapshot-03-final`；源目录不再存在。该磁盘迁移发生在宿主 8 小时测试期间，相关时间段同样不用于隔离资源/时延结论。

公共窗口模块现可在新项目中显式启用，模板提供标题、最大化/还原和最小化按钮，并在 HWND 所属线程处理窗口命令。x86/x64 的 SDK 与模板 Release 编译、公共契约及桌面文字边界测试、SDK Web 构建和模板类型检查/构建通过；仓库外安装的两个架构 SDK 也由独立消费者启用模块、编译并退出 0。契约覆盖假手势、缺手势、缺少窗口和无效参数，但不替代真实 GUI 测试。当前开发树已将跨线程 `setTitle` 改为数字请求 ID 投递与 250 ms 有界确认；双架构 SDK、模板和 Demo 编译及各六项无窗口原生测试通过，窗口线程停滞回归仅完成编译，宿主长测期间不运行该 GUI 场景。若处理已开始，超时仍可能对应未知最终结果；关闭投递也不保证 Promise 在页面销毁前确认，因此 V03-01 保持待验收，细节见 [ADR-0010](../adr/0010-public-window-bridge.md)。

为目标系统实测另加入可选 `--desktop-roundtrip` 原生场景：在来宾创建真实 HWND，尝试公共标题、最小化、最大化/还原和关闭，并与剪贴板中文写入读回一起验证。x86/x64 已编译该场景；宿主长测和其他 QEMU 占用期间没有执行可选 GUI 路径，故尚无通过结论。

V03 目录选择器现作为可选公共 Web 模块注册，模板提供真实点击入口，返回用户选择的 UTF-8 路径或取消状态。SDK 原生服务在弹窗前核对同进程有效窗口和 STA，拒绝 MTA 调用；宿主 Bridge 工作线程原本已以 STA 初始化。x86/x64 SDK、模板 Release、Web 类型检查/构建及各六项无窗口 CTest 均通过，包括伪造手势、无效参数、缺失窗口和 MTA 拒绝。安装后的两个架构 SDK 还分别由仓库外独立消费者只用公开头文件和库编译、运行并退出 0，产物散列与 CTest 原始日志见[目录模块记录](../../verification/production-validation/directory-module-2026-09-25/README.md)。当前没有打开真实对话框，且模态调用会占用 Bridge 工作线程；Win7 x86、取消/中文路径、受限账户、导航/关窗竞态和 Demo 扫描令牌隔离仍待验收，V03 不据此放行。设计见 [ADR-0011](../adr/0011-public-directory-dialog.md)。

为目录模块另编译了 x86/x64 可选 `soui_directory_dialog_tests --cancel`：真实 HWND 由 UI 线程持有，独立 STA 线程经公共 `json_bridge` 打开 Shell 对话框，观察到对话框后发送关闭并核对取消返回；15 秒无终态时进程失败退出，防止客体脚本无限挂住。该可选 GUI 场景在宿主长测期间只完成编译，尚无实际运行证据；正向中文目录选择仍待模板和 Win7 客体验证。

上述 x86 可选程序已冻结为[Win7 SP1 x86 目录对话框客体短测](../../verification/production-validation/win7-sp1-x86-qemu-directory-2026-09-25/README.md)，传输散列、脚本语法、串口校验合成样本和宿主未结束时拒绝启动均已核对。真实客体尚未运行；它仅覆盖取消路径，不替代模板实际点击或正向中文路径验收。

V02 Workbench 现提供可从首页和菜单进入的数据交互页，复用 Electron 对照页的确定性万条数据生成器，组合虚拟滚动、中文输入事件、动效、窗口尺寸和公共 SDK 长任务入口。严格 TypeScript、legacy 与普通 Web 构建均退出 0，原始日志及产物散列见[编译预检](../../verification/production-validation/v02-workbench-preflight-2026-09-25/README.md)；宿主八小时长测期间没有执行另一场 GUI，因此实际滚动、IME、缩放、任务并行和 Win7 x86 仍待验收，不能把编译算作 V02 通过。

最新工作树上的[双架构非桌面回归](../../verification/production-validation/latest-unit-regression-2026-09-25/README.md)各有 9 项包/交付身份测试通过、59 项 Web/协议测试中 58 项通过且 1 项因调试器缺失跳过、6 项 CTest 通过，均为 0 失败。原生二进制沿用既有构建，本轮未重编译；未运行桌面或 Win7 客体，因此仅记录回归范围内的通过，不改变生产准入状态。

05:54 宿主八小时测试仍在执行第 1,300 轮之后的混合循环，另有外部 x64 QEMU 正运行，约 5.0 GB 工作集。[第七次外部负载观察](../../verification/production-validation/preview5-mixed-soak-interference-2026-09-25/external-load-observation-7.json)已保留精确进程身份和时间；该长测只能继续用于操作完成率与终态判定，隔离资源及性能门槛仍须重新运行。

[固定内核的源码声明材料清单](../../verification/production-validation/kernel-notice-inventory-2026-09-25/README.md)现补充定位到 libxml、libpng 和 Node c-ares 的仓内文本，同时确认导入的 libcurl、libjpeg、OTS 头文件指向本次导入源码缺少的配套许可文件。此发现只缩小再分发审查的待办范围；实际链接成员、缺失文本的精确来源、运行包声明和维护者签署仍未完成，二进制授权状态维持 `unverified`。

又从 curl 官方 7.61.1 标签和 IJG 官方 9a 压缩包[补录](../../verification/production-validation/kernel-notice-recovery-2026-09-25/README.md)两份原版配套文本，并保留原始散列及导入头文件的差异核对。OTS 等剩余声明、运行包实际链接组成及维护者审阅仍未完成，不能把源码文本补齐等同于二进制授权通过。

2026-09-25 通过仓库所有者的 GitHub CLI [查询](../../verification/production-validation/repository-visibility-2026-09-25/observation.json)到 `chenziyang110/soui-web-sdk` 的可见性仍为 `PRIVATE`。[首方许可状态](../../LICENSE.md)也尚未选定；若以后公开，公开可见仍不等于获得 SDK/模板/Demo 的通用复用或再分发许可。生产交付及对外采用结论须把这一所有者决策列为独立发布门槛，不能由第三方声明文本的补录代替。

导入的 OTS `ots.cc` 与官方 v5.0.0 仅有少量 Miniblink 适配差异；相应 [OTS 原版 `LICENSE` 已补录](../../verification/production-validation/kernel-notice-recovery-2026-09-25/README.md)。三份新增文本仍未完成整包授权审阅；`releaseReady=false`。

[第三方声明材料交付路径](../../verification/production-validation/license-delivery-2026-09-25/README.md)已补齐并在 preview.6 完整构建后复核：源码 SDK 包和安装项目、双架构 CMake 安装目录、Starter/Workbench 双架构输出均保留同一份声明与四份许可文本，32 份许可副本的原始 SHA-256 均一致。交付身份证明也纳入许可目录及声明文件，防止改动后沿用旧构建记录。此项测试没有运行应用或审完实际链接成员；运行包声明仍不完整，不能将其作为生产授权通过。

当前 preview.6 的 x86 Starter/Workbench [Win7 SP1 QEMU 短测输入](../../verification/production-validation/win7-sp1-x86-qemu-preview6-apps-2026-09-25/README.md)已冻结：从现行构建载荷重新压缩并解压核对 71/475 个文件，宿主运行来宾检查脚本确认两套法务文件哈希与中文路径，串口校验器合成成功/篡改样本自检通过。宿主八小时长测和其他 QEMU 尚占用，客体未执行；不能据此填写 preview.6 的 Win7 支持结论。

preview.6 [独立安装消费者预检](../../verification/production-validation/preview6-independent-consumer-2026-09-25/README.md)在 x86/x64 均通过：`find_package(SouiSdk)` 的外部控制台工程只链接安装后的公开库并退出 0；`Test-Acceptance -PrepareOnly` 又从本版源码独立编译 SDK、安装包、TypeScript 页面和桌面消费者，记录各自 32 文件安装快照与 75 文件载荷。两套安装库与各自 SDK-only 构建字节散列一致，第三方声明和四份许可进入安装目录。此项只通过构建与无窗口调用，不增加真实桌面、Win7 或长稳通过项；`releaseReady=false`。

[preview.5 x86 八小时混合生命周期](../../verification/production-validation/preview5-eight-hour-soak-2026-09-25/README.md)已经正式结束：单进程 1,690 轮、23,664 步、28,802,459 ms，监督器判定退出 0、无超时、操作完整。分时分析同时发现私有内存中位数由首小时约 64.1 MiB 升至第八小时约 136.7 MiB；运行与外部 QEMU 及本机编译交叠，资源和时延门槛均不能用本次数据放行。应定位增长来源，在隔离条件下用最终候选重跑；此结果也不能跨版本计入 preview.6。

preview.6 的[完整源码搬移](../../verification/production-validation/preview6-source-relocation-2026-09-25/README.md)已在无 Git 元数据的仓库外目录重新校验 30,190 个文件、打出一致的 408 文件 SDK 包，并由该包生成独立项目。完整源码与生成项目的 x86/x64 Release Web/原生编译均通过；搬移源码两架构非桌面测试通过。底层依赖未从搬移目录重编，两个项目均未在该目录启动 GUI，因此 `sourceDeliveryVerified` 仍为 false。

preview.6 [Win7 x86 应用短测 v3](../../verification/production-validation/win7-sp1-x86-qemu-preview6-apps-2026-09-25/README.md)已进入客体并验证两个 ZIP 的解包、数量和中文路径，但来宾 PowerShell 2 在许可散列校验器的 `SHA256Managed.Dispose()` 报错，三项应用 smoke 没有执行；串口校验器正确拒绝仅 14/24 文件的失败帧。原样本保留。[v4 独立客体](../../verification/production-validation/win7-sp1-x86-qemu-preview6-apps-v4-2026-09-25/README.md)只修正脚本清理调用，冻结应用载荷不变；实际 Win7 SP1 x86 短测的 24 个串口文件、法务材料散列及 Starter/Workbench 默认/React 三项启动和关闭均通过，退出码 0。该短测不覆盖真实交互、物理设备或长期资源门槛。

[Win7 SP1 x86 公共任务短测](../../verification/production-validation/win7-sp1-x86-qemu-public-task-2026-09-25/README.md)也已在冻结独立消费者上完成：14 个串口文件、系统/二进制身份、Bridge 三轮与 10 个完整步骤均通过，退出码 0；资源门槛仍未评估。随后[公共桌面服务短测 v1](../../verification/production-validation/win7-sp1-x86-qemu-desktop-2026-09-25/README.md)因测试 EXE 未附带 `MSVCP140.dll` 在进程加载前失败，客体退出码 `0xC0000135`；原样本保留。保持 EXE 字节不变、锁定 preview.6 应用本地 VC/UCRT 运行库的[v2 客体复测](../../verification/production-validation/win7-sp1-x86-qemu-desktop-v2-2026-09-25/README.md)已通过：真实 Win7 SP1 x86 上公共剪贴板中文写入读回和窗口标题中文往返退出 0，串口五份原始文件、系统/EXE 身份及哈希均通过独立校验。该短测仍不覆盖模板真实按钮、占用竞态、受限账户或完整 V03。

[窗口线程停滞 v2 短测](../../verification/production-validation/win7-sp1-x86-qemu-title-timeout-v2-2026-09-25/README.md)保持原候选 EXE 身份，额外锁定上述 52 个应用本地运行库，在实际 Win7 SP1 x86 QEMU 上验证超时返回、未执行命令撤销及恢复后的中文标题设置，客体退出 0。五份串口文件、系统/EXE 身份及哈希均通过校验；此结果不跨候选合并到 preview.6，也不放行模板页面或生产资源门槛。

[目录对话框取消 v2 短测](../../verification/production-validation/win7-sp1-x86-qemu-directory-v2-2026-09-25/README.md)同样保持原候选 EXE 身份、补入冻结的 52 个运行库，在真实 Win7 SP1 x86 QEMU 中打开并取消 Shell 目录对话框，退出 0。五份串口结果的系统/EXE 身份、逐文件散列及成功标记通过独立校验；快照客机已退出。它只证明公共取消路径，不涵盖模板真实点击、正向中文路径、关窗竞态或完整生产验收。

preview.8 [最终完整源码包的独立公共 SDK 页面消费者](../../verification/production-validation/win7-sp1-x86-qemu-preview8-public-task-2026-09-25/README.md)先在本机完成 x86 Release Bridge 三轮，再以同一 76 文件载荷进入 Win7 SP1 x86 QEMU。客体逐文件散列、进程退出 0、17 份串口文件、10 个完整生命周期步骤、3 轮资源采样以及页面的公开任务进度/取消、容量与错误恢复均通过独立核验；伪造内核散列和截断事件流的反例被拒绝。此结果不代表 Demo 页面真实操作或生产资源门槛，虚拟机历时也不作性能对标。

随后 [preview.8 Workbench Demo GUI 短测](../../verification/production-validation/win7-sp1-x86-qemu-preview8-demo-ui-2026-09-25/README.md)在另一轮 Win7 SP1 x86 QEMU 快照中完成：475 项运行载荷及五份许可逐文件校验、15 份串口结果和正常退出通过；真实页面显示 10,000 行数据，滚动到第 9,988 行附近，公开 SDK 任务分别呈现 `succeeded · 100%` 与 `cancelled · 5%`。GUI 状态由截图人工核对，不是自动交互回归或性能测量。初始落到首页的原因已定位为原测试命令缺少 `--page=data`，启动入口会清除单独继承的环境变量；已通过首页链接进入数据页，原脚本与偏离截图保留。物理 Win7、IME/DPI、长稳资源和同机 Electron 对照仍未关闭，`releaseReady=false`。
