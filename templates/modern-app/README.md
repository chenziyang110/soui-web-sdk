# SouiModernStarter

问候动作的唯一来源是 `contracts/actions.json`。生成项目中修改它后执行 `node tools/sdk/actiongen.cjs --source contracts/actions.json --root .`，再执行相同命令附加 `--check`；CMake 只读检查，不自动改写。原仓库从根目录执行 `node tools/sdk/actiongen.cjs --source templates/modern-app/contracts/actions.json --root templates/modern-app`。C++ 使用生成的 `register_Greet`，TS 使用生成的 `callAction` 检查输入/成功结果；`Soui::ActionJson` 仅提供可选 JSON 头文件。静态工具描述不授予权限、不重试；运行时工具必须来自原生授权发现。输出有散列保护，别直接编辑，参见 `docs/generated/actions.md`。

React + TypeScript 页面通过统一 SDK 调用 C++，使用可选 WebView2 后端。面向经过验证的 Windows 10/11 环境；Win7 和 PE 应选择兼容模板并完成目标环境验证。

独立项目运行 `powershell.exe -File tools/Build.ps1 -Architecture x64`。需要支持的 WebView2 Runtime；业务程序不需要 Node.js。页面在 `web/`，C++ 注册在 `app/main.cpp`。用户数据与浏览器 profile 位于 Local AppData，部署的 `resources/` 只保存应用资源。

窗口隐藏和恢复使用公共宿主接口。Bridge 使用有界事件通知；处理器不要同步执行长工作。关闭收到 `E_BUSY` 时保留对象并重试。示例 `--smoke-test` 验证页面、GUI 动作、C++ 响应及正常关闭，日志位于该应用 Local AppData 的独立 `smoke-<pid>` 目录。

模板独占自己的持久浏览器目录；同时运行的第二个实例不能共享该目录。多实例应用应为每个实例明确分配目录，或自行接入 SDK 的共享模式及对应关闭契约。关闭 Controller 后，模板保留窗口消息循环、Host、Bridge 和退出观察器，等待浏览器实际自然退出及回调收尾，再结束应用。整个关闭阶段限十秒；超时、异常退出或无法安全释放的借用均按失败处理。
