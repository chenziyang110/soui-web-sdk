# SOUI Web SDK 私有 MiniBlink 输入法追加验收

结论：本轮补齐了正常中文提交、取消、失焦后真实 SDK 提交链，以及两种不同最终文字的真实 Blink API 回放证据。仅这些限定功能条件通过；整体输入延迟、真实帧率和默认采用仍未通过。接续[焦点修复报告](sdk-preview4-focus-20261106.md)，机器记录见[continuation-r32.json](../../verification/performance/sdk-input-frame-20261105/continuation-r32.json)。

## 载荷与范围

本轮仅用 x64 Release：已安装 SDK `0.7.0-preview.4`，build ID `18e47fa2-69df-4530-b1f9-365f6b2aec28`，source SHA `e1d2dd8b7699930f678e21a77772b8fa4e371f5b67a53937d98470ee1659e87c`；沿用 R31 私有 DLL `a4f573415d0bc38d420088d3f70a14d66e42b5703707e6220015e5f476a147cd`。SDK、内核与已发布同版本包均未重编或覆盖，仓库 vendor 修复补丁仍未默认应用。R31 四项历史库等同性未知的限制继续保留，组合不继承配对性能资格。

外部测试消费者增加有界被动输入法页面和独立报告接口，保留原 manual 路径、19 项合同、提交与关闭期限。原生 v143/19041 构建通过，新增36项报告/落盘合同与原19项共55项通过，profile/scalar 合同也通过；不把这些合同当系统输入法验证。原始宿主 SHA `a1fc59d7c8ce0eb6db40001e3ff8f34f9e379c811512fae7d761cba23eda1ece`。

## 一次真实系统输入法流程

run `d3de8df572cfe9556a2f4a3634200142`；只操作本任务的两个 SOUI 窗口，通过真实按键调用系统输入法，未注入 Unicode 字符串或合成 composition 事件。

| 场景 | 实际结果 | 结论范围 |
|---|---|---|
| 输入 n/i、确认首项候选 | 文本“你”，selection1/1 | 本轮正常中文提交符合预期 |
| 再输入 n、按 Escape | 原文字“你”保留，selection1/1 | 本轮显式取消符合预期 |
| 再预编辑 ni，切到另一 SOUI 窗口后返回 | 最终“你ni”，selection3/3，候选消失 | 如实保留系统实际提交结果；未强按新确认键 |

Finish 在关闭前接纳，正常 Runtime/trace 排空后持久化78条页面事件，overflow=false；截图即时77条后追加的 final checkpoint 为78。两个原生进程、监督器和入口均0、Job0，无强制清理，115.725秒完成。SDK/vendor trace 分别13223/70262点，双源 full/complete、loss0/fault[]。活动 tuple2/1/2/2 内 IME start3/update7/commit2/end3 均有唯一接收、接纳、Engine begin、正常 return 链，没有该 cohort 的丢弃终态。

已实际观察到 blur151 返回 QPC `1491881987553`，随后 commit152 才进入 `1491881995772`，正常返回 `1491882009621`；end153 正常返回 `1491882028671`，之后才重新聚焦154。这证明本轮真实 SDK 失焦后提交交付，不能继续笼统记为 unknown。这些 QPC 是功能顺序证据，不是输入延迟或物理显示读数。

第三段预编辑 P 与系统最终 F 都是 ni，因此“你ni”不证明 P≠F 的替换保真，也不能排除 provider 较早自行 finalize。DOM 事件没有 native input ID，未证明逐包 DOM 映射；相邻 update/commit 也不能证明来自同一个双 flags 的 Windows 消息。独立分析 SHA `0b7c2ccc5e419b10a218229fc033af9ced9ce60078812ac0b8e18cfd33c948e2`。

## 不同最终文字的真实 Blink API 回放

此模式从实际已授权 broker 回调取得受控 view/context，有限调用真实内核 API，不生成 OS IME 或 SDK IME mailbox 包。用 P=ni、F=你区分“保留最终提交”和“提前保留预编辑”两种行为。

