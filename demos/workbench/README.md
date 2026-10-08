# Workbench Demo

展示媒体、文件扫描、组件与桌面能力。integration/ 中的方法是 Demo 业务适配器，不属于通用 SDK API 保证。

从交付根目录执行 tools/Build.ps1，运行 out/x64/bin/Release/SouiWorkbench/SouiWorkbench.exe。默认使用稳定的软件呈现路径；`--renderer gpu`、`--renderer readback` 和 `--renderer auto` 仅用于 GPU 内核实验，当前不属于生产验收范围。自动验证使用 tools/Test.ps1。新业务项目使用 templates/desktop-app/。

## 显式三窗口驻留（preview.10）

交付根运行 `out/x64/bin/Release/SouiWorkbench/SouiWorkbench.exe --resident --react --page=data`。应用控制器创建三个独立 Host/adapter/会话，共用进程 Runtime；关闭任一业务窗口或全部业务窗口后仍保留托盘。点击图标、托盘菜单或 Ctrl+Alt+W 可恢复窗口，托盘菜单提供明确退出。快捷键被占用或服务初始化失败会记录错误并退出，不静默换用其他组合。默认不带 `--resident` 的单窗关闭行为保留。

有限生命周期短测使用 `--resident --resident-smoke-test --react --page=data`：等待三个页面 ready，关闭中间窗，重载其余两窗并重建中间窗，关闭全部业务窗后保留控制器至少 500 ms，再重建三窗并统一退出。180 秒内未完成返回非零；监督器应另设进程超时。它不自动操作菜单或发送快捷键，也不证明页面中真实任务互不干扰、用户输入、IME、Explorer 重启、长期资源或 Win7 兼容通过。

驻留模式的 NDJSON 事件默认写到系统临时目录、按进程和运行标识生成新文件；验收可传 `--resident-log` 指向仓库外新的绝对文件路径，保存槽位、代际、ready、关闭、各服务和最终 Runtime 退出结果。禁止覆盖既有证据。不要与单窗 smoke/self-test、benchmark 或时序实验参数组合。桌面验收必须与其他 SDK 窗口和 QEMU 串行，x86 使用对应 x86 输出。公共服务 API 见 [SDK 文档](../../docs/api.md)。
