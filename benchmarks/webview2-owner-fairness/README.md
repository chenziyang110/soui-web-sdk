# WebView2 owner paint/timer 诊断夹具

本目录是独立普通 SDK 消费者，只观察一个私有非输入 desktop 上，真实连续 Bridge 回复期间同 STA 的 `WM_PAINT` / `WM_TIMER` dispatch 计数与最大无进展间隔。它不是硬件输入、有效帧率、physical present、吞吐提升或完整公平性验收。2026-11-03 已完成下述限定构建与单轮诊断。

## 本轮实际结果

普通公开 SDK `0.7.0-preview.1`、BuildID `webview2-owner-fairness-r1`，x64 Release / LLVM22.1.3 / MSVC142 / Windows SDK19041，固定 WebView2 Runtime `156.0.4314.58`。两个 TU 实际独立构建；SDK、内核未重编，不能扩为 v143 资格。应用 EXE SHA-256 为 `c86b7e1998dd972e23b6fbc7d8a62e84fca78b22c10a54afefaa2123c0186ec1`。

唯一私有运行中，60,679 次 4 KiB echo 的页面/原生记账一致，失败、超时、错包和最终 echo 在途均为0，峰值在途8。真实授权 mark 的 native load 区间为10,993.0601 ms；JS 停止回调比原发送截止晚991.9 ms，各 lane 自身仍检查原截止，不能把这轮称为精确10秒吞吐窗或据此认定多发了991.9 ms。

| native 区间 | Paint / Timer dispatch | 最大 Paint / Timer gap |
| --- | --- | --- |
| pre-idle，2,287.1 ms | 74 / 74 | 32.4161 / 46.0951 ms |
| load，10,993.0601 ms | 354 / 351 | 36.8956 / 48.7050 ms |
| drain，0.8137 ms | 0 / 0 | 0.8137 / 0.8137 ms |
| post-idle，3,006.836 ms | 97 / 97 | 32.5966 / 45.6841 ms |

Host、exclusive runtime watch、应用与 controller 均正常结束，owned Job0、无强清；原 broker 在严格完整进程快照中已缺席。617项冻结输入在运行前后不变。本 README 是运行后的文档更新，C++、页面、SDK和构建输入保持原字节；原冻结源码和失败证据不被重写。

该负载未观察到持续 paint/timer 无进展，因此维持当前 SDK 调度，本轮不新增 SDK timer/thread。夹具自身的20ms monitor和SetTimer属于观察条件。没有证明 posted 队列始终饱和、硬件输入公平或有效呈现；`fairnessVerified` / `performanceVerified` 仍为false，不继续补跑或扫参数。原始 `native-result.json`、`supervision.json`、`launcher-result.json`、构建结果及独立窄审随本次外部验收材料保留；独立 DLL 观察与完整 Runtime 闭包仍为unknown/false。

仅当真实连续回复影响这些消息进展时，才为后续 SDK 调度修复提供具体假设；没有观察到对应现象就停止该诊断，不增加 A/B、参数扫描或补跑满意数字。无filter的 Windows 消息循环优先处理 posted，再处理硬件输入、paint、timer；消息顺序只是风险机制，不是本候选已经发生饥饿的证明。[GetMessageW](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getmessagew)

## 最小范围与复用

- `main.cpp`、`paint_probe.cpp`两个 TU，复用 `tests/native/webview2_capacity_host/capacity_host.cpp` 的真实 prepare / mount / exclusive runtime watch / destroy / finish 顺序。
- 页面复用原容量页的正规 preload `envelope/rawCall/success` 语义和 bootstrap / probe 协议，以 ES5 Promise 链实现固定场景；不使用 ESM、未转译 async/await、新 preload、伪造 session 或原容量探针的 raw native-excess 绕过。
- 不包含原容量夹具的 worker gate、256条 PeekMessage 批次、主动 pump。测量期间只有真实 `webview2_event_message` 调用 `Host::pump()`。没有全局 CMake 接入或自动运行 CTest。
- 一顶层窗口、一个可见原生 child strip；WebView2 为480×288，strip为480×32，互不覆盖。20ms低频线程只对自己的 child 调用 `InvalidateRect`；真实 SetTimer 也为20ms。Host/COM全部留在创建STA。线程必须真实停止后才能销毁child。

