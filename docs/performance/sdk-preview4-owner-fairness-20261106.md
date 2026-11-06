# preview.4：WebView2 持续 owner 负载入口验收

**本轮未进入负载或输入测量，W02公平性与性能资格继续开放。** 预定输入发送为0，不能产生输入延迟、成功吞吐或资源负载结论。没有更改SDK、内核或默认参数，也没有将失败归因于SDK。

## 夹具与已执行构建

原冻结SDK `0.7.0-preview.4`、x64 Release、build `18e47fa2-69df-4530-b1f9-365f6b2aec28`；固定WebView2 Runtime `156.0.4314.58`。新独立profile `r41-default-native-strip-v1` 使用1100×792 owner及1100×72原生child strip，同一STA处理真实Bridge回复。原私有非输入桌面prototype没有修改或继承为真实输入资格。

预先冻结的工作为30秒4KiB、7条echo在途及最多1条控制；全部3125次/125Hz、25秒输入必须落在原负载窗口内。原生LoadActive握手及29秒单次完成fence约束跨时钟因果，不以延迟phase消息或裁取子集冒充完整重叠。冷/最终marker、完整账本、前台/几何/held身份、Esc停止、原deadline及自然关闭要求均保留。

v14314.44/Windows SDK19041实际局部构建自然退出0、79.998秒，Host两个TU、输入器一个TU及纯策略合同一个TU；14项纯合同实际通过、62ms。Host901,632 B、SHA `00fecb76fcd51c2379cd65e68b7fe03a6bb17541a0f5a27dc6347d197eb65e12`；输入器113,664 B、SHA `d16cabf70ccc84bd4bac9977b4711663502404ccd4ef1f50ae83bda60b732b9d`。SDK、内核及产品包没有重新构建。Node/页面定向检查保留各自首轮与修复后结果，不把曾经失败的测试harness改写为一次最终全组通过。

[源码快照](../../verification/performance/sdk-input-frame-20261105/diagnostic-source-r41/README.md)保存17个原字节文件、116,952 B，清单SHA `a74707aeb47b6882dbff3615f44c7bd195ea79e67785a8d96a828bd9bad691bb`。快照制作时尚未运行窗口，新地点没有构建或继承新产物资格。

## 启动前修正

第一份冻结入口在原生执行前发现CIM候选时间与Process真实创建时间的精度不同。一次只读元数据检查实际给出native FILETIME `134384370996124471`、CIM `134384370996124470`，旧字符串精确比较会误拒。新入口仅以整数微秒核对CIM候选；真实Process的完整100ns birth以及最终Runtime watch的逐位匹配保持，不使用浮点除法。旧入口与请求保持原处，未执行窗口。

第二份入口经两份绑定同一请求的增量独审后才执行。显示配置原件复用前一入口实际两次相等读数，不冒充第二次采集或连续拓扑认证。环境清理包括SOUI_、WEBVIEW2_及Node选项。原生时钟从实际Host launch起算：READY15秒、GO30秒、Host90秒；输入器45秒、drain2秒及ACK500ms不变。

## 唯一实际窗口结果

run `0112f4abdaf1a610b0cbe8769a5ec74c`，请求SHA `2b64addd1365f80973e67f924d346f99758cedc8fce2b2748410b5b528d743a1`。用户确认暂停操作后启动；GO在原绝对期限内7.2004725秒接纳。

| 对象 | PID / birth | 实际退出 | 时长 | 关闭 |
|---|---|---:|---:|---|
| Host | 93952 / `134384373772478406` | 3 | 26.425秒 | 自然结束、Job0、未强清 |
| 输入器 | 12844 / `134384373845562827` | 1 | 190ms | 自然结束、Job0、未强清 |
| 控制器 | 本轮入口 | 1 | — | 非零保持 |

