# 输入 mailbox 回归源码

`fixture.cpp` 覆盖普通/边沿独立额度、总文本字节、分配失败、FIFO 后恢复、按住键隔离、IME commit、关闭和 Engine 32 Host 注册/轮转。测试项 `sdk.native.input-mailbox` 由根测试配置接入；Root 负责注册。

当前未编译、未执行。专人使用对应源码重编 SDK 后执行 `ctest --test-dir out/<当前构建目录> -C Release -R sdk.native.input-mailbox --output-on-failure`，并补真实 IME、capture/focus、导航/关闭竞争与两架构。此源码不能证明真实输入延迟或性能。
