# preview.6 owner loop：实现与实际验证进度

当前是未发布的 `0.7.0-preview.6` 源码候选。SDK自有循环、WebView2普通输出接线和三个普通应用模板已实现；SDK本体的正常CMake编译/安装及473项文件核验已在x64和x86实际完成。x64正常安装消费者的Core37项和真实SOUI3项通过，三模板main已编译为OBJECT，保留原核验器失败和历史输入闭包限制。**没有完成输入延迟或实际帧率验收，不声称对齐Electron或生产就绪。**

## 改动及依据

[SDK5实际诊断](sdk-preview5-integration-acceptance-20261106.md)最终接收全部3125个正标签，却因tag0、晚到及ACK500失败而不合格；load期间paint/timer最大间隔约29秒。有限inflight和每次短pump不足以排除请求/回复持续补位，使posted队列长期非空的反例。该分析不能证明SDK代码的唯一因果。

本候选把普通输出continuation接入公共 `windows_owner_loop`：仅无过滤Peek确认当前没有可用系统消息时继续一个adapter的原有界pump。私有poke只确认排队责任；普通dispatch后不立刻补发普通输出。紧急控制、原lane FIFO/优先级、admission credit、deadline及owner STA保持原契约。持续业务idle与SDK服务交替，临时modal返回可恢复普通服务，资源或pump故障保留永久legacy回退及失败状态。设计、边界和回退见[ADR-0169](../adr/0169-sdk-owned-owner-loop-integration.md)与[公共API](../api.md)。

普通SOUI模板使用 `soui_owner_application`，其factory在替换前预构造主loop；现代Win32和auto模板也使用公开循环，保留DevTools翻译及原早退关闭/Runtime排空路径。未接入的旧应用保留原通知。Core不依赖SOUI、不引入新DLL，也没有降低页面负载或改Scratch业务。

M01测量源码现位于 `tests/native/webview2_owner_fairness/`，显式选择legacy/sdk模式并共用早期MSG/extraInfo观察与公开 `host.show()` 检查。旧SDK5诊断没有显式show，记录过hidden；这不是SDK默认隐藏或唯一根因证明。新旧仪表有差量，后续对照必须两侧使用同一新版测量源，不能把R54直接当候选的匹配基线。

## 已实际执行的有限检查

| 项目 | 实际结果 | 范围 |
|---|---|---|
| Core源码构建 | v14314.44.35207、SDK19041、C++17、x64 Release，自然0，62.182秒 | 实际Core实现加两份测试，共3 TU；另有正常CMake compiler/ABI探测 |
| Win32 policy合同 | 25/25 | worker并发、pending/queued、旧token/timer、modal、取消失败、临时/永久回退 |
| 公共Core loop合同 | 12/12 | 排队消息、真实OS timer、持续idle、嵌套、异常、错线程与实际弱owner寿命 |
| CTest/监督收尾 | 1/1，通过；监督561ms；Job0、无强清 | 原15秒及2秒drain，未延长 |
| preview.6版本交付小合同 | 2/2 | preview.5元组保持不可变、preview.6精确元组 |
| Web SDK、React、Vue构建 | 三组实际0 | 使用本机已安装依赖分别执行正常TypeScript构建，没有重跑npm ci |
| Web客户端合同 | 31/31，无skip | scope、容量、超时、事件、dispose及连接边界；不等于原生GUI验收 |
| SDK包管理小验 | 16/16 | 临时fixture的所有权、完整性、升级/回退、路径与IO恢复；不代表当前SDK6包已交付 |

构建PID86280/birth`134384685945421770`，测试监督PID81400/birth`134384686568289564`。源输入SHA`3e1e98759e62ddd7d42e73ac0dcd6ab420594eeb99f5e5e9baba24ce35e31268`；测试EXE140288 B，SHA`7b3dae93c4d932b62b3508118c0a5bff30f0f30736363d35cb2fee711938fdf6`。这不是应用体积对照，不能与旧私有原型EXE相减当SDK增长。

这些测试使用隐藏或message-only窗口，不激活桌面、不发送输入、不启动浏览器。没有把源码独立构建称作正常安装SDK、SOUI模板或WebView2运行通过；没有重复旧20例原型执行来扩大计数。

## 独立源码审查与原件

原件相对本任务本地验收包定位，绝对根目录由持久进度记录提供。

