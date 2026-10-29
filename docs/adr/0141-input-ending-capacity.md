# ADR 0141：输入开始前预留结束容量，保留单 FIFO 顺序

状态：实现候选，待集中编译与运行验证。日期：2026-10-29。

## 背景与边界

仅报告输入队列 full 不能保证已接受的按下/组合输入有机会结束；直接把输入挪到高优先队列会改变按下、业务、抬起的因果顺序。既有 Engine 只有有界的 ordinary/control/renewal/cleanup 容量和一个 FIFO。本决策复用容量，不增加队列或挪用回复/续租/清理预留。

任意无限输入流无法在固定容量下保证所有字符必达。这里的保证是：接纳有结束边沿的开始之前持有其结束容量；无法建立保障时拒绝开始，并向 Host 如实返回 full/stopped/invalid。没有预留的孤立抬起、普通字符和一次 IME start 后的额外 result 仍走普通接纳，不能冒称拒绝已被恢复。

## 决定

私有 `input_admission` 每 Host 固定保存 256 个键码的 key-up ticket、一个当前左键 pointer-up ticket、一个 session blur ticket、IME 首次 result 与 end 两个 ticket。仅使用 ordinary 的现有 `engine_reply_ticket`，没有增加额度。

- 新 key/pointer down 先预留其 up，再使用 ordinary 正常容量将 down 排入原 FIFO。重复 down 复用已持 up ticket，失败不释放原先已建立的保障。
- focus 接纳前预留 blur。任何 key/pointer/IME start 在缺少 blur ticket 时也预留一张，使前序 focus 被拒绝或合成输入仍能用 accepted blur 结束该 Host 的 session。这里不合成页面 focus 事件。
- IME start 先预留首 commit 与 end（及缺失的 blur）。commit 消费一次 result ticket，额外 commit 的 full 真实上报；end 可以消费 end ticket并释放未使用的 commit ticket。
- 保证结束使用 `try_submit_reply` 消费该 ticket，但仍追加同一 FIFO。即使其他 ordinary 任务占满可用额度，已预留的 up/blur/IME end 不被挤掉。没有更改执行优先级。
- accepted blur 进入 FIFO 后释放该 Host 剩余 held tickets，把 blur 定义为容量 session 的结束边界；不会合成 click 或凭空声称页面键盘状态已修复。后续孤立 up 重新走 ordinary，避免 Alt-tab 后 key-up 属于别的窗口而永久占用原窗口的 credits。
- view/document 变化和显式 reset/unmount 释放旧 ticket；geometry 变化不释放已接纳手势的结束保障。Engine epoch 改变也清理旧绑定。

## 原子性、所有权与重入

预留和普通入队是两次短 Engine 操作，其他 Host 可以在其间使用剩余普通容量；因此开始入队失败必须回滚仅本次新取得的所有 ticket。已有 ticket 不因 repeat 失败被释放。

`try_post_guarded` 在普通入队的同一个 Engine 锁内核对最多三张结束 ticket 的 owner/epoch，而不消费 ticket，防止 reserve 后 Engine stop/start 把开始接收到新 epoch、结束却仍绑定旧 epoch。两张 IME ticket 同时核对。

`input_admission` 使用自己的 mutex 串行化 owner/engine reset 与接纳。成对边沿在触及 ticket 前核对 Engine epoch；普通 motion/wheel/character/IME update 不额外查询 Engine 快照，避免高频移动增加一次 Engine 锁。Engine 私有 `post_internal` 改为引用接纳，新增 retained `try_post/try_submit_reply`：失败保持 `std::function` 所有权，待 input mutex 释放后由外层函数参数析构。这样拒绝捕获的析构可以重入 Host reset，不新增每输入的 shared_ptr 分配，也不在 Engine mutex 里运行用户析构。成功仍是原有非分配 swap 入 FIFO。

## 兼容、体积与验证

公开 Engine 方法签名和队列容量不变；新增接口仅在未安装的 SDK 内部头中。Host 私有内存增加固定 ticket 表，仍须纳入内存与体积预算，不能以无新依赖代替产物验证。未来增加其他 pointer button 时必须对应扩展结束键，不把所有按钮复用当前左键 slot。

`tests/native/input_admission_tests.cpp` 使用真实 Engine 和有界阻塞回调制造 full，检查 FIFO、预留结束、重复 down、geometry、80 次 focus/blur、IME 多 result/无 result、view/document/epoch、回滚、拒绝捕获析构重入、已接受回调 reset、多 Host。仅新增测试源码，未执行。由集成负责人注册 `sdk.native.input-admission`，专人集中编译、运行输入/IME/关闭及体积验收；旧测试结果不能外推此候选。

回退需一起撤销 Host helper 接入和本 ADR 对应的内部引用接纳接口，保留当前未测/失败状态，不使用旧 DLL 证明新行为。
