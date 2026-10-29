# Win7 当前 Workbench x86 载荷只读图

结论：应复用本轮已通过的 **新 x86 EXE f3da178e…3be4781 + 完整671文件载荷**。不能使用相同大小的已发布默认 EXE59216d…35e6aa，也不能换成历史 V11。宿主没有一个已组装好的 f3da完整目录：新编译目录只含 EXE。完整来源可由现有 manifest精确决定——670个文件来自封存的 `C:/sr06/out/x86/bin/Release/SouiWorkbench`，EXE单独使用新编译目录或已冻结 light-payload副本。

本图只读取任务宿主上的清单、控制源码、原始记录和文件元数据；未编译、复制、制作 ISO、启动/控制 VM、读取个人来宾文件、执行产品或重复现代 Windows 已通过短测。未重新哈希46.6MB运行树或大缓存。JSON给出各清单散列、完整671行的可确定映射规则及实际存在/大小检查结果。

## 已执行的精确身份

原始 manifest：`B/workbench-current/light-payload-0534dc25-c5fb-434c-a05f-19442ea332dd/payload/WB.manifest.json`，866965B，SHA256 `39d10f0236550188ebdbccf06f4cc4d7e54400293ce2b8fda93dc8fd64a50551`，schema=`soui.current-workbench-light-payload/v1`、version=`0.6.0`。

实际 stage为`B/current-decoded/wb-a1-stage/raw/Stage.actual.json`，schema=`soui.current-workbench-stage/v1`，681027B，SHA256 `078d018d3f4daf3ab0882fc1913eb28a5888e0956f381fa167f5435ec5ad9df2`。它在 Server任务 VM为两架构各逐文件复制671项，共1342项；原发行目录只作来源。

实际 run为`B/current-decoded/wb-a1-run/raw/Summary.actual.json`，schema=`soui.current-workbench-finite-result/v1`。x86 PID4864、creation FILETIME `134377427608582728`，原生/监督器0/0、Job自然归零；Owned24972ms，resident日志392条、68187B，SHA256 `31b29101bd354af67b4e8c34f0786bd8e73fcce1422493876d26d0bb1678eecf`。该结果只属于原Server环境，不认证 Win7。

| 来源/对象 | 精确身份 |
|---|---|
| 新 EXE | `C:/scw8-55faf327-x86/out/x86/bin/Release/SouiWorkbench/SouiWorkbench.exe`，1507328B，SHA256 `f3da178e5d78b2ccdf36a469d52788a7b729aab4ace3a69e0115ee2ca3be4781` |
| 同一 EXE冻结副本 | 上述light-payload的`payload/WB-SouiWorkbench-x86.exe`；该项在manifest中唯一`fromPayload:true` |
| SDK安装库（只复用原 pin） | `C:/sr06/out/release-install-x86-release/lib/Release/soui_sdk.lib`，37409002B，SHA256 `d4ec7df4957d4110f0ffbd339ff840770e81860faceda18be09e19fc099afafd` |
| version.h | 240B，SHA256 `74e0b9639567ae187c236628bec3b0a30e7ad113ee15f78b0cbd4283258f2cc0` |
| 内核 node.dll | 28318208B，SHA256 `dc8c37178f4bbd9260edc973d61af876cf8460178eaf89fa2994fdffc94abc9a`；来自原封存Workbench目录 |
| 不能替换使用的默认EXE | 1507328B，SHA256 `59216d2fb2a4c747a89d78923dd47015438ebc7c582092780c6afdc3bc35e6aa` |

运行闭集总量46604959B：根目录62文件、`fixture-data/`9文件、`resources/`600文件，总计671。原`.soui-runtime.json`仅列59个runtime依赖，schema=`soui.runtime/v1`、architecture=`x86`、backend=`miniblink`，不能把它的59项或“78文件”当完整应用载荷。另有`.soui-workbench-native-fixtures.json`（schema=`soui.workbench-native-fixture-owner/v1`）绑定测试fixture。临时`webview-smoke.log`是原读取器明确的可变日志，不纳入搬运闭集。

