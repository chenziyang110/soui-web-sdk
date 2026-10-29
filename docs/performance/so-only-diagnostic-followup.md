# SOUI 独立诊断的实际结果

2026-10-29，按维护者“继续 SOUI、不再处理 Electron”的指示执行一次有界 S 诊断。没有启动 W/E，没有注入鼠标键盘、请求前台切换或修改全局设置。诊断数据不能替代正常运行的 FPS 或物理输入延迟验收。

## 身份与退出

使用已经实际编译的专用 `experiments/scratch-sdk-diagnostic` 消费者，EXE SHA256为`752fe078946d527b6837d7175a61b937cf2ee5c2f5bd7c098b7e64f006833fb3`；并非本次后续源码改动的新 EXE。Runtime为156.0.4314.50。正常缓存版资源共3302文件、160432414B，运行前后逐文件SHA一致；bundle SHA为`2486e82225af9f26129d77dace8878baf0ae32668de0a0a48db1ceea0770f5a1`。heavy场景两端点均为241 targets/240 threads，cache开启。

原始记录`diagnostic-so-only-r2/result.json`：专属Job在25644ms自然0/0、12进程全部退休、active=0，没有强制结束。Controller、回调及实际浏览器进程退出检查完成。trace收到416批、54131条事件，11941508B，无溢出；序列化原文件为13707725B。

首次`r1`把外部启动常量7误认作非激活显示，实际最小化，120秒到期后正常返回2；所有15进程已退休。失败保留。`r2`只把外部launcher修正为`SW_SHOWNOACTIVATE=4`，没有增加等待期限、修改页面负载或改动SDK。

## 证据的限制

`r2`初次配置时页面可见，但采集前发生blur和`visibility:hidden`。原生前台观察为0/237，采集期rAF回调为0，定时器间隔约1秒。面板正确保留invalid；即使采集和关闭成功，也不能把这个窗口用于前台归因、比较FPS，或声称后台定时行为是SDK缺陷。没有在同样状态下重复W采集。

CPU原始文件373591B，12528 samples/509 nodes。实际有26个负timeDelta，最小−127µs；严格加权reader拒绝该输入，未钳零、补时长或重写原始profile。另存的样本计数只表示未加权出现次数，不能标成CPU时间。bundle、map和source-build身份吻合，但采样映射主要折叠在webpack包装位置，因此同样不能据此确定具体Scratch热点。

保留trace中的同PID/TID起止mark，没有未经验证地把浏览器、V8 profile和原生QPC跨时钟裁剪。下一轮前台热点定位仍需真实可见窗口、相同逻辑工作量和可校验的时间域；本轮结果不授权默认开启QoS、共享预算、状态复用或降画质。

关键本地证据标识及SHA256：

- `diagnostic-so-only-r2/qualification.json`：`afb82da497e9f6f5400f06e5c4d18e3e5292c0ffef0893fab125df6736c8b218`
- `S-session/cpu-profile.cpuprofile`：`d6b55d981af44129c0c9672ef67b99c7e3af4be585c6d84120da33f6574b545e`
- `S-session/browser-trace-chunks.json`：`3c0378ed532cd1246a49f101c487a426249e89a3cc43f9725dec5a34f125edc0`

这些本地证据没有加入SDK运行包。完整前台、输入、体积对照及稳定性验收继续保持未完成。

## 后续可见页面采样

在用户明确表示可手动置前后，刷新专用S消费者并运行一次新的有界采样。增量构建27786ms自然0/0，4个对象本轮实际重编；31个对象按旧实际编译证据逐项核对source/object/argv/精确headers后复用。105个实际头文件、6115项provider输入和三份内嵌协议已核验。EXE仍1486848B，SHA `4e5dad2730dd413551b8a0e88a9089cc0f8884e7bde53b7afbd44a1076f709fa`；源上下文为b424db369及已记录的非S脏文件，不能冒充完整干净发布构建。

`diagnostic-foreground-S-r1`自然0/0、27960ms，12个进程全部退出、资源3302项前后SHA不变。页面采集前后均visible、241 targets/240 threads；约10秒观察到987次rAF及607次VM step/draw/targetsUpdate。面板本身完成且valid，诊断窗口率98.687仅是带Profiler/Tracing的观察值，不能用作正常应用FPS对比。原生前台观察0/244、页面hasFocus=false，严格焦点条件未通过；trace到67108761B发生上限溢出，不能用于完整因果链。

实际CPU文件619763B、24733 samples/692 nodes/0 unknown。58个负timeDelta被严格reader拒绝，仍未钳零或换算CPU时间；未加权计数有19245个program、3916个idle、41个GC及1531个其他样本。已识别的`_step`包含样本1473个，其中`emitTargetsUpdate`780、`stepThreads`433、`draw`253；`handleTargetsUpdate`771与React的`notifyNestedSubs`736说明状态事件的GUI订阅链值得优先调查。这是已归因子树中的样本关系，不能称53%总CPU，也不能排除大量program样本中的成本。

`_getConvexHullPointsForDrawable`包含样本50个，当前证据支持先查GUI通知/props扩散，尚不足以按预想继续改hull、默认调度预算或QoS。实际bundle及函数坐标/片段已绑定；source map没有被当作可靠的具体源码归因。原始CPU SHA为`2e1f89e76b411c440eb5e7330f9e8d89b1f013cbfa3c8f266a4e314f27777cd7`；计数、源路径、限定分母和资格分别保存在该证据集合的`analysis/cpu-counts-and-calltree.json`、`hotpath-assessment.json`和`qualification.json`。未重新启动Electron或完成S/W有效配对。

下一次人工配对应先补明确的原生“开始采集”入口，在页面准备后由用户点击，再核验真实owner前台及页面focus后进入既有warmup/capture；不能把“用户准备好了”或ShowWindow返回当作焦点证据。该入口尚未实现；应保持原总期限、内容viewport、固定负载和失败规则，不以自动输入或删掉focus条件取得通过。