| 原件 | SHA-256 |
|---|---|
| `W02-sdk-owner-loop-next-r1/source-and-contract-review-r1.md` | `4c504f73d21a8288a4bf9a1832c4bc36564251a771608d6e6b9705f42a703fb0` |
| `W02-sdk-owner-loop-next-r1/integration-source-review-r1.md` | `f13f06592961d62bacb82686d91d214de25df163c99c38e6164ed160472551bb` |
| `W02-sdk-owner-loop-next-r1/integration-unique-ownership-review-r1.md` | `d1c615587dc32ecd0177ae8c2cf6ed2c49c04fa4bca3269ec30f38dd5873cec0` |
| `W02-sdk-owner-loop-next-r1/M01-repository-consumer-source-review-r1.md` | `9f8d971cd7d721ef3ca6a8d1dae8cc2258a7820d38ef131abc38c33799b8cd5b` |
| `W02-sdk-owner-loop-next-r1/SOUI-consumer-source-review-r1.md` | `630c30df3002f9a7efcc29a173f4a19e0d2ee6d6b7ef2f4222bb95d170980eed` |
| `W02-sdk-owner-loop-next-r1/templates-owner-loop-source-review-r1.md` | `7defb9284d8c425cf5ea6d4269636feab3b431fdb440ae0af34e6bb4557d75f6` |
| `W02-sdk6-contract-build-r1/build-r1/build-summary.json` | `11b0959ef6728fc7862403d46a708d7424637ab8e3c2cdb25ed5f6bfe3b844ac` |
| `W02-sdk6-contract-build-r1/build-r1/actual-independent-review-r1.json` | `53c7599ca99f148be77675e73289ec9990241bdfb27482531afcfd20b8f29ffa` |

Core、接线与测量消费者源码分别经非实现作者审查。发现的持续idle饿死SDK和SOUI消息记录缺口已修正。实际构建和37项合同也经非作者独立核对：真实CL读取、3个obj、PE、原始监督终态和两个编译助手自然退出均一致；源码审查与实际审查分开记录。

## 正常SDK本体构建与核验器失败

R57直接使用仓库正常SDK CMake目标与安装规则，实际完成34个Core TU和1个WebView2 TU，配置、编译和安装命令均0。主监督PID51632/birth`134384694386605975`，131.097秒自然0、Job0、无强清；两阶段编译助手均在原30秒界内自然0，600秒总监督和2秒drain保持。不构建ALL、zlib、SOUI或MiniBlink内核，不执行Web重建或GUI。

本轮外部冻结构建UUID为 `5daa8ffe-54e2-403b-8de4-325fd50398f2`，不是canonical `Build.ps1/Build-Evidence` ID。源码输入SHA`25121e81a1554b9cd8f9c1f45f03ecfa54854535c5c9a542de3873ce10675227`。Core archive57,947,532 B、SHA`753b773ad0fec558a570ce14dac0494e286e689f0feb3af5a9040942ffb46b01`；WebView2 archive8,466,996 B、SHA`9cd697c08bb9cd39b11b7d5fc3402761a53e8e798794d27ae84e710d85b5d127`。它们还不是最终应用EXE或runtime的体积对照。

原外层reader仍返回1：对许可说明只预期了路径替换，未计CMake在本机写入的LF→CRLF转换。实际源8108 B，生成/安装均8096 B；仅5处许可路径替换再转换48个换行，严格等于生成文件，生成文件与安装文件逐字一致。原失败不改写，后续采用单独只读核验，不为核验器错误重编或重装。

实际CL读取还发现3个MiniBlink头未列入R57预先冻结集合：`wke.h`、`input_ui_api.h`、`wke_cursor_broker.h`。当前字节和既有身份可单独核实，但不能改写成这3个文件在原375项中已预先冻结；完整历史读取闭包保留该限制。生成preload也须从已冻结JS和模板单独核验。上述边界不会被正常命令0或473个安装文件、0重复清单的旁证掩盖，最终只读收据及独审另行记录。

单独只读恢复已实际0，原SDK字节、原失败和输入未修改：53个公开头、三库、配置及法律材料的473文件闭集通过，0重复安装行；preload由冻结JS和模板按明确转换精确重建匹配。三个后补观测头与既有不可变源码副本同字节，仅是历史身份旁证，不冒充编译瞬间字节追踪。恢复收据SHA`c17ec5f60fd4115e4e4ae2800c8cd62716dc9efcec142a1cae22dcd50541a1f2`；独立实际审查SHA`14b1a60a530d0dfc527d2f5ac972bd5d834d7c5b1209b115afdec5ba0ca35cb6`。

限定同工具链/x64 Release静态安装，SDK5的80,253,090 B到SDK6的80,831,171 B，增加578,081 B，约0.7203%。包含新公开头、版本与静态归档差异，不能把全部增量归因某一处调度代码；不等于最终EXE、DLL、runtime或ZIP增量。

## 剩余门槛

