# ADR 0140：隔离消费者的有界 WebView2 / Electron 性能诊断

状态：实验实现中，尚无性能通过结论。日期：2026-10-29。

## 背景

相同 Scratch 页面在 SOUI WebView2 与 Electron 的手动帧率有明显差距，现有数据不能分摊SDK、页面脚本、浏览器调度与显示成本。需要一次S/W/E同源短profile，不能为诊断开放产品远程调试/任意CDP/网页授权接口。

## 决定

- 使用显式独立的 `experiments/scratch-sdk-diagnostic`、raw WebView2 和 Electron诊断模式；正常SDK/演示目标不启用、不安装这些诊断实现。
- S的专用TU原样编译生产 `webview2_host.cpp`，复用现有Stage0原生grant、独占profile、可信文档、pending/active callback和真实Runtime退出watch。只在两个测试宏同时启用时声明固定profile方法，定义全部在实验目录，不改公共SDK API，不返回COM指针或任意脚本执行入口。
- 三端嵌入同一固定protocol、页面setup/query/probe和10秒预热/10秒capture。页面准备复用已有场景handler和确认流程，仅作用于新建诊断文档。支持固定Profiler与Tracing白名单；未知命令/参数拒绝。不打开inspector UI、远程端口、沙箱开关或loader例外。
- trace总量64MiB、单响应16MiB、chunk数8192；超限/分配失败/丢包/不支持明确记录，不把部分trace当完整。采样期缓存在内存，末尾写证据；CPU profile与native时间、页面mark/实际窗分别记录，profile下帧率不是正式成绩。
- 私有命令复用UI STA、grant/document代际和async receipt；临时分配或同步提交失败退还pending。事件订阅显式撤销，失败保留所有权与非成功结果；用户关闭时先停诊断订阅/新请求，再按原controller/实际回调/runtime退出协议收尾。

## 兼容性、替代和限制

没有改变正式应用的权限/运行时行为、最小工具链、资源策略或依赖。正常编译不包含新的私有方法定义。优先此有限消费者而不是产品CDP网关；仅ETW可作为某个domain不可用时的补充，不能填造缺失的JS栈。

实验消费者与真实S应用在初始化/诊断循环上存在有限差异，因此S/W/E profile只用于归因，后续性能验收必须回到无profile的正式消费者。重复采样只因实际失败或新假设；不能通过换资源、降VM步频、切GPU、失焦节流或隐藏坏成绩取得收益。

用户最新允许小量体积增长，覆盖此前0字节门槛；仍报告正式EXE/DLL/runtime/ZIP实际增量。诊断消费者不进入用户运行包。

### 人工启动门槛

S/W增加可选`--start manual`。页面ready后才启用原生“开始采集”菜单，用户触发时复核owner前台并通过原Controller/Host focus入口请求页面焦点；页面query实际hasFocus为真后才进入原预热/采集。菜单位于client外，outer尺寸补偿菜单高度，保持原内容viewport；关闭仍走原借用/Controller/browser退休路径。默认自动模式保持原行为，不新增远程/任意命令入口。

人工等待计入原总期限：S仍从既有began计算120秒；W从首次ready时刻计算原120秒capture预算，不因点击或重复ready报告重置。启动前取消返回非成功，不能把native菜单事件当作已证明真实硬件输入或正式帧率资格。采集中的原生前台、页面焦点、trace容量和失败规则保留。

调用 focus 后再次读取实际 owner 状态，保留 W 的 Controller 强引用及关闭/当前 identity 检查，避免 COM 重入后继续使用已退休窗口。取消仅将原成功状态置为取消，不覆盖既有失败原因。当前 x64 LLVM22/MSVC142 专用 S/W 目标已实际编译；最终两处消费者增量自然 0/0（12.336s、12.286s），930 项选定源码前后不变。未改的隐藏原生夹具此前 1/1 自然通过，覆盖菜单销毁与 client 尺寸。此证据不是默认 MSVC143、正式应用构建或实际人工前台采样资格。

## 验证与回退

固定命令/页面脚本/容量边界做行为验证；专用S/W目标真实编译，验证正常完成与中途关闭时订阅/方法回调排空。三端以同源recipe hash和实际载荷、Runtime、GPU、视口、前台证据准入。缺少domain/trace/原生守卫时分别报告partial/unknown。回退可删除独立实验与测试宏私有声明，正式SDK行为不变。
