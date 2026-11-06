# preview.4：WebView2 持续 owner 负载入口验收

**最新R42复验已进入持续负载，但完整验收失败，W02公平性与性能资格继续开放。** 3125次标记输入均发送并有原生回调；部分回调位于Finish关闭cohort之后，原生账本与页面fence资格均失败。保留此前R41的0输入冷失败和R42开窗前准入失败，没有更改SDK、内核或默认参数。

## 夹具与已执行构建

原冻结SDK `0.7.0-preview.4`、x64 Release、build `18e47fa2-69df-4530-b1f9-365f6b2aec28`；固定WebView2 Runtime `156.0.4314.58`。新独立profile `r41-default-native-strip-v1` 使用1100×792 owner及1100×72原生child strip，同一STA处理真实Bridge回复。原私有非输入桌面prototype没有修改或继承为真实输入资格。

预先冻结的工作为30秒4KiB、7条echo在途及最多1条控制；全部3125次/125Hz、25秒输入必须落在原负载窗口内。原生LoadActive握手及29秒单次完成fence约束跨时钟因果，不以延迟phase消息或裁取子集冒充完整重叠。冷/最终marker、完整账本、前台/几何/held身份、Esc停止、原deadline及自然关闭要求均保留。

v14314.44/Windows SDK19041实际局部构建自然退出0、79.998秒，Host两个TU、输入器一个TU及纯策略合同一个TU；14项纯合同实际通过、62ms。Host901,632 B、SHA `00fecb76fcd51c2379cd65e68b7fe03a6bb17541a0f5a27dc6347d197eb65e12`；输入器113,664 B、SHA `d16cabf70ccc84bd4bac9977b4711663502404ccd4ef1f50ae83bda60b732b9d`。SDK、内核及产品包没有重新构建。Node/页面定向检查保留各自首轮与修复后结果，不把曾经失败的测试harness改写为一次最终全组通过。

[源码快照](../../verification/performance/sdk-input-frame-20261105/diagnostic-source-r41/README.md)保存17个原字节文件、116,952 B，清单SHA `a74707aeb47b6882dbff3615f44c7bd195ea79e67785a8d96a828bd9bad691bb`。快照制作时尚未运行窗口，新地点没有构建或继承新产物资格。

## 启动前修正

第一份冻结入口在原生执行前发现CIM候选时间与Process真实创建时间的精度不同。一次只读元数据检查实际给出native FILETIME `134384370996124471`、CIM `134384370996124470`，旧字符串精确比较会误拒。新入口仅以整数微秒核对CIM候选；真实Process的完整100ns birth以及最终Runtime watch的逐位匹配保持，不使用浮点除法。旧入口与请求保持原处，未执行窗口。

第二份入口经两份绑定同一请求的增量独审后才执行。显示配置原件复用前一入口实际两次相等读数，不冒充第二次采集或连续拓扑认证。环境清理包括SOUI_、WEBVIEW2_及Node选项。原生时钟从实际Host launch起算：READY15秒、GO30秒、Host90秒；输入器45秒、drain2秒及ACK500ms不变。

## R41原实际窗口结果

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

## R42：冷检查旁证修正与首个编译准入失败

新工具源码只在三个冷检查点保存原API结果、首个失败谓词及有效位。38项原短路谓词、热守卫块和完整发送循环保持；没有失败后重查或在热输入路径新增API/IO，没有移动恢复操作或容忍几何变化。四个原字节源文件共28,218 B，见[修正源码](../../verification/performance/sdk-input-frame-20261105/diagnostic-source-r42/README.md)。源审与构建脚本审分属不同作者，源码计划SHA `eb318744606a8f8d7546a3a19b15262eeb8c0840ea3ecdc66d3d45dc21a3c95a`。

原目录三个静态源码检查通过；副本只调整了基线源码引用，实际在新地点重跑三个静态检查，退出0、0 fail/skip。它们验证词法/顺序及指定反例，不执行Win32、输入或原生目标。

