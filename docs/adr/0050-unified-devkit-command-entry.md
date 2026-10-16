# ADR 0050：统一开发入口的有限监督与交付能力

## 背景

公开 create/doctor/build/test/package 和 SDK install 已有真实实现，但分散入口没有统一参数、错误与派发契约。标准 build 已提供独立 owned 政策；现有 Test.ps1 桌面段使用不同进程内互斥及根 PID 生命周期，不能由另一个持有同名 mutex 的进程简单包装后宣称完整监督。Core SDK 当前不交付统一入口，也不包含模板/Workbench/完整工具准备源码。

## 决定

新增 Soui.ps1 持有实际 Local\SouiDevKit.DesktopValidation，Node 内部仅严格派发已有实现。每个实际 stage 都通过现有 owned-process/OwnedProcessRunner；读取实际 raw/context/finished 核对唯一身份、命令和自然终态。源码 build 复用标准 caller 的原 compiler-tail 政策并单独保存它的 verdict；其他命令无尾接受。兼容旧入口，不改变公共 SDK 身份、持久安装事务、同版本内容限制或业务授权。

新增真实 Vite loopback preview worker：原配置及真实 watch/HMR，固定时限正常 await close；中断用本轮私有 nonce 控制后必要时停止本 Job。没有 native Bridge，不能据浏览器预览授予宿主资格。默认没有网络依赖安装。

managedFiles 只增加 entry/Doctor/说明，helper 随原 tools/internal 根交付。生成项目显式借完整 source devkit 的 prepared validation tools；它仍构建自己的锁定 SDK，不 fallback 到另一套 SDK source。完整 create 和发布包装仅在完整源码根提供。自升级从独立完整 devkit 执行，避免替换正在执行的入口。

桌面迁移复用共用 desktop-smoke helper。独立 Test.ps1 在自己的既有租约内调用它；Soui 已持租约，先监督源码 UnitOnly 再直接派发 helper，不调用完整 Test.ps1 的第二租约。每个应用自然闭合、日志就绪/关闭及真实参数独立核验；controller 闭合由外层另验。保留 native-smoke/v2 构建/源码/载荷语义，并新增可重读的 raw/context/finished 扩展；旧报告和应用日志先保存到本轮独立目录。任何 compiler tail、timeout、STOP、缺失日志或身份漂移仍失败。

## 备选与兼容性

拒绝直接拼接 shell 字符串、未知参数透传、按进程名清尾、只停止根 PID，或将 compiler root0 推成所有 native/test 成功。另一套 SDK installer/package identity 增加并行事务与迁移风险，因此复用原 install。增加 managedFiles 不要求旧 lock 包含新入口；旧已安装 identity 仍可读取，但新包内容沿原同版本限制处理。

mutex 与 nonce 约束合作入口，不认证同权限用户，也不能阻止用户手动运行旧脚本或修改整套证据。输出包含阶段/code/repair，不为旧入口的未迁移路径伪造成功。

## 验证与迁移

先由 Root 审查并在独立完整 overlay 中执行纯参数/派发、synthetic receipt reader 与真实 pack/install 小测试；这些不执行 GUI 或证明 native Job。新增 resource/session/devkit reader 测试必须已采用，缺失即失败。然后 Root 串行实际准备工具并测试 busy lease、stage 参数、JSON、Vite资源改动、自然 budget close、用户中断和 installed/relocated 入口。桌面迁移还需实际 normal SDK/source/project smoke、旧证据复核及失败回归。UnitOnly 可能重编译正在用的 prepared runner，入口在该阶段前后复核字节并保留锁定/替换失败；不偷偷另选 runner。独立 Test.ps1 的旧前置单元命令仍未全部监督，05-09a 继续待验收。此候选全部未执行，所有完整资格保持 false。
