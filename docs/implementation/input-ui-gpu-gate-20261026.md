# W11 GPU 候选交接与采用门槛

日期：2026-10-26。状态：设计/诊断候选，默认不启用、待软件 profile。本独立W11文档包不新增GPU后端实现，继承软件栈的数值诊断；未编译/测试/benchmark/VM；不代表本轮已经实现硬件后端。决策见 ADR-0126。

## 已核查入口与诊断缺口

| 入口 | 当前源码能力 | 后续需要的证据/实现 |
|---|---|---|
| `GpuSkiaSurface::initialize/createFramebuffer` | WGL/Ganesh、单 texture/FBO，软件 GL 识别、首表面清理 | actual owner/context代际；冷初始化失败清理；全部尺寸/字节边界 |
| `beginFrame/flush/present` | 修复 current context、flush、blit/quad、SwapBuffers | 逐帧类型化失败、GPU/平台 fence、两在途和驱动隐藏队列深度 |
| `resize/reset` | 重建 framebuffer；尝试 owner context 释放与 DC 回收 | device/context lost、跨屏/DPI、不能 make-current、cleanup拒投与关闭回执 |
| `readback` | glReadPixels 到复用 vector，翻转拷回 CPU canvas | 显式消费者/一请求/字节上限、frame lease、alpha/通道/快照语义 |
| `LayerTreeHost::paintToMemoryCanvasInCompositeThread/drawFrameInCompositeThread` | 实验分支绘制/读回、帧末直接呈现 | callback/锁与 owner lifetime；失败时 dirty/事务恢复；不将软件lease资格推广GPU |
| `shutdownCompositeThread/~LayerTreeHost` | 合成线程删除 surface 后 shutdown，仍有既有等待路径 | 有界恰好一次 cleanup/late通知/任务析构、真实join、不得UI强删或假drained |

当前独立GPU/交接PR以其实际head及执行前 `git rev-parse HEAD` 为准；生产源码继承Draft PR122，根 `source-manifest.json` 与各raw delta固定身份。表中源码能力按最终软件树符号重新静态核对，不用旧worker head代替最终候选。历史 ADR-0005/GPU 生命周期失败与资源记录保留原身份，不当作本候选测量结果。现有实验 GPU callback 的复合锁/重入边界、设备恢复和关闭资格仍缺。

## 两阶段交接

1. **软件 profile 后选择候选**：冻结 SDK/内核/页面/工具及同一物理目标、窗口、DPI/刷新率、配置和载荷；先做工具扰动预检与相关正确性。分别给出 CPU raster/paint/copy、资源等待、锁/callback、布局/JS和提交阻塞的逐输入关键路径证据，不把各阶段 P95 相加，不将并行 worker CPU 当成串行墙钟。
2. **独立 GPU 实现/资格**：软件仍为基线，另开 Draft PR 和默认关闭开关。先落 owner/代际、两槽与字节预算、fence/lease、软件恢复与关闭，再做有效呈现/资源对照；不能先强开 GPU 追数字再补生命周期。

没有第一阶段数据时，继续软件独立工作，交设计/诊断入口和本表“待数据”状态；不得写无数据证明不采用。软件已经满足预算且 GPU 固定成本/恢复风险超过经测收益时，可以有证据地不采用，但仍记录设备/场景范围、成本模型和重新触发条件。

## 候选状态、容量与守恒检查

| 状态/资源 | 必须保持的限制/动作 |
|---|---|
| off/disabled | 不分配 context/buffer、不开线程/周期唤醒；模板/默认配置保留软件 |
| ready | 一个 compositor owner；GPU pixel lease最多2、总在途提交最多2；pending视觉描述最多1 |
| 视觉替代 | 仅未发布可替代状态；dirty并集、输入覆盖分段、scene/action及取消回执守恒；不改变DOM事件频率 |
| 字节 | 每pixel lease候选上限64MiB×2，CPUreadback64MiB×1；Skia/tile/cache另有冻结预算；溢出/过大尺寸直接软件 |
| resource/present受阻 | 保持完整action/scroll fence；等待实际通知/完成，不1ms/零间隔轮询，不提交无界帧 |
| lost/resize/DPI | 阻止新GPU接纳、旧代通知失效；旧lease仍受保护；软件资源真实重建、首帧全量，再恢复进度 |
| readback | 显式消费者、同frame/size/device代际、一请求、lease存活；失败不返回过期DC/像素 |
| close | owner停止/取消/完成/释放恰好一次；拒投保留cleanup intent；句柄、模块和surface不在使用中强删 |

`flush/SwapBuffers/glReadPixels/recordPresented` 各自意义独立。应用两对象并不证明驱动显示队列深度为二；缺少可观察 fence/平台 ownership 的路径不能获得有界低延迟资格。若需要新 presenter，能力探测、版本边界、Win7/无GPU回退和许可来源独立记录，不冒称现有 WGL 实验已实现。

## 需要返回的数据

- 同机同载荷软件与 GPU 的 run/process/view/doc/geometry/device/context、input/task/frame/action 身份，整数 QPC frequency，真实阶段与未知阶段、trace丢失/扰动、有效/无效轮次。
- T1→T2、实际关联 T1→T6、校准注入区间→桌面观察、连续内容间隔与最老/最新输入年龄；真实可见样本的数量/P95/P99、每轮尾部不确定性。GPU完成不填T7，桌面图像不填T8。
- CPU与GPU可解释工作量、lock/callback尾部、wake/s、copies/bytes、raster bytes、在途/最老帧、普通/USER/GDI句柄、CPU内存/可测GPU驻留与后台吞吐。不可观察项标未知，不写零。
- 故障与恢复：初始化各阶段失败、无GPU/软件GL、make-current/提交/读回失败、设备丢失、迟到/重复fence、两槽耗尽、callback重入、resize/DPI/透明、截图/DC消费者、导航/close与cleanup拒投。
- 源码/构建/DLL/EXE/页面/工具/驱动/适配器/配置散列及真实 Runtime 版本；旧预编译 DLL 不算新候选。

## 采用、停止与重开

采用前冻结代表性60Hz和一个真实高刷模式、相关x86/x64生命周期与目标平台兼容范围；对照至少五个有效交替配对，相关输入总数及实际可见阶段样本分开报告。条件采样/丢记录不得变完整因果链；GPU专用诊断不能改变正式收益轮次的负载。

通过所有正确性、两在途/内存、恢复与关闭门槛后，才讨论达到软件主线同一输入/帧预算及已确认卡顿的30%改善目标；关键场景超过 max(5%,噪声界) 的退化必须解释和修复。CPU/内存/能耗/后台吞吐预算在首轮profile冻结，吞吐更高不抵消输入显示更旧。

输入状态错误、动作/coverage/fence/ACK不守恒、露白/错帧、死锁/UAF、持续资源增长或回退失效，立即关闭 GPU 候选并保留失败证据。相同环境原因两次无效停止该场景；不是无限重试或强删关闭检查。专人只重跑受改动影响集合，软件主线不因待GPU数据阻塞交付。

重新触发采用评估的条件必须具体：新的软件关键路径 profile 显示可GPU化瓶颈、可验证的 bounded-present/fence 能力、已定位并修复的恢复/资源问题，或新的受控目标设备。当前待办是上述证据与独立硬件实现；缺数据不是不采用证据。

后续采用还必须遵循[专人交接](input-ui-specialist-handoff-20261026.md)的统一至少5个有效交替pairs、每候选至少10000条可判定输入、实际stage/visible样本分别计数，以及unknown/无效轮次/两次同因失败停止规则；不能以5对小样本或GPU吞吐替代输入尾延迟与资源资格。