## 新 Win7 镜像的控制环境缺口

新镜像为 x64来源，实际启动后的OS build/SP、WOW64、计算机名、PowerShell版本、中文 IME均尚未核验。x86进程运行在Win7 x64/WOW64不等于Win7 SP1 x86系统资格。Server的机器名、缓存、F盘工具路径及既有stage证明不能迁移为新客体状态。

现有`WB-Guest.ps1/.cjs`不是可直接搬过去的单架构启动器：

- PS使用`ConvertFrom-Json`、`Get-FileHash`、`[type]::new`、泛型集合等，当前版本要求Windows PowerShell5语义。Win7默认环境是否满足未知。保留实际native LASTEXITCODE、环境还原、命名Mutex和fresh输出边界。
- CJS要求win32+x64、匹配实际hostname/ExpectedMachine、不同于preparationHost，固定schema/version及`['x64','x86']`两臂；还依赖`Array.at/findLast`等现代Node语义。Server工具pin为Node91380224B/`63c259c8…48088`、OwnedProcessRunner170496B/`f254dadd…dea83`；本任务没有Node24工具在Win7可加载的证据。不能假设Server的`C:/issues8`缓存存在或可用。
- manifest引用23个旧绝对reader路径（`F:/work/soui-issue96/...`）以及`C:/issues8/tools/...`；新客体没有这些路径。需要新客体专用、固定来源的单x86控制/清单适配，不能直接删除原manifest的x64臂来绕过原验证。
- OwnedProcessRunner是本机C++程序，源码以`_WIN32_WINNT=0x0601`为下限，CMake使用`MultiThreadedDLL`；不是可据此推断无需CRT的.NET程序。原使用者是x64工具进程，若继续用该工具需另带对应x64 CRT，不能用应用的x86 CRT替代。工具及其Win7实际加载/Job API结果仍要真实确认；失败保持失败，不能降为只等顶层PID。

因此可先完成的准备是：671项离线载荷选择、x64工具/CRT候选与Win7适用性前提、实际OS/PS/机器名的只读preflight方案、单架构原生Owned启动及原始结果读取/完整导出计划。若Node环境不适用，应复用既有受控原生runner并编写薄兼容驱动；本图没有编写或执行该驱动，也不承诺它已就绪。

## 一次必要的 Win7 应用短验

若目标是确认本轮已修复载荷在新Win7环境可装载并自然退出，最小已有行为是**只运行x86一次原resident有限场景**，原参数：`--resident --resident-smoke-test --react --page=data --resident-log=ABS_FRESH_LOG`；原inner ready45000ms、smoke180000ms、Owned200000ms+2000ms drain保持。运行目录为新组装的完整671项，输出单独全新目录；记录OS/SP/bitness/WOW64、实际加载模块、全文件输入/Guest拷贝身份、PID/出生时间、392项不是预设成功计数、真实完整resident协议、native0/Job0/无强清。只确认原读取器要求的实际标记，不把Server392条硬凑为Win7必须同样数量。

本只读图不安排第二次现代Windows复跑。若Root本轮的Win7目标已明确是滚轮、中文IME和公共任务并行，应在**一个事先冻结的Win7真实GUI短例**中一次收集这些动作与自然关闭，不先叠加多轮烟测；原resident自动脚本不提供这三种真实输入证据。此类新UI workload必须另有明确参数、期限和原始输入/任务/提交结果，不能把自动resident通过说成IME/滚轮通过。只有Win7启动后确认可用环境，Root才确定执行计划。

V11历史Win7 x86 resize/close结果继续绑定其旧60d729…源码和b6ca…构建，本次不重做、不改挂到0.6载荷。完整 Win7、IME/轮输入、公共任务并行和生产资格仍未关闭。
