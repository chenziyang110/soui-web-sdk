# preview.9 渲染边界与能力发现验证

日期：2026-10-02。对应 [ADR-0012](../adr/0012-renderer-boundary-and-capability-discovery.md)。本轮范围内的本地验证已通过，未发布；`releaseReady=false`。

## 验证范围

公共宿主常规操作经适配器转发；未实现能力明确拒绝；关闭失败可重试且销毁保持幂等。`bridge.describe` 只返回当前会话有权访问的已注册方法；Web 客户端验证并有界分页；模板在上报就绪前执行真实发现。

## 当前结果

| 检查 | 结果与范围 |
|---|---|
| 环境检查 | Windows 开发机：MSVC 19.44、Windows SDK 10.0.19041.0、Node 24.14.0、CMake 4.2.3；Doctor 通过 |
| Release 构建及安装 | x86/x64 的 SDK、模板、Workbench 与测试目标通过；公共头文件随 CMake 安装 |
| SDK 包与交付检查 | 每次 Test：SDK 包14/14，交付/ABI等12/12 |
| Web/验收协议回归 | 每次 Test：69通过、0失败、1跳过；跳过项要求配置额外的x86调试器，不能视为通过 |
| 原生回归 | x86/x64 各7/7通过，包括新宿主适配器、发现契约及现有公共任务 |
| 真实桌面启动 | 每架构模板、Workbench默认、Workbench React共3个场景均通过；模板在就绪前完成 `discover()` |
| 安装后的独立 CMake 消费者 | `tests/package-consumer` 对已安装x86/x64 SDK独立配置、编译、运行，退出0 |
| 仓库外生成项目 | 新目录生成 `AgentFoundationCheck`，锁定410项SDK文件；x64 Release独立构建及真实 `--smoke-test` 通过，退出0 |
| 包源码一致性 | 本地 `dist/sdk/0.3.0-preview.9` 与当前SDK源码匹配；未公开发布 |
| 独立静态审查 | 发现并修正 close→destroy 遗留生命周期订阅问题；增加回归后，审查未发现未解决的确定性问题 |

主仓库两架构使用相同构建输入摘要：`3868638e3088c079a2ab295bafe4e59d85b88cd04456c648fe19a3f97f1acb96`。x64构建ID为 `354e0c05-9e08-47bf-b569-9fc82fc08ca0`，x86为 `68af287c-ba37-4204-a801-1e6a95dc18f4`。独立生成项目构建ID为 `5c9a7aac-9009-44ae-8ff6-50c087152bc1`，其摘要与主仓库不同，因为包含替换名称后的独立业务源码。

构建及启动记录包含源码与完整载荷身份，见 [证据目录](../../verification/production-validation/agent-foundation-2026-10-02/README.md)。日志将开发机根路径归一化为 `<SDK_ROOT>`，并统一为 LF 换行；原始副本保留在本地忽略目录，结果、计数与错误未改写。仓库外完整编译仍有上游SOUI头文件的既有类型转换警告，本轮没有更改第三方源码。

主要复验入口（从交付根目录执行）：

```powershell
powershell.exe -File tools/Doctor.ps1
powershell.exe -File tools/Build.ps1 -Architecture x64 -Configuration Release -Install
powershell.exe -File tools/Test.ps1 -Architecture x64 -Configuration Release
powershell.exe -File tools/Build.ps1 -Architecture x86 -Configuration Release -Install
powershell.exe -File tools/Test.ps1 -Architecture x86 -Configuration Release
```

## 尚未覆盖

本轮未执行Debug配置、底层依赖重编译、WebView2/第二后端、完整界面语义与Agent操作协议、Electron性能对齐、8小时持续运行、GPU资源收敛、目标系统全矩阵、正式签名与完整发布准入。历史 `verification/acceptance.json` 不自动变成本版本的验收证据。
