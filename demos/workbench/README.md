# Workbench Demo

展示媒体、文件扫描、组件与桌面能力。integration/ 中的方法是 Demo 业务适配器，不属于通用 SDK API 保证。

从交付根目录执行 tools/Build.ps1，运行 out/x64/bin/Release/SouiWorkbench/SouiWorkbench.exe。默认使用稳定的软件呈现路径；`--renderer gpu`、`--renderer readback` 和 `--renderer auto` 仅用于 GPU 内核实验，当前不属于生产验收范围。自动验证使用 tools/Test.ps1。新业务项目使用 templates/desktop-app/。
