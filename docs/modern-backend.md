# 现代桌面后端与运行档位

0.4.0-preview.1 增加可选 `Soui::WebView2`，基础 `Soui::Sdk` 仍不链接 WebView2。兼容模板维持 Chrome 49 构建目标。现代模板使用独立 React/Vite 锁文件和 Chrome 120 构建目标；实际浏览器接口要求还在启动时验证。当前现代模板版本为 React 19.3.0、Vite 8.3.3（2026-10-08 从 npm 注册表核对）。

## 构建与独立新项目

现代前端开发机使用 Node.js 22.12 或以上的受支持版本；本次验证使用 Node.js 24.14。最终用户机器不需要安装 Node.js。完整研发仓库含较深的历史验收路径，Windows 上克隆时使用 `git -c core.longpaths=true clone <仓库地址>`；此设置仅作用于该次克隆，不要求修改全局 Git 配置。

```powershell
$devkit=(Get-Location).Path
powershell.exe -File tools/Build.ps1 -Architecture x64 -WithWebView2 -Install
powershell.exe -File tools/New-Project.ps1 -Name MyModernApp -OutputRoot ..\MyModernApp -Profile modern
cd ..\MyModernApp
powershell.exe -File tools/Build.ps1 -Architecture x64
powershell.exe -File tools/Test-Modern.ps1 -Architecture x64 -DevkitRoot $devkit
```

生成的项目记录 `runtimeProfile: modern`，构建工具自动恢复散列固定的 Microsoft WebView2 SDK。准备离线开发机时须预先提供 `out/webview2-deps/<固定版本>/` 缓存。最终用户运行不需要 Node.js、CMake 或 Visual Studio。WebView2 Runtime 需要单独安装；不自动下载、不以旧内核静默代替现代功能。公开适配器构造函数支持调用方提供绝对路径的 Fixed Runtime，但具体离线运行时仍须单独验收和审查再分发条件。

只有基础组件的 SDK 安装不提供 `Soui::WebView2`。独立 CMake 消费者应使用：

```cmake
find_package(SouiSdk 0.4.0 EXACT CONFIG REQUIRED COMPONENTS WebView2)
target_link_libraries(MyApp PRIVATE Soui::WebView2)
```

可选静态 loader、SDK 头文件及授权材料随安装目录搬移，不引用开发目录。源 SDK 本身仍采用原有固定包身份和升级回退机制。现代应用运行包没有 Miniblink/SOUI DLL 要求，但浏览器运行时、VC/UCRT 和 Web 资源的总体体积必须单列计算。

## 公共宿主使用

包含 `<soui/host/webview2_host.h>`，在 UI STA 创建 `webview2_adapter(owner, profile)`，其中 profile 是受控资源目录之外的绝对路径。`prepare_async` 成功回调后才调用公共 `webview_host::start`。准备失败或显式取消均完成一次通知；消费者不能在回调中抛异常。创建环境和 Controller 的失败通过 `status()` 保留。

窗口处理 `webview2_event_message` 时调用 `host.pump()`。SDK 合并通知、每次有界发送，工作线程不调用浏览器 COM，不需要每 16 ms 空轮询。`show()`、`hide()` 不更换业务会话；调整窗口大小调用 `resize()`，获得焦点调用 `focus()`。

关闭返回 `E_BUSY` 时，保留 Host 和 Bridge 并稍后重试。成功关闭后才能销毁，Bridge 必须比 Host 存活更久。同步业务处理器必须有界返回；长任务使用公共 task 服务。析构不能抢占任意阻塞的 C++ 处理器。

收到通知并泵送后检查适配器 `status()`，呈现进程故障时向用户显示错误并完成关闭。现代模板提供此路径及 Per-Monitor V2 清单、`WM_DPICHANGED` 尺寸调整；实际输入法和跨屏行为仍需目标设备验证。现代项目安装或回退时，安装器在修改文件前拒绝缺少现代组件的 SDK 包；兼容项目可继续按已有升级/回退流程使用旧版本。

页面只加载受控本地映射地址；拒绝外部导航、子框架导航、下载、弹窗和浏览器权限。Web 页面声明不能变成原生用户手势。导航和重载撤销旧会话。进程故障不自动重放写入操作；完整崩溃恢复与用户状态恢复仍需应用策略及独立验收。

## 环境与前端能力

