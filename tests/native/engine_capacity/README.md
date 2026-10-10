# Engine 容量真实线程回归

一个 EXE、8 个固定 CTest；正常 `Soui::Sdk` archive，真实 `engine_thread`、工作线程、条件变量和 capture 析构。没有复制调度算法、私有编译宏或 TestKit。

父 `tests/native/CMakeLists.txt` 已注册本目录。本目录默认只注册上述纯 Engine 组；真实 mounted Host/kernel 的容量拒绝和关闭保留所有权通过单独的可选组执行，不能从这 8 项推定。现有 metrics 11 个 ID 保留，`engine-fifo` 的容量断言更新为 580。四类排队额度为 512/32/4/32，执行中的一个回调另计。

常规入口 x86/x64 的 Debug/Release 均已通过，见[有限整合回归](../../../verification/production-validation/0.5-runtime-capacity-2026-10-10/README.md)。四个测试目标（包括可选 Host）沿用 SDK 的 /MD、/U_DEBUG 约定。此前真实 Host 安装消费者的有限回归另有记录；最终组合安装消费者、8 小时、资源趋势、目标系统、完整工具链和正式发布仍待验收。
