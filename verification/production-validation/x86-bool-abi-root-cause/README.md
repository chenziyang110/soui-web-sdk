# x86 动态绑定 ABI 根因记录

`56fc6fee-15b9-407c-a6b1-7178c27111c7` 的 x86 公共消费者能够完成 `bridge.bootstrap`，但首次普通调用全部超时。带诊断的复现 `f0ba4128-e502-4a58-88fd-11c9581940af` 显示 Promise 的 `jsValue` 为 `8`，随后被 `jsIsString` 判为字符串，Host 因而在请求入队前返回：

```text
binding.bootstrap id=sdk:bootstrap:1
binding.resolver-created id=...:1:1 value=8 string=1
```

随 SDK 交付的 `third_party/include/miniblink/wke.h` 把 54 个实际导出为 C++ `bool` 的函数指针声明成了 Win32 `BOOL`。x86 调用方读取 32 位返回值，而被调函数只保证布尔返回寄存器的有效部分，导致高位未定义；x64 没有暴露这一问题。真实导出声明以同一内核源码的 `wke/wkedefine.h` 为准。

`c8271244-b7ac-40f5-9d0d-28bc53680ceb` 记录了用 `jsTypeOf` 绕过该函数的反证实验；进程以 `0xC0000005` 退出，因此没有采用规避分支。最终修复对齐全部共享动态函数的返回类型，并增加逐符号头文件 ABI 契约测试。

修复后的 `f841db83-f174-4558-913f-6e02ca6c5126` 在 x86 完成真实 reload，诊断链为 `string=0`、入队、工作线程响应、引擎线程 Promise resolve，进程正常退出。随后源码指纹 `3dcd7b97ea5f75deb752b1940ac34a30875eba6e851b00603ea426e88e1da699` 的 x86/x64 公共消费者四场景各 3 轮全部通过，见相邻 suite。短周期通过不判定资源与长期稳定门。
