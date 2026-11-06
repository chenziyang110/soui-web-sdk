# preview.5：WebView2活动输出通知合并

SDK源码已实现通知合并并分配新候选版本 `0.7.0-preview.5`；当前通过的是SDK4基座上的私有组件验证。本报告不将它改挂为preview.5整包、帧率、输入尾延迟或完整公平性通过。

## 实际SDK修改

`webview2_adapter::pump` 进入时清除原通知pending后，worker在COM期间新入队的回复可能再次通知；该回复即使已在同slice处理完，窗口仍会收到一次空pump。新私有作用域只在首个实际pop的原mutex段取得活动标记；活动期间输出由当前drain接续，尾部若仍有输出才补通知。无新增线程、timer、消息抽取或队列状态查询。

独立生命周期通知保持原路线。最初源码审漏掉了“首pop为空后入队且首次Post失败”的旧尾部恢复机会；Root定位后恢复原normal空尾重查，并新增第10项反例，最终独审r2明确取代旧r1。没有删除旧断言，或据此宣称所有Post失败均可恢复。lane FIFO/优先级、COM前额度释放、原deadline与owner STA均未改，设计及回退见[ADR-0167](../adr/0167-webview2-active-output-wake-coalescing.md)。

## 已执行的限定验证

原SDK `0.7.0-preview.4` 安装保持不变；仅从当前源码构建私有renderer并与原Core/ActionJson/Loader链接，不发布另一个同版本SDK。原owner策略fixture保留原断言并执行10项新增合同；实际容量fixture源码不改，仅其链接指向新私有renderer。真实link.read核对新archive，排除旧installed renderer。无窗口激活、前台切换或输入注入。

| 对象 | PID / birth | 实际时长 | 结果 |
|---|---|---:|---|
| 三TU构建 | 73896 / `134384482941536033` | 69.349秒 | v14314.44/SDK19041/x64 Release，自然0、Job0 |
| 原owner策略fixture | 56236 / `134384483638119258` | 105ms | 原断言及10项新增合同通过，自然0、Job0 |
| 原隐藏容量fixture | 89560 / `134384483642697309` | 8.281秒 | 原拒绝/续租/2清理/129业务及Runtime正常关闭，自然0、Job0 |

原监督上限分别180/15/60秒，drain2秒；各执行一次，没有强清或重试。原容量程序内部55秒总截止保持。其browser PID102300/birth`134384483644503153`与原始Owned流对应。隐藏功能结果不代表可见帧或完整资源归属。

私有renderer archive为8,406,500 B、SHA `86a0d3e8e9ac16833fadc8cdc3b354564ac54a66367b86ac1361349f42cea2f0`。它不是同编译器整应用体积对照，不能据此推算SDK/runtime/ZIP增长或性能收益。源码包小合同另实际16/16通过，仅验证ADR收录及原打包契约。

## 证据与剩余工作

原件相对于验收包为 `R44-active-output-candidate-r1/`；冻结输入 `build-r1/source-inputs.json` SHA `19f61f6c372473fbba234f833107d4a488bc4145028f4d905f725c0e11cd665c`。最终源码独审 `source-code-independent-review-r2.json` SHA `96e65fd66c88f3be30f9571a3a6dc0589049b041658c3ecf8b3e0c24a550bae9`；实际独审 `actual-independent-review-r1.json` SHA `e32763bf98fbaa95d246636ccbb9936ec9c2be0903f945b236a2fc8887f47ff6`。源码审和实际审与实现作者分离；未运行新的测试来重写原始结果。

后续SDK5的正常构建、安装及独立功能结果见[集成验收](sdk-preview5-integration-acceptance-20261106.md)，不替代本报告的私有组合身份。R43的26.205秒绘制/定时器间隔、tag0和超时原件保持；该根因尚未因本项局部修正而关闭。新M01 v2仅修正发送完成与接收完成混淆、身份/失败旁证，已编链但消费者退场门失败，尚未取得输入采样资格，不继承旧样本。

`performanceVerified=false`、`inputLatencyVerified=false`、`wholeFairnessVerified=false`、`releaseReady=false`、`wholeGoalComplete=false`。