冻结单TU构建输入SHA `e3d4fc946fd9532f61d25c443115086ed83a916ecdf05acb3a3e5db4ff97dd5a`。唯一实际控制器在CIM编译准入前因其他MSBuild/VCTIP实例退出1、约2.348秒，尚未启动configure、编译或自有构建Job；没有产生原生退出码，不以包装器1替代。未结束这些进程或重试。低CPU及无编译器子进程不足以证明闲置，准入拒绝继续保持。

因此本修正状态为“源码审查完成，未编译、未原生复验”，没有新EXE可供采样。原R41失败仍保留；下一轮窗口前台准备和真实检查也不能从源码通过推断完成。机器记录分别为 `continuation-r41.json` 与 `continuation-r42.json`。

### 后续同源单TU构建已通过

上述“未编译”为首个准入失败检查点的状态。随后独立全局CIM实际返回空列表，才使用新的 `build-r2`；原失败、脚本及期限保持，只有尝试目录变化。构建输入SHA `49e68e052acec600d844c6df1dad0e1222c63aebd628fc0b0df5b7066b9611cf`，不能冒称旧 `e3d4…` 对应实际编译。

本次实际v14314.44/Windows SDK19041 x64编译自然退出0、60.901秒，PID106044/birth`134384395571200616`，Job最终0、未强清，只有 `input.obj`。新输入器143,872 B、SHA `39ba96a119cc9aac76a213a79a0895c03d4c206038059408c46cd6cd032d883a`；SDK、Host和内核未重编，产品包未改。新源码副本所在位置仍未编译，不继承新产物资格。

新入口严格绑定本次真实build收据、输入器及原Host，两份事前独审通过。新的显示配置原件为单路DISPLAY1、1920×1080、约74.986Hz，与旧双屏不同，不用于跨轮收益比较。此构建检查点尚未执行真实窗口复验；当时等待安静时段确认，期间未追加编译或压缩。后续实际结果分节保留如下。

新构建与事前入口身份另存 `verification/performance/sdk-input-frame-20261105/continuation-r42-build-r2.json`；旧 `continuation-r42.json` 继续只记录首个准入失败，没有改写为通过。

### 随后入口在开窗前被资源门拒绝

用户确认当前60秒暂停操作后，实际执行新入口run `c4654468a4635e93f7a08270afcddf41`。准入助手PID104180/birth`134384405983272996`自然退出0、2.343秒、Job0；其快照发现新的VCTIP PID32976，控制器按原规则退出1。该实例与前一批遗留助手不同；存在记录不能证明它正在编译或闲置。

本次没有启动Host、输入器或窗口，也没有phase/GO文件；因此不是R42冷守卫的原生复验，更没有延迟结果。已立即释放用户暂停操作的安排，未重试同一run或结束该进程。机器记录为 `continuation-r42-native-admission.json`。后续先解决编译准备与采样间的资源协调，不放宽原准入门。短暂复用项目现有互斥只能排除遵守该协议的同session编译，不能声称全机独占。

## R42最新实际复验：持续负载出现长间隔，完整资格失败

用户再次确认就绪后，仅执行新run `1e3c9fbd4e104b2da1282332a68ee5a6`，请求SHA `bf255d16f6f554afc6441453395cab22c0cdea783d39268f105536842cefce43`。沿用原Host、SDK、浏览器版本及工作量，唯一变化为已编译的R42冷诊断工具和入口资源协调。通过实际返回的唯一任务窗口对象激活并查看后，在Host启动后的23.9803114秒接纳GO，原30秒期限未变。三个冷检查均complete/pass；恢复后和zero-capture读到的foreground HWND都等于任务owner `5581894`，本轮不是此前综合冷守卫失败。

| 对象 | PID / birth | 实际退出 | 时长 | 关闭 |
|---|---|---:|---:|---|
| Host | 32624 / `134384444126712062` | 3 | 59.119秒 | 自然结束、Job0、未强清 |
| 输入器 | 93100 / `134384444366898474` | 1 | 28.612秒 | 自然结束、Job0、未强清 |
| 控制器 | 当前entry-r3 | 1 | — | 非零保持 |

