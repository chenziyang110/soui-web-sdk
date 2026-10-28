# 实际公共 SDK Host 量测入口（源码交付，未执行）

`SouiInputUiHost` 使用真实 `soui_miniblink_host` / `dynamic_miniblink_adapter`、原生输入窗口、Bridge 和最终 Runtime 关闭。没有 raw WKE 页面/输入绕过。SDK 和内核 trace 在任何 Host/worker 注册前冷配置；详细模式才启动独立有界 consumer，不在 engine/UI 热路径落盘。

```powershell
<SouiInputUiHost.exe> --miniblink=<匹配DLL绝对路径> --page-root=<冻结protocol页面目录> --result-dir=<本轮已创建结果目录> --run=<32位小写hex> --kind=pointer_move --trace=full
```

输入器必须由 OwnedProcessRunner 持有，并使用它的真实 PID/创建 FILETIME；等待 `host-ready.json` 并与原生 start 复核，再启动 `BurstInput`。它仅接受 `SouiInputUiHostV1` 窗口，在 final ACK、桌面 collector 和输入原件持久化后设置绑定 run/PID/创建身份的本轮结束 event；不会向可能复用的 HWND 发送跨进程关闭消息。宿主收到后走真实 Host.destroy 和二十秒 Runtime 总期限，再 stop trace、等待 producer quiescent、排空和写完整结束；失败非零，不修改验收资格。

冻结的 burst HTML 原样复制为 page-bundle/index.html；run/kind 放在新建的有界 burst-config.js。公共 SDK 仍只信任 index/react 本地入口，不放宽 query/navigation 检查。指针用唯一坐标编码输入序列；滚轮固定 -120 单位，在 DOM 按实际 wheelDelta 累积，OS 聚合不改写为丢事件。全模式缺少对应能力即拒绝；`--trace=e2e` 只允许明确的旧 API 缺失，内部阶段标 unavailable。

consumer 每次最多1024项/源、4ms条件等待，两个64KiB缓冲文件均 CREATE_NEW。每 exporter 总计100000事件/小于64MiB；错误/超限/未静止不写完整 end。默认 SDK 不自动创建这个工具线程。窗口保留既有公共 benchmark 的16ms owner pump，不能将该工具的调度误称内核 deadline 优化收益。

活动期限固定150秒；Host.destroy、最终Runtime关闭和trace收口共享从关闭请求开始的20秒剩余预算，不逐阶段重新计时。完成字段只证明实际capability/getter与导出闭合；各阶段关联仍由真实point/content证据判定，未知继续unknown。

旧基线需使用其**匹配的 SDK 库和公共 Host 头**。如旧安装缺新诊断模块，可在开始物理采样前运行 prepare-legacy-support.ps1，从冻结候选仅复制六个中立诊断文件到全新目录，再向本目录 CMake 传 SOUI_INPUT_UI_LEGACY_SUPPORT。该 include 树没有 Host/输入/渲染类头，不会把旧对象与新 C++ 布局混用。旧内核没有新表时只运行 e2e；overlay/source manifest、旧核心库、DLL、宿主 EXE 与同一冻结量测源码都必须进本轮来源/载荷清单。不能把 overlay 描述为旧核心已经获得新生产优化。

本目录 CMake 可独立 find_package，也可由集成根注册。编译、原生无目标/错误参数、真实窗口/关闭、overflow、trace 开关预检和实机性能均交专人执行；本次没有运行这些命令。W02 的实际输入取消 oracle 与命名场景见 tests/native/input_frame_trace/INPUT-CANCEL-SCENARIOS.md。
