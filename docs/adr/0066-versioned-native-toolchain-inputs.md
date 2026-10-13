# ADR-0066：版本化原生工具链输入与固定 WinRT 搜索根

- 状态：proposed
- 日期：2026-10-11
- 决策者：维护者 / 评审人
- 关联：[ADR-0023](0023-explicit-windows-sdk-selection.md)、[ADR-0033](0033-standard-owned-build-caller.md)

## 背景

LLVM22MSVC142 的固定输入原使用五个包含目录和十七个必需文件。真实现代 SDK 构建在 WebView2.h 包含 EventToken.h 时失败；所选 Windows SDK 10.0.19041.0 的文件存在于 winrt 目录，但该目录未进入显式 /imsvc 搜索和预先冻结的依赖范围。静默扩大旧身份会让历史请求、缓存和编译证据的含义发生变化。

## 决定

当前 Finder 与 CMake 工具链生成 `soui.llvm-profile-inputs/v3`。包含目录固定为 LLVM、MSVC、SDK ucrt/shared/um/winrt 六项；十八个必需文件在原 Windows.h 之后插入 EventToken.h，其余顺序保持。摘要按 LF 连接十六个标量、八个工具角色、十八个必需文件和两个固定桥文件，共四十四行，无末尾换行。Finder、CMake 与请求读取器独立使用相同顺序。

读取器显式保留 v2 的五目录、十七文件、四十三行合同；未知版本、跨版本数量或成员组合均拒绝。v2 兼容仅允许准确读取历史记录。当前工具链 SHA、完整输入摘要和原缓存身份规则要求重新生成 v3 请求并进行 fresh configure，不能把旧产物或旧 observed 记录改写成新构建证明。

八个工具角色、十二个 CMake 定义、三个库目录、两个固定产品桥及其身份算法保持。编译命令原 /X 与 /imsvc 循环实际接收新增目录；不得借助任意系统包含路径。未来控制须冻结并在执行前后验证全部 winrt 普通成员与字节，实际观察器仍拒绝未被冻结的头文件。

Ninja Multi-Config 的 commands 与 deps 查询都明确选择 `build-<Configuration>.ninja`；否则默认 build.ninja 选择可能与实际对象所属配置不同。该修复只改变 deps 查询的文件选择，保留原 VALID、准确数量、对象集合和原始头文件绑定条件。

## 备选方案

直接沿用 v2 并放宽目录、数量或成员会改变历史身份含义；加入全局 INCLUDE 或依赖隐式系统搜索也无法证明使用了固定 SDK。只加入 EventToken.h 单文件而不冻结 winrt 树不能覆盖它真实依赖的其他头文件。保留默认 Ninja deps 查询则不能保证读取了指定配置的对象证据。

## 影响

不改变 SDK 公开 API、ABI、运行期线程、资源规则或目标系统宏。现有 MSVC143 默认分支不变。当前 LLVM 请求和缓存需要迁移，增加一个明确的 SDK 头文件输入库；历史失败、请求、源码清单及 observed 记录保留原件。

## 迁移与回退

在独立工作区组合已审源码、生成当前 v3 请求，并使用原严格 Owned 预算、2000ms drain、自然 whole-Job 0/0、Job0 和 noForce 条件。新请求或缓存不匹配时拒绝，不通过宽松回退重用旧构建。原 v2 源码组合保持可查；回退使用其原身份和原产物，不修改其收据。

## 验证

保留原十五项 profile 测试，并运行新增四项真实 readRequest 用例：两架构和两版本准确输入、未知版本或数量混合、同数量成员替换、EventToken 缺失或字节变化与摘要失配。源码审查和合成文本夹具不证明编译完成。Root 仍须在两架构、Release/Debug 正常入口及可选现代 SDK 上实际配置、编译、安装和观察，核对六个 /imsvc 根、全部实际头文件和严格退出。配置化 Ninja deps 修复的实际配对查询、现代容量宿主及资源/运行期/Windows 7/完整内核资格分别保留自己的验收，当前组合不宣称这些通过。
