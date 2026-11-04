# #31 剩余必要项只读评估

结论：**本轮新增产品短测建议数为 0**。现有实现没有发现足以要求修改 SDK 的具体反例；剩余核心缺口是带真实续期的浏览器集成证据，以及目录/媒体资源的长期夹具。当前公开客户端在实际约25分钟以前不会发出合法续期，现有同文档场景最低52分钟。单独补一次早续期拒绝或容量诊断，不能完成这项功能验收。按 Root 最新决定，不执行这种收益有限的短例，不改变时钟、TTL、leaseElapsedMs、保留 RPC 或资源 fence。真实超过30分钟、52分钟先行观察和8小时留到统一长批次。

本评估仅写本目录两份报告；未编辑 C:/sc07、构建、运行测试/产品、控制 VM、访问个人来宾文件、操作 Git 或遍历哈希整个缓存。Project Learning `learning start --command issue31-short-assessment` 返回 read_only=true、0条匹配；沿用任务树已有 cognition 的 missing_graph_store 边界，没有建立新图或进入 sp 工作流。

## 关闭条件与证据分层

Issue31快照及评论6211425167要求：同一文档保持 session/origin/能力集，原任务、订阅、目录与媒体资源跨续期保留；验证两层满载、确认丢失、撤销/提交交错、安装接入、两架构；真实时长和 Win7 独立执行。ADR0014限定生产授权30分钟、25分钟后合法轮换；模拟时钟不能替代真实经过时间。

| 现有项 | 实际证明及可复用范围 | 不能据此认定 |
|---|---|---|
| preview11续期预检 | 两架构14组原生续期契约、任务/Bridge budget等，以及安装 session-consumer 的20次模拟轮换。原输入和原结果保留 | 当前0.6二进制、真实浏览器、真实25/30分钟或8小时 |
| 原生 Demo续期契约 | 实际小目录扫描、原 root/scan/cursor，任务、订阅和媒体绑定的归属检查；媒体记录由测试 seam预置，未运行媒体 decoder/encoder | 真实媒体打开、解码/转换、浏览器中的目录/媒体跨轮换 |
| Web续期及 resolver测试 | 单飞、普通/清理满载、384+1 resolver、乱序/迟到/dispose等，使用受控端口/计时器；resolver直接读取生产脚本字面量 | mounted Host 与浏览器两层同时满载的真实集成 |
| preview8 Win7公共消费者 Bridge三轮 | 指定 preview8 输入实际验证256普通请求与清理槽、任务进度/取消；23,691ms，旧身份保留 | 后续新增的专用 renewal lane、当前0.6或跨续期保留 |
| 当前 ordinary-reload 两架构通过 | 相同当前 SDK/运行载荷的三次 reload、九步/七采样/27事件、自然退出 | capacity。`--mode=reload`产生`bridgeChecks=false`，没有执行容量分支 |
| 当前0.6限定 SDK CTest | 发布记录的7项x64集成、另外11项x86等确有通过 | 本次筛选名称不含 session-renewal/task-service/bridge-budget/Demo renewal，不能补成这些项目通过 |
| preview11 real-renewal v2 GUI | 两架构初始60秒未就绪，bootstrap/任务/真实续期均为0；失败继续保留 | 短例或真实续期成功，不能盲目重跑原失败载荷 |

报告 JSON给出精确文件散列和路径。这里的“复用”是保留已执行且身份匹配的范围，不把旧通过迁移到新 SDK。逐文件核对发现：session_renewal_tests、demo_session_renewal_tests、bridge_budget_tests、session-consumer/main、Web两份测试、session-renewal.ts、bridge_pending_script.h与preview11记录逐字节一致；但 permission_store.cpp/.h、task_service.cpp、client.ts、bridge_request_budget.h 已变。因此不能声称整个旧执行的全部输入仍与当前相同。保留原契约和模块未改部分的证据，**不为重复这些既有结果安排本轮复跑**；若后续修改相关主体，只补受影响验证。

## 当前夹具的准确缺口

`tests/acceptance-app/main.cpp:671`仅在 bridge/mixed/session启用 bridgeChecks，仅 session启用 session_checks。`main.cpp:323`只在 session_checks中 enable_session_renewal。普通 bridge的 Web分支（web/main.ts:151）为一个200ms delay、256次reflect和一次不存在任务的 cancel，要求恰好一个 E_BUSY及 E_TASK_NOT_FOUND。它没有同时占满128个清理槽，也没有填满384个普通/清理 resolver，更没有 renewal成功。

