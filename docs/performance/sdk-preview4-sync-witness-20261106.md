# SOUI Web SDK：同步输入旁证的限定检查

**本轮完成测量夹具的单次会话修复、实际编译和冷初始化检查；输入延迟与实际帧率仍未验收。** 两次修正后的启动都因窗口控制失败而未进入输入采样。没有新的 FPS、输入延迟、收益或 Electron 对齐结论。

本报告接续[IME 消息边界检查](sdk-preview4-ime-boundary-20261106.md)，设计见[ADR-0166](../adr/0166-synchronous-input-dispatch-witness.md)。机器记录见[continuation-r35.json](../../verification/performance/sdk-input-frame-20261105/continuation-r35.json)。原件相对于本地归档 `PR129-performance-20261104/repair-r35-sync-input-witness-r1/`；旧失败与原始日志均保留。

## 目的和实现边界

既有测量会收到同坐标的真实重复 DOM 回调，仅按坐标、相邻时间或忽略 tag0，无法证明画面反馈对应哪个原生输入。新夹具在真实 `wkeFireMouseEvent` 的同步 DOM 回调内，只读同一 EXE 中已有的 `input_frame_trace::current_context`，保留完整 input ID 与代际，再精确关联 trace 的 T1 注入标签。页面为每个反馈保留独立可见槽，重复与未知回调全部记录。

原型只修改独立测试消费者和协议，不改 SDK、公共 ABI、默认内核或运行包。它需要匹配 SDK 的私有诊断头，额外 getter 与详细 trace 的测量成本尚未量化，因此不是普通应用接入方式，也不是正式性能基线。64个反馈槽不等于64帧；捕获结束上界不等于精确首次可见或物理光子延迟。

## 实际执行结果

冻结载荷为 `0.7.0-preview.4`、x64 Release，SDK build ID `18e47fa2-69df-4530-b1f9-365f6b2aec28`、SDK source SHA `e1d2dd8b7699930f678e21a77772b8fa4e371f5b67a53937d98470ee1659e87c`，默认 node.dll SHA `56d460c0b4136d85d3fcb8d4e4f00c19a04ff56532228031b55368742ef54308`。

首版实际运行 `a881df0c74a114935b4283db84864c17` 在5.932秒自然退出：Host2、入口1、Job0、无强制结束。测试页面重复 bootstrap，违反同文档只能有一个活动会话的契约，getter未安装、输入未启动。这是夹具缺陷，未用旧失败给 SDK 性能下结论。

修正只建立一次真实会话，用凭据响应和安装通知的双就绪门汇合，不伪造凭据或重置会话。5项新增握手状态测试实际通过；新消费者的两个 C++ 编译单元在 MSVC19.44/v143、Windows SDK19041 下实际编译链接成功，58.447秒、自然0、Job0。输入工具复用首版已构建产物，没有重编 SDK 或内核。

| 修正后实际 run | 冷初始化 | SDK/vendor 详细 trace 点数 | 输入器 | 自然终态 |
|---|---|---|---|---|
| `0a092a1828a0d749af3f22fe3200e7a6` | 单次 bootstrap、receipt4、getter/cold0、page ready通过 | 4,812 / 20,741，完整、loss0 | 未启动 | Host2 / 入口1，36.970秒，Job0 |
| `9c80743cbc86810371aae3d39faf1d2f` | 单次 bootstrap、receipt4、getter/cold0、page ready通过 | 4,752 / 20,538，完整、loss0 | 未启动 | Host2 / 入口1，36.462秒，Job0 |

两轮实际冷调用均验证 cookie、上下文 ownership 与 Engine owner，返回0；分别精确对应 SDK task8/task9 的 point12。它们证明冷初始化链已执行，不能代替正输入关联或由正输入触发的独立延后负对照。SDK仍有正常 UI 输入阶段记录；“未启动输入器”不表示所有原生输入事件为0。

窗口工具在目标窗口选择及 `get_window` 成功后报 `failed to activate captured window`。首轮重新读取状态又报 `foreground window did not report a process id`；后轮重新选择时目标已关闭。没有发布 GO，原30秒截止届满后通过既有结束事件请求正常关闭；两轮都未强制结束。操作系统或工具层的精确根因未知，不能据此声称桌面锁定、SDK卡死或发生产品回归。后轮失败已独立核查；不在相同条件下继续机械重试。

## 交付和剩余验收

修正后 Host 为1,281,536字节，SHA `c4320c14cc641b904e920bd6a1eff918cb002a560e430837b4a0fde9a9339aaa`；复用输入工具131,072字节，SHA `e54cfffcded416431c991f22706a0e4c354221cc2338b9a5827db76324d66c7c`。Host相对本轮首版增加512字节，仅属于诊断程序，不计作产品 runtime 增长。原有 SDK/runtime/ZIP 体积证据保持原身份，见[交付复验](sdk-preview4-delivery-20261105.md)。

完整实现、源文件清单、构建原件与独立审查在归档的 `recovery-single-bootstrap-r2/`；输入工具来源在 `project/benchmarks/protocol/`。开发交接说明在 `handoff-r1/README.md`。已消费的入口不能覆盖重跑；恢复真实可操作桌面后，应以同一已构建载荷、新run与新结果目录，按原期限进行一次受影响的64输入检查，不再重复编译或无关矩阵。

后续必须补齐：正输入的 T1→同步 DOM ID→首次反馈槽→有效桌面捕获关系、独立 point12 负对照、测量成本、同负载配对输入尾延迟与有效帧间隔/长帧，以及 CPU/完整内存和适用 DPI/多屏门槛。不得用本轮 cold0、完整 trace、rAF 即时读数或历史包体积替代。

默认共享预算0、输出准备关闭、既有采用决策不变。`performanceVerified=false`、`inputLatencyVerified=false`、`releaseReady=false`、`wholeGoalComplete=false`；PR129保持 draft，不合并、不发布。
