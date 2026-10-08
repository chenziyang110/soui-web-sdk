# UpgradeLedger

最小模板：标准窗口、React 页面、C++ `app.greet` 方法、可取消的公共 SDK 长任务 `app.count` 和自动启动验证。默认使用已通过关闭回归的软件呈现路径；GPU compositor 仍是内核专项实验，不进入新项目默认配置。

通过交付根目录 tools/New-Project.ps1 生成独立工程后，在新项目根目录执行：

```powershell
powershell.exe -File tools/Build.ps1 -Architecture x64
.\out\x64\bin\Release\UpgradeLedger\UpgradeLedger.exe
```

业务修改放 app/、web/、contracts/、resources/；SDK 由 sdk-lock.json 管理。新增方法同时维护 contracts/app.contract.json、TypeScript 类型和 C++ 注册实现。在完整交付仓库直接使用模板时，从交付根目录构建。

长任务示例在 `app/main.cpp` 注册处理器，在 `web/src/main.tsx` 通过 `task.start/query/cancel` 及 `task.progress` 事件使用。新项目可以替换 `app.count` 的业务实现，保留 SDK 的调度、会话权限和取消协议；处理器需要自行检查输入并定期响应取消。完整限制见 [SDK API](../../docs/api.md#公共长任务)。