首次回放在测试调用者读取脚本结果时失败，native2/入口1，Job0、正常关闭；实际 capture 成功，但 composition case 尚未开始。根因是 wkeRunJS 自己包一层函数，夹具传入的 IIFE 没有外层 return，产生 DOM 副作用却返回 undefined。这不是内核 composition 失败。原记录和独立失败审查保留（SHA `316f86bcb1bb2aab3a5951e3576910dd6b1b180de1dd71e497dc191d24c35255`）。

修复仅发生在另一份外部消费者：显式返回表达式，立即有界复制临时 JS 字符串，再完整复核上下文。只重编受影响的 Host，不重编 SDK/内核、不重复55项未变合同或系统输入法流程。新宿主同为1,273,344字节，SHA `32ed7a551045ead20519e8d6b83eee6c10e31aa9122af5c36a624803e444a64b`。

run `36769db5916beec98693a1309e40d935` 的两个冻结配方均满足限定条件：

| 原生 API 顺序 | 最终文本/选择区 | 实际事件 |
|---|---|---|
| setComposition(ni) → killFocus → confirm(你) | 仅“你”，selection1/1，documentHasFocus=false | 8条，无 overflow |
| setComposition(ni) → killFocus → setFocus → confirm(你) | 仅“你”，selection1/1，documentHasFocus=true | 11条，无 overflow |

两侧 compose/confirm 均真实返回 true。blur 后及第二侧 refocus 后均未记录提前 compositionend；唯一 compositionend 都在 confirm 阶段，data为你，之后 input 值为你。这证明两种实际 Blink 配方的不同最终文字替换；不唯一归因内部状态，也不能外推为完整 SDK/OS IME 链通过。原生产者 functionalPass=null、realOsImeVerified=false、sdkImePacketPipelineVerified=false 均未改写。

本轮入口、原生和监督器均0，Job0，无强制结束，22.641秒正常关闭；SDK/vendor trace 分别2629/15264点，完整、loss0/fault[]。独立实际分析 SHA `cca78a27c48d1b2278bca204fb244a28ee327a538c23ea892d985c1419f6b776`。

## 失败保留与可审查源码

第一次消费者原生构建已通过，外层读取器却因 LF-only 匹配误拒绝标准 CRLF SDK identity，原退出1保留。随后独立严格解析五个唯一字段，复核原构建后首次执行55项合同，没有重编旧产物或把旧退出改成0。独立恢复审查 SHA `1a43e18a5747ece034f1a2d8c847d8dad378c65c09bf41017f2c78554628652c`。源码补丁生成过程的准备失败也保留在本地归档，不作为原生执行。

[未应用的测试消费者补丁](patches/0164-ime-functional-consumer.patch)以提交 `5237282ed4cbb1fcb92f43d8f294bc504820d8d8` 为基线：20个源文件中9项真实变化、11项原样，原文件的继承换行差异未混入。补丁按仓库 LF 表示保存，79,717字节，SHA `c4d081445c8d61d196bf26fda0dca1e2a77fefba4a15d274a6aa5d93d1da39cb`，git apply --check 实际0；原始79,732字节补丁及其逐源清单另有独立散列保留。它是测试消费者源码，不是默认应用的 SDK 功能。

原件位置以本地验收归档 `PR129-performance-20261104/repair-r32-ime-functional-r1/` 为根：`build-r1/post-build-verification-r2/`、`os-entry-r1/run-once/`、`os-ime-actual-analysis-r1.json`、`api-repair-r2/entry-r1/run-once/`、`api-repair-r2/actual-api-analysis-r1.json`、`repro-source-r1/`。机器记录绑定各必要原件 SHA。

## 仍未关闭

OS IME 的 P≠F late payload 经确切 SDK 包链后的保真、同一 Windows 消息双 flags、非折叠选择区、导航和关闭重入、popup/生命周期、x86真实交互及更多输入法仍未认证。真实输入到可见延迟、有效帧/物理显示、I02/W02完整决策、配对 CPU/完整内存和 DPI/多屏门槛继续开放。本轮没有新增产品 runtime/ZIP，不能把测试 EXE 或 R31 私有 DLL 增量当交付体积结果。

所有任务窗口已正常关闭；没有启动 Electron，也未重复 FPS 矩阵。`performanceVerified=false`、`inputLatencyVerified=false`、`releaseReady=false`、`wholeGoalComplete=false`，PR129继续保持 draft。
