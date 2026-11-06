# ADR-0151：显式 provider 截止覆盖完成任务退休

日期：2026-10-31。状态：源码与两架构受影响回归通过；匹配内核验证准备中。

## 问题

ADR-0147把显式共享截止传入scheduler/admission，但 `retireCompletedTasksForOwner()` 仍按独立32项批次析构。一次provider机会可在service入口、scheduler前后各执行一批。单个析构不能抢占，但截止已过后仍开始后续多项析构，会延迟input及close/retry。

## 决定

退休函数增加内部可选QPC截止/频率和显式一次最低进展参数。service入口带同一截止并允许一次退休，scheduler前后只在截止未过时退休；每次取出timer前检查，取出后仍锁外deref。旧无参调用、worker及shutdown保持原批次和排空规则。QPC频率0表示原无截止路径，不增加默认热路径采样；WTF仍只判断任务due。

service入口的最低份额只在本次首批使用，不能在三处都重新赠送一次。正常callback最低进展和close lane份额各自保持，避免清理把它们饿死。剩余退休任务继续走现有SharedTimer continuation/recovery，并计入bounded service的workRemaining，让Engine不会无限睡眠；不得因纯future timer或空退休队列自激唤醒。

保留现有无参退休符号作为wrapper，新增私有有参overload，无public/WKE/provider ABI新增、class布局变化。沿用m_retiringCompletedTasks重入guard、先pop后deref和资源屏障，不在锁内析构，不释放仍在执行的task。不抢占单个析构；异常与单项超时仍为原契约限制。

## 验证、备选及回退

实际WebThread源码夹具制造多项已完成但未退休的task，析构可推进受控QPC，验证过期provider整轮只保留一次退休；未来截止在每项之间停止，残留可续接并最终恰好一次释放；ready callback/close路径最低份额、重入、原无参32项及shutdown全排空继续成立。先在原实现复现多项退休超过截止的指定失败，再应用修复。固定源/片段与map/HEX门禁，保留已有精度修复源码，不以旧pins覆盖当前SharedTimer。

备选为只减少固定32项，不能对齐实际截止且会改变默认策略，拒绝。给整个provider提前return会饿死关闭及退休，拒绝。通用V8临时对象扫描另有维护成本与寿命约束，本次不靠跳过该扫描声称覆盖全部不可分服务成本。

回退本次退休参数和调用接线，保留provider/absolute/高精度wake/GDI完成边界。该源码修复不是FPS、真实输入或全production TU证明；新匹配内核另行验证，历史产物结果不重贴。

## 实际结果与保留失败

原实现x64实际编译成功，在过期provider调用中连续退休95个capture，指定`provider-retirement-expired-count-assertion-failed`失败，自然exit3/3且owner/owned group清理完成。候选r2在x86/x64各44/44通过，编译15.618/16.396秒；39选定输入执行前后不变、CTest自然0/0。新案核对过期退休1项、未来截止在第三项后结束、无参32项、不额外读QPC、ready callback最低进展、真实continuation不重复以及shutdown全排空；依赖时钟受控，不证明硬件准时性。

首轮候选43/44失败的原件保留：旧huge-deadline断言只接受普通pool timer，未随a135高精度后端更新。最终同时要求实际pool arm和timer存在，或实际precise arm与timer/wait存在；原目标保留、不提前、取消和清理断言均保留，新增两路arm次数观测。生产还恢复了初次retire之后原有的bounded状态采样位置，避免析构重入改变willExit后使用旧状态；前置退休只使用临时frequency gate。上述实际变化后才执行第二轮两架构44项，没有覆盖原失败。

原SharedTimer全文件fixture pin已按a135/c883真实字节更新，实际精度/waitable后端仍保留，旧pin未用来覆盖生产。源补丁0032独立于0031画布条件，前后SHA与实际收据见相应source record。默认预算仍0；本次不缩小关闭排空，不改变公开ABI，不预称FPS收益。
