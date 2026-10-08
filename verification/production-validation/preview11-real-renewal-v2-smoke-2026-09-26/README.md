# preview.11 续期夹具 v2：两架构首次 GUI 均未就绪

**x86、x64 均在初始 60 秒就绪期限失败、原生退出 1；真实续期未通过。** 两次在桌面空闲时串行运行固定 30 秒模式，使用同一 v2 freeze `80d19d4e2008a505bbb8a9293849f3756d4bf07ad7838ad38aa5bb054ac0ee3a`，未改变 SDK。完整原生日志、监督器、输入前后身份和分析器失败输出在对应架构目录。

两次各有 19 条 v2 NDJSON：一次 bootstrap 前生命周期退役被正确分类，bootstrap、ready、两类任务调用均为 0。到 60 秒记录 `Initial page/Host readiness exceeded 60 seconds`，随后 Host 销毁和 Runtime 关闭成功。监督器确认无外部桌面负载、无外层超时，运行载荷和冻结输入前后一致。严格分析器均退出 1；没有通过结果。原始日志不能确定页面脚本为何未启动，不能归因为授权续期本身。

x64 运行中的定向窗口截图显示静态页面仍为“等待原生连接 / 初始化中”，交互统计尚未初始化。为读取脚本错误另建三个诊断副本，只增加头部 error/load 观察器，保持原 EXE；它们有独立输入前后清单，**不属于原冻结验收输入**。三个副本也在初始期限退出 1，未产生成功证据：第一次手动截图因窗口隐藏/之后进程结束未取得；第二次定时截图虽然 API 返回成功但内容全黑，不据此断言页面；第三次让被测 GUI 按既有 ShowWindow 显示后获得可读截图，仍只有静态页面，未显示 error/load 观察结果。截图助手只匹配准确测试进程与窗口，辅助脚本后续增加了隐藏窗口枚举，不能把修订后的助手称为首次截图的冻结版本。

下一步应定位 classic HTML 解析、polyfill 加载、System.import 与事件循环的实际停止点，可用独立诊断副本记录分段标记或原生 `application_diagnostics()`；没有证据前不修改原 SDK、不放宽期限、不启动 35 分钟或八小时长测。冻结 v2 首次失败原样保留，不继续盲重跑。

归档仅含原始日志、清单、诊断脚本和截图；运行二进制保留在清单指定的外部目录。根 `source-navigation.log` 的完整诊断副本随每次运行保存。`realRenewalVerified=false`、`sourceDeliveryVerified=false`、`benchmarkValid=false`、`releaseReady=false`。
