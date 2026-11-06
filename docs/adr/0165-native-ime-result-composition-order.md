# ADR-0165：原生 IME 复合消息的结果与预编辑顺序

状态：未采用的限定原型；源码反例与无活动组合的 provider 保护已实际验证，活动回调及旧 provider 兼容性仍未认证。两个补丁独立保存，未修改默认 SDK/内核；不声明性能收益。

## 背景与依据

R32 已取得正常中文提交、取消和失焦后 native commit 交付证据，以及两个真实 Blink API 的 P≠F 配方；它没有取得同一 Windows 消息同时携带结果和当前预编辑的原件。因此不能据 R32 宣称双标志已验证。

[WM_IME_COMPOSITION 的官方契约](https://learn.microsoft.com/en-us/windows/win32/intl/wm-ime-composition)允许 lParam 同时包含多个 GCS 标志；RESULTSTR 与 COMPSTR 分别代表结果和当前预编辑。[Microsoft DXUT 示例](https://github.com/microsoft/DXUT/blob/main/Optional/ImeUi.cpp#L1189-L1209)用两个独立分支先处理结果，再处理 composition。

提交 `130f7df69af4137a5dd69b787ace9512cce2c6e3` 的 SDK 先发布 ime_update、后发布 ime_commit。已有预编辑 ni，同消息 RESULT=你、COMP=ha 时，旧顺序先用 ha 替换旧预编辑，再由 commit 替换成你；新预编辑消失。这是可执行源码反例的预期，不能在执行前称为实际 OS 复现。

仅交换分支还不够：ime_commit 设置 composing=false/committed=true，之后 ime_update 未清 committed。新的 ha 会成为 composing=true/committed=true，随后 END 跳过取消。状态还在 provider 调用后写入，可能覆盖该调用同步触发的生命周期清理。

独立审查进一步指出：结果先提交后，若空 COMPSTR 到达且结果的 input 回调已选中普通文字，旧 provider 的空 setComposition 会删除当前 selection。现有 wkeCancelComposition 也委托该空 setter，不能当作安全替代。无实际 composition 时必须是无副作用的 no-op；活动 composition 的删除还须防止 compositionend 回调切换选择区、文档或建立新 composition。单纯 SDK bool 不能认证真实 provider 状态，须先完成真实原语反例与安全边界，再决定最终实现；CPU 替身不证明 Blink 安全。

随后 R33 私有真实原语探针已确认该误删：run b71a2fbc88863525af89b81e1f99b9ed 两次均先确认“你”、再选中0..1，空 setter 和 cancel 均返回true却令文本变空。Host/入口自然0、Job0，双源完整且loss0；这是失败行为的实证，不是安全通过、真实系统双标志或 SDK mailbox 复现。独立分析相对本地归档为 repair-r33-ime-packet-boundary-r1/api-empty-probe/actual-empty-analysis-r1.json，SHA6420f7365fd7b113d86f262d415fcb62d6dbd80c9ee0e1871a53ebb844b3c07d。

provider 的 setComposition 入口增加“空请求且无实际 composition 时直接返回”的私有候选已完成两架构增量编译，并以同一 Host/页面/SDK4 完成一次 x64 原语复验：run417b30d72e813850164f0b717511905f 两次调用均保留“你”和选择区0..1，没有新增空 input；原生/入口自然0、Job0。独立分析 SHA55a1eb5847cddb44e4e75738ec8ec84800b3f69eadea5748a72733b1e0280c3b。新 TU 明确不使用旧 PCH，其原件历史身份以及四个既有库的历史等同性仍未知，不能声称严格全历史单输入差异或配对性能通过。

该候选仍未默认应用。SDK 的 order/state 提案已通过20个源码场景；原实现、仅换顺序、未处理空状态的三个负对照实际分别失败于指定反例。初次夹具编译因 WinAPI 替身错误地依赖 this 而失败；只修外部替身后，原生产片段和全部断言不变，四个实际退出为0/1/1/1。活动分支保持原字节不等于其回调安全已认证；不能用这四行关闭 active reentry、ABA、plugin、真实系统双标志或完整输入法门槛。

源码提案见[SDK消息顺序及状态补丁](../performance/patches/0165-sdk-ime-packet-boundary.patch)和[provider无活动组合保护补丁](../performance/patches/0165-provider-no-composition-empty-guard.patch)。二者均未应用；原工作中 SDK 差异在精确保存、补丁检查与候选字节复核后移出产品源码。冻结 preview.4 的 SDK、内核及包保持原身份，后续采用仍须新开发版本和适用验证。[本轮报告](../performance/sdk-preview4-ime-boundary-20261106.md)记录失败、有限通过与未验范围。

## 决定

1. 在一个原生消息内先处理 RESULTSTR，再独立处理 COMPSTR；在 publication 前确定语义顺序，已发布的全局 ordinal、mailbox FIFO 与恢复规则不变。允许只有任一标志，不用 else-if 丢掉另一字段。
2. 每次开始新的 ime_update 时，在 provider 回调之前按文本是否非空设置 composing，并清 committed。空更新完成清除后不再由 END 重复取消；非空新预编辑则在 END 正确取消，不能误删已提交文字。同步清理后的状态也不被旧调用返回重新覆盖。
3. 保留现有 IMC 获取/释放、字节上限、负数/奇数/短读拒绝、cursor 与 generation 校验、失败终态和取消恢复。结果读取失败仍沿原失败即恢复路径停止该消息；结果成功后若预编辑读取失败，已提交的结果不被丢弃，失败字段仍明确拒绝并走原取消路径。不增加跨字段原子提交承诺。
4. 不改公共 ABI、input_kind、队列容量、通知策略、共享预算、provider 焦点实现或帧率。不把本修复解释为性能收益，不在此处理其他 IME 标志或重新定义取消消息。

## 验证

- 将真实 UI 三个 IME case 与 Engine 四个 IME case逐字投影进最小 CPU fixture；只有 IMC/provider 外围为显式替身，不能称为 OS、真实 mailbox 或完整生命周期测试。
- 不同 payload 的双标志反例检查中间值、活动预编辑及 END 后最终值；原实现与仅交换顺序的部分修复必须分别失败，候选必须保留“已提交你＋活动ha”，END 后只保留你。
- 覆盖单标志、start/end、原有缺上下文与长度/读取失败路径、IMC 释放次数、cursor 折叠语义；验证 provider 同步清理后不会由旧返回覆盖状态。
- 编译受影响 SDK 目标时使用原 v143/C++17/SDK19041，两架构适用检查；新输出独立保存。真实双标志消息、非折叠选择、导航/关闭重入及更多输入法仍单独记录未验证。

## 备选、兼容与回退

保持旧顺序会保留可辨别的预编辑生命周期错误；仅交换顺序会留下 committed 状态错误，均不作为完整修复。用全局 cancel、丢弃第二字段或合并 IME 包会改变其他输入/终态，拒绝采用。

公共接入与普通单标志行为不变；可能改变此前错误处理的复合消息结果。SDK 内容变化必须进入新的开发版本并同步 CHANGELOG/版本入口，不覆盖冻结的 preview.4 SDK 或旧证据。回退使用先前完整匹配 SDK/运行库；本修复不授权导入 R31 私有内核、发布或扩大任何输入/帧率/生产资格。
