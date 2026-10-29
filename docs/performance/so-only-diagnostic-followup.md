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
