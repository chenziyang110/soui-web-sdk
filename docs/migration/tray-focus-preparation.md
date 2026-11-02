# 托盘焦点准备候选迁移

当前本地候选为 `0.6.1-preview.3`，尚未发布。焦点状态准入和重入提示处理于 `.2`
引入，`.3` 另为两个调用方加入有界 callback 队列。两代分别构建和验证，旧载荷及
失败保持原身份，不能原地替换 DLL/LIB 后沿用旧验收。本说明不提升完整 #27/#29、
Win7 或生产资格。

公共 C++ 签名、owner 线程、HWND/图标所有权和关闭顺序保持。应用结束菜单后仍须
调用并检查 `tray_service::return_focus()`，不能仅以菜单选择或图标可见判断焦点成功。
候选只在初始前台恰为 owner 时，先尝试一次 Shell 激活、一次 WM_NULL 和有界的
GUI 状态准入。其余初始前台保持原 NIM 路径。准备采用共用的绝对 1,000 ms 协作预算，
不使用固定等待作为成功条件，也不构成整个成员调用的硬超时。

`E_TRAY_FOCUS` 可能来自准备阶段的保守拒绝，此时尚未执行 NIM；message 区分该
情况与实际 NIM FALSE。一旦尝试 SFW，未知状态、身份/前台变化或预算耗尽均不会
回退到未经准入的 NIM。实际调用仍只有原始 BOOL 非零才返回成功，不重试，不把
焦点侧效应转换为 API 成功。API TRUE 也不能代替真实图标的 Shift+F10 路由验证。

同步准备允许 WndProc 重入。应用继续把原 TaskbarCreated 消息连同原始参数交给
`dispatch_message()`；无需注册新的公开消息或保存 SDK 指针。只有该已注册消息
返回 handled、无事件、E_BUSY 的精确组合可记为“已延后”，须保留原响应，不能吞掉
其它 E_BUSY 或把后续 ADD/VERSION 失败当通过。旧唤醒不会恢复已移除的身份；投递
失败也不承诺自动重试。准备期间嵌套 remove 只取消本次准备且仍返回 E_BUSY，真正
移除须在成员返回后重试；成员活动期间仍禁止销毁服务和 owner。

安装消费者应使用新 SDK 重新构建。CMake 的 `find_package(SouiSdk 0.6.1 EXACT)`
只检查数值部分，不能区分两个 preview；必须同时核对安装头文件中的完整
`SOUI_SDK_VERSION_STRING`、库散列和实际构建记录。回退时恢复完整旧安装载荷及
其记录，不能混用两代头文件、库和资源。GUI bindings 的精确 preview 声明保留各自
元数据；旧 `.1` 安装/回退规则不因新增 `.2` 而放宽为任意未来版本。

历史 `.2` 已在 Server 和 Win10 上执行双架构安装消费者，另执行 Server Workbench。
七个新 case 的十八次公共 `tray.focus` 响应均成功；原件没有单列 NIM BOOL，不能写成
仪器化的直接 BOOL 采样。两 Server 消费者及第一轮 x86 Workbench 自然退出 0；
新的 x64 Workbench 和两 Win10 消费者出现同步调用期间重入的非延后 `E_BUSY`，
最终退出 1，Win10 x64 还记录菜单激活失败。第一轮 x64 Workbench 超时也保留。
这些结果要求继续修复调用方的串行分派，不能吞掉 `E_BUSY` 或将整个 case 计为通过。
完整经过与未完成项见[维护记录](../maintenance/tray-focus-20261102.md)；实现边界和
失败处理见 [ADR-0012](../adr/0012-public-tray-service.md)。Win7 SP1 OS32 仍未验证。

后续 `.3` 候选为两个应用引入各自拥有的有界 callback 队列；安装后的程序须连同其
自有辅助头重新构建。SDK 的 `.2` busy/BOOL 实现保持，TaskbarCreated 仍立即分派。
仅 tray 成员栈内原 callback 入队，入队不表示 SDK 已成功；安全 timer 每轮最多
处理进入该轮前的八项，实际 response 保留全部失败。退出撤销剩余项目，溢出明确
失败。该 FIFO 不保证所有未入队输入的全局排序，也不改变 Windows 的前台权限。
新版本与旧安装须整套升级或回退；`.2` 原件、库和失败保持原身份。`.3` 双架构
构建及安装消费通过，Server/Win10 六个新 GUI 用例均自然退出 0，十八次公共 focus
响应成功，实际取消、原位键盘菜单和退出由操作记录及截图支持。本轮 queued/replayed
均为零，真实 FIFO 路径尚未覆盖，相关边界由故障注入回归验证。

同一冻结程序随后在 Server 追加两架构正常 Explorer 重启恢复，图标、菜单、原位
键盘焦点、提示和快捷键可用，均自然退出 0。TaskbarCreated 原响应保留
sourceVerified=false，恢复结论结合真实操作/图标和后续 SDK 状态。快速快捷键前台
拒绝、Win7 OS32、其它系统恢复及长期驻留仍未闭合，不继承旧样本或 probe 的资格。
