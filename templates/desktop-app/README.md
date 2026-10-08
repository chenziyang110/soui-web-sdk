# SouiStarter

最小模板：标准窗口、React 页面、C++ `app.greet` 方法、可取消的公共 SDK 长任务 `app.count` 和自动启动验证。默认使用已通过关闭回归的软件呈现路径；GPU compositor 仍是内核专项实验，不进入新项目默认配置。

通过交付根目录 tools/New-Project.ps1 生成独立工程后，在新项目根目录执行：

```powershell
powershell.exe -File tools/Build.ps1 -Architecture x64
.\out\x64\bin\Release\SouiStarter\SouiStarter.exe
```

业务修改放 app/、web/、contracts/、resources/；SDK 由 sdk-lock.json 管理。问候动作唯一来源是 `contracts/actions.json`；`app.contract.json` 仅说明公共服务和计数任务。在完整交付仓库直接使用模板时，从交付根目录构建。

修改动作源后，在生成项目根目录先生成，再构建：

```powershell
node tools/sdk/actiongen.cjs --source contracts/actions.json --root .
node tools/sdk/actiongen.cjs --source contracts/actions.json --root . --check
```

原仓库使用 `node tools/sdk/actiongen.cjs --source templates/desktop-app/contracts/actions.json --root templates/desktop-app`。CMake 只检查，不自动写入；输出有散列保护，不要直接编辑。C++ 使用 `app/generated/actions.hpp` 的 `register_Greet`，前端使用 `web/src/generated/actions.ts` 的 `callAction`。可选 `Soui::ActionJson` 提供 JSON 头文件。非法输入不发请求，成功结果仍检查；原生验证是最终边界。Agent 描述不授予权限、不自动重试。契约文档见 `docs/generated/actions.md`。

页面滚动条的颜色、宽度和滑块形状定义在 `web/src/style.css` 的 `::-webkit-scrollbar` 规则中，兼容当前 Miniblink 页面目标。新项目可按品牌调整这些规则；改动外观后仍需在目标 Windows 版本上实际检查滚轮、拖动和键盘滚动。

长任务示例在 `app/main.cpp` 注册处理器，在 `web/src/main.tsx` 通过 `task.start/query/cancel` 及 `task.progress` 事件使用。新项目可以替换 `app.count` 的业务实现，保留 SDK 的调度、会话权限和取消协议；处理器需要自行检查输入并定期响应取消。完整限制见 [SDK API](../../docs/api.md#公共长任务)。
