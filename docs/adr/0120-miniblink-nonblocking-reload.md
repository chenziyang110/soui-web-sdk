# ADR 0120：MiniBlink reload 保留原任务并返回 pending

日期：2026-10-22。状态：已实现，局部回归与 x64 Release 下第一暂停退出后的真实公共 reload/新文档隔离切片通过；暂停栈内 reload、重建和完整 Stage0 A 未验证。

## 背景与决定

公共 adapter.reload 仍通过 invoke_lifecycle 在 UI 等待 future.get。共享引擎
暂停，或 __souiBeforeReload 在任务内暂停时，UI 无法返回。该路径由实时源码
确认，不能只给等待加超时，因为任务捕获了栈上的 result 引用。

仅将 reload 改为适配器持有的单个操作：原 receipt、owner、完成票和结果。
任务仍在普通 FIFO，第一次接纳后只提交一次；重复 reload 以零等待查询原结果。
未完成返回 E_RELOAD_PENDING，容量不足也保留原已撤权的转换以便重试。
真正完成才返回原成功/错误，Host 仍只在成功后更新会话一次。pump 不消费结果。
Workbench 的两个现有消费者显式轮询 pending，不重置原 ready/smoke 总期限。

保留双 invalidate、broker request→engine arm→新 context ACK 和原文档撤权。
引擎调用前及每次可重入调用后核对原 receipt 与 close 标志；旧任务不能作用于
新对象。UI 关闭撤回任务准入，但保留操作、adapter、Bridge 与原 view 到任务
真实返回及既有清理屏障完成；不提前释放，也不强制恢复暂停或泵业务 FIFO。
生命周期遵循实际 HWND/继续操作 owner；引擎线程不得同步调用 UI reload。

## 兼容与替代

不改变通用 invoke_lifecycle，不为其它栈捕获制造悬空引用；不增加超时取消、
新队列或公共 DevTools 权限。现有 Host 转发失败/pending 不轮换会话的契约保留。
源码消费者必须处理 E_RELOAD_PENDING：保留原对象并在后续 UI 轮次调用 reload
直到最终结果，或按原 close/destroy 保活约定收尾。私有类布局改变需重新编译；
不替换已发布 0.5.0 安装内容。

## 验证边界

真实验证复用既有公共 SDK 暂停关闭消费者：第一次 inspector 已严格退休且
原 A 返回状态已核对后，由原生菜单授权执行公共 `Host::reload()`，在原 UI owner
轮询同一 pending 操作，原 30/40 秒不重置。成功返回后仍等待实际新文档 ready/ACK，
再借用新的 context receipt 和 document identity；旧 receipt 必须失效。新的 inspector
建立前，用旧目标 identity 的 open 必须精确 Stale，不能被已有 session 的 Busy 掩盖；
新会话真暂停时继续检查旧 session 消息不影响新暂停。期间不以旧 context 调用 JS。
此切片不声称覆盖暂停栈内 reload 或目标重建。

实际 run `51a54bd95abd484aac10722558ce111f` / PID 3652 在相同 SDK 库、内核的
x64 Release 下自然退出 0，grant 到完成 20953 ms，完整 owner 39823 ms，原 30/40 秒
不变。原生 reload 授权在 30578 ms，公共调用同轮返回 pending 且旧 receipt 4 已拒绝；
30625 ms 返回成功，但尚未拿到新 context；30688 ms 才取得实际 ready/ACK，receipt
10、documentGeneration 2→3，仍为原 Host、view、engine。30735 ms 旧 target open
精确 Stale 3，且未创建 session；新会话暂停后旧四项操作仍为 Stale，新暂停保持三次
心跳。明确的非原生诊断收尾后，脚本返回、pause/dispatch 平衡、前端严格排空、新
A 返回状态及 A/B/Runtime 退休均完成。余下 177 ms 不是性能裕量保证，不补跑以改结果。
app `f54f6b03...` / EXE `33a19e00...` 的原始证据位于
`out/devtools-stage0/miniblink-sdk-close/same-a-reload-2026-10-22T11-32-44-301961Z/guest-ec49aaec11b44dbd91130d836f3f90b1/`。

沿用编译实际 adapter 的专用无 DLL 测试目标，通过测试宏绑定真实 engine 和
不解引用的 opaque view、原类型的 native 函数 spy。阻塞真实 FIFO，直接调用
public reload，要求 UI 返回 pending、只排队一次；释放后核对 owner/次数和真实
清理。另验证 close 使排队 reload 失效及满队列重试。测试恢复 watchdog
只让旧实现返回明确失败，不是生产超时或强杀成功。

局部测试不证明真实 inspector、V8 暂停、broker realm、重建或安装消费者行为。
上述实际切片以外的暂停中 reload/重建及完整 Stage0 A 仍须验证；不冻结公共 API。
第一代 inspector 退休、旧 A 完整关闭后重建新 Host 的隔离已在另一冻结候选单独验证，
数值 receipt/generation 复用与 endpoint 身份边界见 ADR 0118；不转挂到本 reload 候选。

2026-10-22 本机 x64 Release 实测：原实现编译成功后，新增用例在预期的 UI
阻塞断言失败（CTest 退出 8，约 2.94 秒）。修复后实际 SDK 和相关目标构建成功，
reload、两项 unmount、Host 转发和引擎诊断五个 CTest 通过，合计约 1.73 秒。
三个 reload 场景在同一注册用例内执行；没有用测试 watchdog 释放后的运行冒充
非阻塞通过。原始结果与源码/库/EXE 字节在
out/devtools-stage0/miniblink-reload-unit/red、green 保留。

独立只读审查发现调用方在 future 完成但页面尚未 ready 后，可漏检下一轮晚到
ready。两处现保留独立 waiting-ready 状态到实际就绪，getter 返回后复核原期限；
不清除超时起点，也不增加 15/45/180 秒预算。修复后仅重编 Workbench main.cpp
（包含 resident_controller.h）对象，编译退出 0，约 7.36 秒；没有重新链接、部署
或运行应用。最终有限复核未留下确定问题，SDK 已通过的五项没有为此重复执行。

SDK 局部构建使用 v143、MSVC 19.44、Windows SDK 10.0.26100.0；Workbench 对象
使用原项目的 v143/Windows SDK 10.0.19041.0。保留既有编译警告，不作无警告或
其它配置/架构/系统兼容性声明。完整索引见同目录 verified-local.json。