`inspect_runtime_environment()` 读取实际系统版本、进程/系统架构、MiniNT 标记和远程会话状态；`select_runtime_profile()` 使用调用方已探测的后端可用性选择候选。选择成功不代表目标机器已认证；控制器接口、驱动和业务需求仍须验证。PE 不假设跨架构兼容层，现代要求在旧系统和 PE 上明确拒绝。

Web SDK 的 `inspectSouiWebFeatures()` 提供 JS 接口和 CSS 语法能力报告。`requireSouiWebFeatures([...])` 在界面挂载前拒绝缺失或无法验证的能力。报告只表示接口/语法探测；存在 WebGPU 接口不代表设备可用，任何字段都不能授权原生操作。不存在自动把任意新 CSS/Web API 转换成旧内核完整实现的保证。

`SouiRuntimeDoctor.exe` 是独立原生环境诊断工具，编译时静态链接 CRT；没有 Node、PowerShell 或浏览器运行时也能启动诊断。输出的兼容结论始终是 `unverified`，实际 Win7/PE 运行证据另行采集。

## 验收边界

发布门槛见 [0.4 执行清单](roadmap/0.4.0.md)。现代模板首次真实启动记录不等于性能、8 小时稳定性、Win7 或 PE 通过。旧内核 GPU 默认开关保持关闭。现代组件不会提高兼容应用的最低系统要求。

## Modern 启动检查的进程与日志边界

公开入口仍为 tools/Test-Modern.ps1。它持有桌面验收租约，委托随 SDK 交付的 tools/internal/modern-smoke.cjs，使用已有 OwnedProcessRunner；应用预算为原有 40 秒，退出后的整树收口预算为 2 秒。超时、取消、监督器错误、强制结束或 Job 中仍有进程均不能通过。检查不会自动编译或用另一份工具替代缺失的准备结果。

完整开发目录必须事先准备相同架构与编译档位的测量工具，并形成当前应用构建证明。生成项目可显式传入 -DevkitRoot <完整开发目录> 读取已经准备的工具；这个目录由调用者指定，不能由验收结果选择任意脚本。默认 MSVC 检查不接受 LLVM 参数。LLVM 调用须传入 -NativeToolchain LLVM22MSVC142、-LlvmRoot、-MsvcRoot、-WindowsSdkRoot、-NinjaExe、-WindowsCmdExe、-PowerShellExe，以及可选 -ToolchainRequest；省略请求路径时读取当前应用的 out/<架构>/llvm-profile-request-<配置>.json。六个路径必须与已有请求逐项一致，并通过现有构建观察器。生成项目的公开 LLVM 构建能力仍受既有控制器限制，这个启动检查不会补造接受证明。

模板用系统 LocalApplicationData 已知目录和实际进程 ID 写 smoke.log；仅修改 LOCALAPPDATA 环境变量不能证明日志重定向。检查在启动前记录该模板的既存 smoke 目录，出现同 PID 的旧目录便失败并保留旧文件。新目录与日志的文件创建时间须跟随 Owned 公布的实际进程 creationFILETIME，再以保留的日志字节验证 process-start、真实 Runtime 版本、application-ready、host-close-complete 的顺序。读取后的身份或内容发生变化也失败。旧 PID-only 的 soui.modern-smoke/1 结果不能用于新检查。

这个检查只证明当前模板的一次启动、页面就绪和应用及其 Owned 进程树自然退出。日志没有浏览器身份或 Host owner 的 FILETIME，不能补造这些字段。外层 PowerShell/Node 调用者的完整收口、无关进程独立观察、25 分钟续期、性能和长期稳定性需要单独的真实证据，releaseReady 始终为 false。工具或源码变化后须重新形成受影响的构建和准备证明，历史失败结果不得改写成通过。

检查从开始到结束必须绑定同一份构建收据。即使源码与应用载荷相同，只刷新 buildId 的重新构建也会使本次检查失败；须重新执行检查，不能把已有进程与日志改挂到新的构建。入口保留开始和结束时的原始构建收据，只有内容及受控身份一致、且运行后的当前构建仍有效，才报告成功。漂移失败仍保留原始进程监督和日志记录。

SDK 的现有交付规则递归包含 tools/internal，并安装包清单中的文件；无需为 modern-smoke.cjs 再添加单文件白名单。须在源码采用后重新生成并验证实际 SDK 包及生成项目，不能将旧包或仅有源码清单视为新检查器已交付。