R58实际使用正常x64安装SDK链接独立消费者：Core37项、SOUI3项均通过，7个显式target共9 TU完成，包含同仪表M01 Host、输入器和三个真实模板main OBJECT。Core合同与前述源码独立构建是同一37项，不能累加成74个独立用例。SOUI3项执行真实SOUI应用、消息filter/idle、嵌套loop和退出清理；没有执行M01输入、模板EXE链接或启动。三个模板的OBJECT检查也不代表AutoHost库链接通过。构建、两次CTest和编译助手均自然0、Job0，无强清。收据`R58-installed-sdk6-consumers-r1/build-r1/build-summary.json` SHA`b7c0207f1bc6f5db1dcf8aadb4f745dacc655a5b459a4ba19b49a06f08952742`；独立实际审查SHA`c71af1aabb76e7dad02fc5a83313161deaded7d69f71798cfd64a8c53d298869`。R57历史输入冻结限制随消费者保留。

R59实际完成正常x86 SDK的34个Core TU和1个WebView2 TU、安装及473项闭集核验，配置/编译/安装与监督均自然0、Job0，原时限未变。三库及实际对象均为x86；本轮预先冻结了R57漏列的三个MiniBlink头，但不追认R57历史冻结。构建UUID`ae790d08-1710-49d1-a8f2-8498cfd284cc`仍为外部冻结身份，canonical BuildEvidence ID未知。收据`R59-sdk6-x86-normal-install-r1/build-r1/build-summary.json` SHA`c4600557488faf89fc26f6cac4e5238a9cae1866bd20df4fc334f318cabe197d`；独立实际审查SHA`b29497b70ec5929c526034a16adcb706f0779ef5ed9923d7183be59e7ef61825`。该项不代表x86消费者或GUI运行通过。

R61随后实际完成x86正常安装消费者：Core37项与SOUI3项均通过，5个显式target共6 TU，包含三模板OBJECT。两个EXE及6个对象为x86，实际CL读取的168个本地源/头均已预先冻结；使用正常R59 SDK与x86 SOUI导入库。构建79.602秒、两次CTest监督504/211ms均自然0、Job0；原15秒测试、600秒构建和2秒drain未延长。收据`R61-installed-sdk6-x86-consumers-r1/build-r1/build-summary.json` SHA`639f972c0550b6d1c0a876ff100f61593db1275eb01c95490c9f73001dac4ae9`；独立实际审查SHA`6eb4adacd931172c6dc353435b6720e5e1b396da43336d961a176aaceda15d63`。它不补齐模板EXE链接、启动或M01运行资格。

R60已另从不可变正常安装SDK5链接同新版仪表的legacy Host，只编译Host两TU；复用R58无SDK库链接的同一输入器，两侧ActionJson字节一致。实际链接只读取SDK5的三库，无SDK6混入。构建67.372秒自然0、Job0，EXE926,720 B；收据SHA`148c39975bb93bc92b8494b61655399888ab8fb8ea05d604cf76fccc9b468f8d`，独立实际审查SHA`2eb8ee2c9d1677996e800b8d724d5ccf701621565e2dc7f361c9b36fc76bc2fa`。SDK6对应M01 EXE967,680 B，增加40,960 B，约4.42%；这是同仪表诊断程序的有限对照，包含版本与链接差异，不是正式模板、完整runtime或ZIP增量。

当前继续三模板必要链接/启动及同仪表真实输入对照；不重复未改动内核或Electron。不能用早期hidden页面诊断替代本轮匹配基线。

R62已冻结同仪表SDK5/legacy与SDK6/sdk两个入口，保留全部3125输入、30秒负载、ACK500、tag0否决、运行时身份和原绝对时限。reader的8个合成合同通过；双入口源、构建绑定、真实请求及资源槽均已事前复核。实际第一轮在资源槽取得前返回`mutex-busy`：辅助进程85128自然退出1、Job0、无强清，尚未执行CIM门、开窗、Host、输入或GO。后续只读CIM另证实其他工作区当时存在真实编译；没有终止它。该次是开窗前资源拒绝，没有产生新输入或帧率数据，不能算候选失败或通过。

输入尾延迟、有效画面间隔、长帧、完整CPU/内存、成功吞吐与必要交互仍须合格样本；物理显示不可观测时保持unknown。EXE、DLL、完整runtime及ZIP增量也须分别绑定实际交付，不用旧SDK5静态库增量补齐。MiniBlink既有资格缺口不因这次WebView2循环改动而关闭。`performanceVerified=false`、`inputLatencyVerified=false`、`releaseReady=false`、`wholeGoalComplete=false`。
