# SouiStarter

最小模板：标准窗口、React 页面、C++ app.greet 方法和自动启动验证。

通过交付根目录 tools/New-Project.ps1 生成独立工程后，在新项目根目录执行：

```powershell
powershell.exe -File tools/Build.ps1 -Architecture x64
.\out\x64\bin\Release\SouiStarter\SouiStarter.exe
```

业务修改放 app/、web/、contracts/、resources/；SDK 由 sdk-lock.json 管理。新增方法同时维护 contracts/app.contract.json、TypeScript 类型和 C++ 注册实现。在完整交付仓库直接使用模板时，从交付根目录构建。
