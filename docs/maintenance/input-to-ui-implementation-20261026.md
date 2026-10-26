# 输入到可见反馈：实施台账与专人交接

基线 `44eff565bf9ab7778a8b9fdaa5403cc3b6f568ed`，设计契约见 [ADR-0122](../adr/0122-input-to-visible-feedback.md)。遵循维护者提供的《2026-10-26 输入到 UI 性能计划》第 10 节。此台账持续更新，不把局部 timer 修复视为完整交付。

## 当前身份与依赖

实时远端核查：PR104 → PR105 → PR107 → PR108 → PR109 → PR110 均 open/unmerged；PR107–110 为 Draft。PR110 head 与指定基线一致。后续 PR 明确标 base，保持 Draft。其他工作树和 DevTools 候选未修改。

## 工作包台账

| 包 | 需求与实现边界 | 回归/命令/证据 | 状态 |
| --- | --- | --- | --- |
| W00 | 同线程无效计时收益更正；continuation 生命周期/重建；legacy 单 deadline 重试；默认独立路径与 scheduler oracle | 生产代码与回归源码已集成并完成静态复核；[来源增量](../../third_party/manifests/kernel-input-ui-w00-source.json)；入口 scheduler_continuation、worker_lifetime、render_deadline_policy；未编译/未运行 | 代码完成待编译 |
| W01 | 统一 QPC/身份/关联；默认关闭有界 ring；scalar 指标；替换逐事件实验文件日志；同产品与 burst 新协议 | 公共数值契约冻结中；严格旧协议保留；无执行结果 | 实现中 |
| W02 | 类型化接纳；关键边沿保障/明确取消恢复；多 Host 公平；有界内部通知；输入语义守恒 | 容量/OOM/关闭/IME/状态恢复回归源码待交付 | 待 W00/W01 接口 |
| W03 | ready/future/idle；最早 deadline/票号/取消；有界压力恢复；单调时钟/Win7 fallback | 固定时钟/失败/重建/睡眠恢复回归源码待交付 | 待 W00/W01 接口 |
| W04 | future/ready 索引；50ms 老化与 tie-break；摘要；有界接纳/promotion/取消 | 随机 oracle 对照与关闭/自删除/引用回归源码待交付 | 待 W03 |
| W05 | 复用 pacer 和分数相位；窄版本能力/代际契约；资源与 pacing 分离；首次 dirty/窗口状态 | 可控时钟与刷新率/恢复/跨屏回归源码待交付 | 待 W01 接口 |
| W06 | 原因分类；资源完成合并通知；注册后重查；移除正常 1ms/8ms 轮询，完整 fence | 完成竞态/多 producer/迟到/取消/资源释放回归源码待交付 | 待 W05 |
| W07 | copy/锁/回调计量；mirror 消费者；有界 generation buffer lease；锁外绘制 | 像素/alpha/resize/DC/截图/重入/关闭回归源码待交付 | 待 W05/W06 |
| W08 | raster 成本诊断；可见优先/有界并发候选；项数/字节/scene/cache/预取预算 | 未 profile；不得把缺数据记为不采用证据 | 待设计/诊断/候选与数据 |
| W09 | layout/record/scene/snapshot/JSON/Bridge/分配诊断；按证据合并、切片、失效缓存及预算协调 | 未 profile；不无依据大改 layout | 待设计/诊断与数据 |
| W10 | WebView2 输出批次实耗时、转换/JSON/deque/广播诊断；COM/授权/撤销保持 | 未 profile；Runtime 外部内部调度另列边界 | 待设计/诊断与数据 |
| W11 | GPU 独立设计/诊断候选；device loss/生命周期/深度/软件回退 | 软件主线和 profile 尚缺，未启用 | 待数据，未关闭 |
| W12 | 精确需求→源码→回归源码→命令→证据→状态；原编码补丁/来源/注册；暂存区清单；分批 Draft PR | 由集成负责人随各包刷新；所有未执行保持未执行 | 持续集成中 |

## 所有权和验证边界

A 独占 SDK 输入/引擎与 vendor 任务调度，B 独占渲染/资源/帧和 buffer，C 独占 trace API/实现与测量协议。各人独立工作树；共享 cpp 的 hook 由所属 owner 接入。集成负责人维护 ADR 索引、全局注册、来源清单、分批 PR 与最终审查。

本阶段不编译、不运行测试/VM/量测；回归源码及命令交专人。仅使用源码/差异/接口/来源的静态检查，不能推导运行正确性或性能收益。最终清单从暂存区生成，固定预编译包保持历史身份。完整代表性验证矩阵、一次性执行清单与每包命令将在对应实现冻结后补齐，未齐前不称代码交付完成。

## W00 静态复核与一次性交接

- owner timer 在停止时撤销发布、清空窗口/间隔状态；native timer ID 不复用旧 owner，也不与保留 ID 相撞；SetTimer 失败取消已知 timer 并使间隔缓存失效。过期窗口、票号、关闭和 ticket 耗尽有回归源码。
- legacy 只保留一个可取消 deadline 任务；提前回调按真实剩余时间返回，dirty 在投递拒绝/OOM 时保留。resize generation 失效旧 payload，默认 independent 路由未改。首次代码审查修正了 Release 断言失效、票号回绕及长间隔兼容问题。
- `tests/native/vendor_lifetime/worker_lifetime/source-map.json` 保留精确 13 个源码、12 个复制片段、22 个 payload；更新三个实际全文件身份，原部分片段保持原字节。重新核查 PR110 已变更的 writer 后，将 LLVM observer 的三个固定 writer SHA 绑定到实际 LF 规范化源码，未放宽来源校验或用新散列覆盖历史导入记录。
- 早期元数据更新因 raw/LF writer pin 不匹配两次拒绝；随后检查了实际 writer、其规范化规则与基线差异才精确重绑。没有执行生成的夹具、测试或编译。六文件原字节 patch 只做 reverse `git apply --check`，不冒充完整 forward replay 或运行证据。
- 静态依赖审计 `npm --prefix sdk/web audit --json` 本轮退出 0，报告 0 项漏洞；范围仅该包的锁定依赖，不扩大到全部 vendor 或许可结论。

验证人员一次执行两个架构的 scheduler_continuation、worker_lifetime 和[legacy deadline](../../tests/native/render_deadline_policy/README.md)入口，失败只回到相应工作包。随后真实核查提前唤醒、投递拒绝、resize/close、legacy dirty 与默认 DIRECT_HWND/independent 两条路径。当前 dormant `#if 0` custom timerqueue 分支未获安全资格，W03 将替换；不得在 W00 单独打开。W00 不证明输入完整链路或性能目标通过，后续 W01–W12 继续推进。
