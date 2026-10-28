# 0134：受控 Inspector 的键盘事件快照

状态：实现候选；编译与运行留到统一验证阶段。本决策不关闭 D09 或 D01–D14。

SDK 自有的 MiniBlink Inspector 在原生 UI 线程收到键盘消息，再将固定输入记录
排到 Engine。旧记录丢弃原始按键数据，Engine 又用自身线程的 GetKeyState
重建修饰键；该线程不持有原始键盘输入队列。因此记录没有保存事件接收时的
Ctrl/Shift/Alt、开关键状态、扩展键身份和重复状态。

保留现有 32 字节私有输入布局。键盘种类的 message 保存原始
WM_KEYDOWN/UP/CHAR 或 WM_SYSKEYDOWN/UP/CHAR，flags 保存原始
32 位 Win32 lParam，x 保存六位明确的快照：Shift、Control、Alt、
Caps Lock、Num Lock、Meta。y 和 delta 必须为零。未知修饰位或不符合
种类的消息明确拒绝。死键消息不是已提交字符；Windows 翻译与组合产生后续的
字符消息。IME 提交保留现有有界原生缓冲区，经同一快照路径传递。

Engine 在入队前校验固定记录。现有有界受控队列仍是唯一输入队列。投递经过
原有 WebPageImpl 按键入口，通过仅内核内部可选快照参数进入
PlatformEventHandler；可见 popup 分支也传递同一参数。受控事件直接使用
快照，不读取 Engine 键盘状态。没有快照的调用保留普通 WKE 当前语义。没有
引入线程输入附加、进程键盘状态修改、线程局部覆盖、普通 SDK FIFO 泵送，
也没有新增公共 SDK/WKE API。

原有内核 C++ 方法签名保留为转发到空快照的 overload，旧对象文件的符号不因
默认参数变化而消失。新的快照 overload 与 SDK 消费方、受控 Broker、事件
构建和 popup 实现仍须配套重编；不能只替换旧候选中的单个 DLL 或对象文件。

私有 controlled Inspector API 与 session snapshot 从 ABI 2 升到 ABI 3。
布局不变，但新语义需要配套内核；SDK 必须拒绝 ABI 2 内核。独立的 frontend
resource snapshot ABI 仍为 2。普通 WKE 二进制导出不变。已有包 pin 不能证明
ABI 3 二进制已重建或验证；旧候选 DLL 和证据保留原身份。

输入成功表示受控队列接受了事件。队列饱和已有失败关闭路径；意外 busy
或其他拒绝也必须保留有界失败原因并开始受控退休，不能当作已经投递。关闭
拒绝后续输入，保留原清理 owner，沿现有 close/drain 屏障收尾。不增加重试
队列、忙等、容量或期限。

定向回归源码覆盖 wire 一致性、组合修饰键、扩展/重复位、系统键、非法记录
和输入拒绝分类，并使用生产共享的纯映射函数与真实 Blink 键盘 POD 验证 UI
快照覆盖空或相反的环境状态。它们与其余候选源码在统一阶段编译运行。真实 Console/Sources
快捷键、中文 IME、焦点切换、暂停输入、队列饱和与实际 drain 必须使用重建的
配对内核和原生应用验证；源码审查不代表这些运行行为已经通过。

该回归使用完整 vendor 内核源码中的 Blink POD，沿已有
`SOUI_BUILD_VENDOR_TESTS` 开关注册。便携 SDK 的普通构建不新增完整内核源码
依赖；统一验证必须显式启用并执行这项回归。