3125次SendInput均返回1。5460行原始WndProc账本中的正tag恰为1..3125，各一次，无缺失或重复；其中2725项在cohort内，400项在输入器置Finish、监视线程关闭cohort后才进入WndProc，不能称为400项丢失。最后tag的receipt比对应SendInput返回晚1386.9659ms；该差值是失败轮的单项诊断，不是合格整组输入延迟分位，更不是网页或物理呈现延迟。

原账本首错为fault5：ordinal2实际extraInfo=0/tag0、同前一标记坐标(39,64)，真实dispatch tuple有效且位于cohort内。其具体发送方未知，不认证为用户、硬件或其他自动化。原策略拒绝保持；绘制代码在账本failed时将marker nonce置0，而输入器最终报告 `marker nonce/counters/content`。最终解码字段在抛错前未落盘，不能伪称实际读到具体nonce/CRC或用源码推断替代该缺失原件。

另一独立失败为页面 `E_INPUT_FINISH_FENCE_LATE`。预定30秒负载及其收尾记录90,247次4KiB成功echo、在途峰值7，失败/超时/内容不匹配均0；页面实际发起单次Finish请求的时间为load起点后31,000.9ms，晚于原30秒deadline。原生load阶段长度31,003.0861ms，正常关闭但未进入完整postIdle，整体 `diagnosticExecutionComplete=false`、`scalarUsable=false`。这些有限计数不授予完整成功吞吐、CPU/内存或公平性资格。

负载阶段原始绘制/定时器最大间隔分别26,205.3452/26,205.8710ms；它们是实际WndProc观测及阶段边界统计，不是唯一帧、浏览器绘制完成或物理显示。该异常与Bridge持续完成同时存在，说明需要继续定位消息类别的调度机会。当前SDK的单次owner slice上限为16项/2ms，尾部输出未空会再次PostMessage；切片预算本身不能保证不同类别之间的公平性。[Microsoft GetMessageW文档](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getmessagew)说明无filter时posted消息先于输入、WM_PAINT和WM_TIMER。持续posted链是有源码依据的假设，但本轮没有消息类别完整计数，不能认定SDK补投是唯一原因，也不能排除WebView2自身消息或测量影响。

### 合作编译锁的实际结果

使用项目既有同session互斥，holder PID49384/birth`134384443925900905`；Ready前严格CIM为空。QPC频率10,000,000，Ready `1988501637440`、原90秒到期/实际释放 `1989392093415`；load从 `1988950130938` 至 `1989260161799`，数值上全部落在Ready→release内，距释放尚13,193.1616ms。

holder按原90秒期限自行到期，exit2、Job0、未强清，原线程释放成功；`expired=true`、`rootStopObserved=false`。Root确认两原生Job终态后尝试Stop时，事件已经不存在，控制器保持非零。数值区间覆盖不替代预定的正常Root-stop资格，也不证明全机独占、无外部输入或没有不遵守该互斥的编译。不得将已到期结果改写为成功协调。

原件相对于验收包为 `repair-r42-native-cold-guard-diagnostic-r1/entry-r3/run-once/` 和 `operator-slot-once/`；机器索引 `continuation-r43-native.json` 绑定请求、进程、负载、原始失败及独审。没有自动重跑、改SDK/default、放宽fault5/fence期限或重新打包。下一步先分别定位Finish/marker测量边界与posted链公平性，保持本轮全部失败及未知项。

本轮实际独审位于 `repair-r42-native-cold-guard-diagnostic-r1/entry-r3/actual-independent-review-r1.json`，20,735 B、SHA `2b13dd07c11c388c43fcc7394ec3228ba98d99b5ef23fc499b940aebc3c015f2`。独审核对三份原始Owned流及PID/full birth、全部正tag、实际冷快照、slot QPC区间和保留失败，不认证性能。Root使用的保守40秒lease余量门在执行代码中检查，但该同REPL anchor未独立落盘，不能从本机原件重建这一事前余量，亦未补写为已验证。

marker/协议只读定位为同目录 `marker-failure-diagnosis-r1.json`，SHA `979236140fd9935fd3b13a1616aaf8d2ff9b525c10b9098fb5b420cb3fe944a8`；其作者参与原工具，不能充当工具源码独审。该定位不含新执行。历史R41/R42失败与本轮失败分别保存，报告无通过标志扩张。