## 固定窗口与报告语义

页面完成真实bootstrap和ready probe后：2s idle → 10s停止条件的4KiB回显流 → 最多5s drain → 2s idle。固定8个echo在途，最多再叠加1个正常控制请求，合计不超过9；原preload的128额度与SDK权限没有修改。每次请求本地及native timeout为5s；drain使用原 `loadDeadlineMs+5000`，不重置截止。逐项校验序号及全部4096个ASCII字节，失败不重试，不声称超时取消了native工作。

`fairness.mark`在真实授权的**Bridge worker handler**处记录native QPC，并不是owner消息入口或精确JS10秒边界。报告保留mark请求/回应的JS单调时间和native QPC；两者不同钟，不直接相减。原生load区间包括控制运输等待，drain单列，不能把页发送截止冒充native最后工作终点。post-idle在全部echo本地终态已收到之后才开始。

四段原生样本均包含首个dispatch之前和最后dispatch之后的尾部gap。64位QPC、计数用十进制字符串；缺失/未结束段的`closed=false`，clock/overflow/paint失败保留fault，不补零变合格。`paintDispatchCount`只计WM_PAINT处理机会，静态strip的FillRect不是唯一画面帧，也不证明显示完成。InvalidateRect及timer会合并，调用次数不能换算精确丢失paint。前后idle没有paint/timer进展则`probeQualified=false`，不能据此判定负载饥饿。

`diagnosticExecutionComplete`要求真实echo完整记账、所有阶段闭合、无fault/超时和自然runtime关闭；load零paint本身不会被隐藏成“夹具失败”。`paintProgressDuringLoad`与最大gap保持原事实；`fairnessVerified`和`performanceVerified`始终false。`fairness.done`进入native只确认此前report响应已收到；最终done控制回应可能随关闭丢弃，`finalDoneControlReplyConfirmed=null`，不冒充全部控制运输完成。echo inflight=0与此是不同事实。

唯一JSON结果使用同目录`.pending`新建、flush/close后no-overwrite rename。rename前后复核原55s截止；若最终文件已发布但操作迟到，进程仍非零，原件保留。消费者必须同时要求外部监督器自然exit0、Job0、无强杀及报告资格，不能只看JSON里的某个true。

## 构建与执行约束

独立CMake target为`SouiWebView2OwnerFairness`，链接正常`Soui::WebView2` / `Soui::ActionJson`。必需参数是绝对`MATCHED_SDK_ROOT`与唯一`SOUI_FAIRNESS_BUILD_ID`；`MATCHED_SDK_VERSION`接受明确raw版本，省略时读取仓库根`VERSION`。复用既有`benchmarks/input-ui-host/sdk_version.cmake`，numeric EXACT且实际imported `version.h` raw一致。外部最小源副本需保留该helper或显式传`SOUI_FAIRNESS_VERSION_HELPER`；没有版本常量或伪造VS兼容变量。真实编译器/Windows SDK/库/源码/产物身份由独立构建driver冻结。

本轮构建driver另限定现有`candidate-script-source-sdk-r1` SDK、LLVM22/MSVC142/19041；不重编SDK、不声称v143资格。仅部署实际加载的`resources/index.html`、`resources/fairness.js`及正常SDK runtime。没有ESM包、npm安装、Web bundle或CPU self-test阶段。

程序参数依次为`--profile <全新profile> --resources <冻结页面目录> --output <全新JSON路径> --runtime <冻结WebView2 runtime目录>`。**只由任务已有严格私有desktop/owned Job监督器启动**，不能直接在Default桌面运行；程序也要求自有desktop以`AgentGui_`开头且实际UOI_IO=false。只SW_SHOWNOACTIVATE，不激活/切换desktop/注入输入。

冷环境固定：prepare_output、owner_budget、output_timing只接受未设置或`0`；SCRIPT/PHASES必须未设置。启动15s、工作45s、全生命周期55s为从进程入口开始的绝对截止；独立线程只请求停止/推进关闭，不能通过可能被饿死的WM_TIMER结束。任何截止/强制清理都不算通过。完整结果、外部PID/creation/模块/私有desktop/Job原件在执行后由现有runner保留，本目录不新增通用reader或授权接口。
