# preview.4：输入覆盖与坐标诊断

**真实输入延迟验收仍失败，尚不能给出完整输入到画面的分位数或性能收益。** 此报告接续[原生入口复验](sdk-preview4-native-preflight-20261106.md)，保留旧失败。以下证据路径相对于本地归档 `PR129-performance-20261104/`。

冻结产品仍为 `0.7.0-preview.4`、x64 Release，SDK source SHA `e1d2dd8b7699930f678e21a77772b8fa4e371f5b67a53937d98470ee1659e87c`、build ID `18e47fa2-69df-4530-b1f9-365f6b2aec28`、stock node.dll SHA `56d460c0b4136d85d3fcb8d4e4f00c19a04ff56532228031b55368742ef54308`。本轮只改独立测量页面、读取器和输入器；没有重建或覆盖 SDK、内核、runtime、ZIP。

## 缺失 DOM 属性的诊断修正

实际旧 MiniBlink 回调没有 `isTrusted` 属性。原 v3 页面因此置 invalid 并画出 nonce0，观察器正确拒绝初始标记。运行账本和匹配源码支持属性缺失，不能将其改写为 true。v4 页面只兼容“属性不存在且值为 undefined”，记录 raw -1、typeof 和存在位；false、null、字符串、读取异常及错误作用域继续拒绝。固定页面不能合成事件或改写信任属性，当前 TLS 只支持该闭集中的原生派发归因，不是任意页面的事件认证。

R37 仅复用已构建的同步 Host，新增六项 CPU 用例首次5通过/1失败；失败来自 mock 默认参数吞掉显式 undefined。只修正 mock 后，对失败用例单独复验通过；没有声称执行了一次最终6/6全组，也没有重跑旧全套。

## 两次已结束的实际采样

| 项目 | R37 v4 | R38 坐标旁证 |
|---|---:|---:|
| run | `0790ebfd55add348ec99cab8eb956ff5` | `8ab6255a5078de0682e6758151dc59ce` |
| SendInput 返回1 | 64/64 | 64/64 |
| 原生入口带标记回调 | 56 | 56 |
| 带标记 T1→T3→正常T4→getter | 56 | 56 |
| 实际保存的带标记 DOM 回调 | 56 | unknown：页面未保存快照 |
| 最终持久反馈槽 | 55/64 | 0/64 |
| 首次置槽且标签精确一致 | 35 | unknown |
| Host / input / 入口退出 | 0 / 1 / 1 | 2 / 1 / 1 |
| Host / input 时长 | 11.396 / 1.117秒 | 16.473 / 1.122秒 |
| SDK / vendor 详细点数 | 1,674 / 9,606 | 2,142 / 11,071 |

两轮 trace 均完整、loss0、fault为空，所有自有 Job 自然清空、无强制结束、Runtime正常关闭。最终500ms ACK门均失败，入口保持非零。两轮缺失的原生标签均为9、16、24、31、39、46、54、61；不是SDK接纳后的额外丢失证明，也没有据此定位操作系统、DPI或内核的根因。

R37 的84个实际 DOM 回调和86次 getter全部保留，含tag0、outside和重复。55槽中20个firstsetter的T1标签与槽号不一致，因此不能用“页面完成”代替64项因果门。仅对56个实际收到的标记样本作 SDK 阶段诊断：T1→T3的P95为0.9784ms，T3→T4的P95为0.4237ms；这是轻负载、带探针子集，不能补齐缺少的8项、推导完整尾延迟或声称优化收益。

R38 独立输入器实际局部构建67.329秒、退出0，MSVC v143 14.44 / Windows SDK19041，只编译两个输入器TU。新EXE 152,576 B，相比旧诊断工具增加21,504 B，没有新增DLL，不属于产品体积增长。输入期间只保存预分配的实际归一化参数，不增加查询API、等待或IO；原坐标公式、125Hz频率、64次工作量、flags和截止不变。

R38 实际64组参数均符合冻结整数式。首项请求client100/100、全局188/211，记录的归一化参数3217/12882，而原生回调为client99/99、当时GetCursorPos为187/210。后者是API读取值，不是校准过的物理像素真值。实际thread/window均SystemAware、DPI96；使用的虚拟桌面为0/-1、3840×1081。这些记录仍不足以证明准确逆映射或失败原因。

R38所有56个标记原生点的y均为99；固定页面网格要求y≥100，源码支持其不置槽的解释。但本轮没有保存DOM坐标与回调结果，不能把源码推断写成实际DOM账本。67次捕获的nonce/CRC有效、最终mask全零；67次捕获不是67个独立呈现帧。

两轮实际显示配置不同：R37主输出DISPLAY1/60Hz，R38主输出DISPLAY5/100Hz，后者副输出约74.986Hz。配置变化原因未知，不能跨轮计算改善、噪声或把配置刷新率当物理帧率。

## 有限下一步与资格边界

[ADR-0166](../adr/0166-synchronous-input-dispatch-witness.md)记录独立审查有条件允许的新v5夹具：用预先冻结的64个不重叠cell编码序号，同时保留全部64标签、tuple＋ID、T3/T4、getter、DOM ordinal/firstsetter、完整账本、正常关闭及桌面CRC门，新增实际DOM坐标与对应原生坐标严格相等。它不修复或关闭旧精准点失败，也不允许缺少标签；不得把不同轨迹与旧样本配对计算收益。新夹具尚未执行通过，之后基线和候选必须使用同一新负载。

独立实际收据分别为：

- `repair-r35-sync-input-witness-r1/recovery-dom-capability-r3/entry-r1/actual-v4-coverage-review-r1.json`，SHA `d8a42fe73ab0fcb3f45c328cca2870eb577edf814d1a5b025ca04cf9a6e8a991`。
- `repair-r38-input-coordinate-evidence-r1/entry-r2/actual-coordinate-environment-review-r1.json`，SHA `63dba54ec3ccfe3a57947f3d542be904a307fb56fe153f1c785963a25d26fcff`。
- `repair-r38-input-coordinate-evidence-r1/spatial-cells-critic-r1.json`：仅为新设计的条件审查，不是运行或性能通过。

供开发复核的[冻结诊断源码快照](../../verification/performance/sdk-input-frame-20261105/diagnostic-source-r38/README.md)包含27个实际源文件、说明和清单，共29文件、279,101 B，逐字节复制原件，未复制二进制、日志或旧启动器。清单SHA `5663e750d4f11fdc9e71a4f8eb1b0a644a10375c8effe59181186e30d6da1590`。它保留R35/R37/R38身份，不含后续v5，不代表已默认集成或在新目录重新构建通过。

I01/F01/W01实现和既有正确性证据保持；共享预算0、输出准备关闭。完整输入到反馈、有效帧、配对CPU/完整进程树内存、键盘/IME及适用DPI/多屏门槛仍开放。产品体积继续沿用[交付复验](sdk-preview4-delivery-20261105.md)的冻结载荷，不能把诊断工具增量算作SDK增量。`performanceVerified=false`、`inputLatencyVerified=false`、`releaseReady=false`、`wholeGoalComplete=false`。