输入器报告 `attempted=0`、`sent=0`、`inputReadySent=false`，错误为 `R41 target foreground/geometry/class/child/session/held process changed`。同一综合错误在Ready前有三个可达检查点：恢复前、恢复后及冷捕获入口；原件没有保存具体检查点和失败字段，因此前台、几何、类名、父子关系或session中的具体失败项仍unknown。不能用Host READY内的常量类名/尺寸声明代替实际API快照，也不能将错误直接归为外部操作或DPI。

Host按原冷25秒截止收到页面 `E_COLD_INPUT_READY_DEADLINE`，保存并正常关闭Runtime。page未armed、phase0、echo0、native tagged0。保留的10个非标记回调均不属于测量cohort，不据其认定具体发送方。实际冷browser与Host最终原始browser PID/full birth相符，但本轮没有合格输入/负载。最终SDK scalar单份快照已停止、quiescent、dropped/unregistered/causalLost均0；五个phase边界均未取得，整体 `scalarUsable=false` 保持。完整收尾不能将本轮变为有效性能样本。

Job资源的两个阶段从未触发，所有数值为null、attempted=false；不是CPU或内存0。该接口原计划也只覆盖调用进程直接Job原值和查询区间，不认证整个应用/共享浏览器/GPU归属或负载区间峰值。

## 后续与证据

没有以相同条件自动重试。下一项仅补冷检查点、首个失败谓词及实际字段，保留所有守卫、工作量、热输入路径和期限；在根因未定位前不改调度策略或放宽断言。`performanceVerified=false`、`inputLatencyVerified=false`、`fairnessVerified=false`、`releaseReady=false`、`wholeGoalComplete=false`。

原件位于验收包相对路径 `repair-r41-wv2-native-fairness-r1/entry-r2/run-once/`：`host-result.json` SHA `2a5dcb3a8451ad7a7787a653d7bae01e85d2f8a24a2075cce011fdd8d6bc3bc2`，`input-result.json` SHA `5f62194fff463e616970a22eb2b14793abcbbdf964848be06ab302d58e6795f8`，`execution-failure.json` SHA `26aa1348870b8545913583efc4aef35b9badfc3d68515ddd8603035da514431d`。失败请求、构建、独审与原始终态均保持原处。

实际失败独审为 `repair-r41-wv2-native-fairness-r1/actual-failure-independent-review-r1.json`，SHA `e3aecac15c6dce85f1155eb02d6ab4ef5694edc10a655017a9e99922a6106750`。它核对原始退出、完整收尾、0预定输入、资源null及报告范围，不是性能通过。

## R42：冷检查旁证修正，编译未开始

新工具源码只在三个冷检查点保存原API结果、首个失败谓词及有效位。38项原短路谓词、热守卫块和完整发送循环保持；没有失败后重查或在热输入路径新增API/IO，没有移动恢复操作或容忍几何变化。四个原字节源文件共28,218 B，见[修正源码](../../verification/performance/sdk-input-frame-20261105/diagnostic-source-r42/README.md)。源审与构建脚本审分属不同作者，源码计划SHA `eb318744606a8f8d7546a3a19b15262eeb8c0840ea3ecdc66d3d45dc21a3c95a`。

原目录三个静态源码检查通过；副本只调整了基线源码引用，实际在新地点重跑三个静态检查，退出0、0 fail/skip。它们验证词法/顺序及指定反例，不执行Win32、输入或原生目标。

冻结单TU构建输入SHA `e3d4fc946fd9532f61d25c443115086ed83a916ecdf05acb3a3e5db4ff97dd5a`。唯一实际控制器在CIM编译准入前因其他MSBuild/VCTIP实例退出1、约2.348秒，尚未启动configure、编译或自有构建Job；没有产生原生退出码，不以包装器1替代。未结束这些进程或重试。低CPU及无编译器子进程不足以证明闲置，准入拒绝继续保持。

因此本修正状态为“源码审查完成，未编译、未原生复验”，没有新EXE可供采样。原R41失败仍保留；下一轮窗口前台准备和真实检查也不能从源码通过推断完成。机器记录分别为 `continuation-r41.json` 与 `continuation-r42.json`。