`sdk/web/src/client.ts:103`拒绝对保留续期方法的普通call；`session-renewal.ts:80`在真实续期窗口前直接返回现有lease。早调用 renewSession得到lease不是一次原生续期。现有 json_bridge公共构造器不提供时钟参数；不应为短测给它添加时钟/授权替代路径。

现有 session入口只接受实际 DurationMs≥3120000，默认28800000；窗口1保留真实 acceptance.session任务及一个真实进度订阅，至少两次 generation推进，窗口2/3只做背景任务及撤销。runAnchor核对 scope、任务状态、递增事件、pending/subscription清理及实际 handler退出，**没有目录或媒体方法/状态**。SessionReport现有 schema是固定字段且additionalProperties=false，未包含 directory root/scan/cursor、媒体文件绑定/任务/解码结果，单纯运行它52分钟也补不全 #31。

## 后续代码包及长测交接建议

1. **先准备夹具，不启动测量。** 保留独立 public acceptance的任务/订阅场景；另为实际 Workbench adapter准备明确选择的同文档混合资源 fixture，复用生产 adapter与现有公共接口，不把 Demo依赖放入 SDK。使用桌面真实准入一次建立目录和媒体资源：`desktop.dialog.pickDirectory` → `demo.scan.start/page`；`desktop.media.pickFile` → `media.convert.start/query`、`desktop.media.preview.start/query`。固定一个小目录和一份可实际解码、事先记录散列的短媒体；不能用seed-media记录替代。建立原任务、订阅、目录绑定、scan/cursor、媒体文件绑定和真实媒体结果后，只执行规定的新操作，SDK不重放原副作用。
2. **补真实证据的记录器/分析器。** 在真实running观察时只捕获一次lease generation并配对当前观察，沿用已有runAnchor的快照规则。通过实际端口应答旁路观察而不改写凭据，记录原subscribe ID、task ID、事件sequence及原资源的非敏感关联身份；原生通过已核验authorization_scope/session进行绑定确认。所有鉴权bearer、根/文件访问凭证不写日志。为资源建立、每次合法续期前后及撤销/清理增加有界字段/阶段，TS、C++ schema和session-validation同时更新；分析器拒绝资源重建、订阅重建、scope变化、sequence回退、目录cursor/媒体旧绑定失效及缺口。现有session验证代码未实现这些资源断言。
3. **纳入真实长批次一次执行。** 每架构冻结一次完整输入；先行最少52分钟覆盖两次真实续期，正式混合资源8小时使用28800000ms，不用短例冒充。第一次实际续期到期时，用真实公共SDK调用建立一次固定、可释放的满载批次：Host256普通+128清理+1专用续期，renderer384普通/清理+1专用续期；实际观测占用与完整应答，不能手工造pending项或只看最终成功。保留原30/25分钟时钟、续期最多三次/25秒总预算、普通方法同步发送、所选夹具原步骤/关闭限额及资源fence；延迟/容量失败按原值失败，清理后要求自然native0/Job0。其余续期只持续检查同一文档的原资源，不重复低价值满载。目录/媒体真实工作覆盖、52分钟和8小时结果分开标记；Win7另有实际目标机记录。

上述第三项是待实现/冻结的长批次设计，**不是已有可执行命令或本次通过声明**。当前 `tools/Test-Acceptance.ps1 -Architecture <arch> -Scenario session -DurationMs 3120000 -IdleMs 60000`仅是已有任务/订阅入口；不应将它标为完整混合资源验收。新夹具和分析器可先编写/静态审查，随后与其它尚缺长项统一执行，不为本报告另编译或补一次无助于关闭功能条件的短测。

建议后续代码包在`docs/testing/v0.6.0-long-run-handoff.md`的D07之后新增明确的“#31/V01-06同文档混合资源续期（待实现/未执行）”条目，列出上面的资源基线、两次真实generation、两层满载、真实媒体路径、原输入身份、逐架构/OS结果与失败保留，并明确现有session入口缺DIR/media。当前交接文档只有D07通用业务租期条件，未提供这套冻结的混合资源夹具。此缺口属于验收代码/证据，不是已定位的产品运行时bug。

可复用的当前0.6输入候选已在普通消费者实测记录内：两架构installed prefix、对应soui_sdk.lib与version.h、75文件运行载荷及node.dll指纹；JSON保留实际manifest中的元数据引用。这里不重新哈希大库、内核或整个缓存，也不复用旧EXE作为新夹具EXE。后续仅构建新增消费者/夹具资产，SDK/内核在精确匹配时复用。

Issue31继续开放；full current browser renewal、DIR/media retention、真实时长、Win7及生产资格均保持未通过。本报告没有修改这些状态。
