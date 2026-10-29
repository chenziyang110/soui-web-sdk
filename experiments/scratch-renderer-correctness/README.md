# 实际 WebGL 凸包缓存差分实验

该独立消费者读取正常 Scratch demo 的同一 resources 目录，从已加载 bundle 的真实 `vm.renderer.constructor` 创建两个临时 WebGL renderer。一个关闭局部凸包缓存，一个开启；按同样的固定调用比较 local hull、bounds/bubble/fence、pick、point/touch、强制 CPU/GPU color query 和完整 RGBA 字节。无需重新构建 Scratch bundle，不修改 SDK 公共接口或现有 S/W profile 配方。

固定脚本另验共享局部 hull 与独立 world hull、同尺寸 bitmap/SVG 编辑、rotation center、真实 SVG decode 与更大 MIP、连续 SVG generation、效果回退、隐藏/零缩放/透明、pen line/stamp/clear、1025 个真实 skin 的 LRU entry 上限，以及销毁后的缓存释放。脚本必须等到 `_svgImageLoaded`；`createSVGSkin/updateSVGSkin` Promise 只证明归一化尺寸可用，不能替代真实 image decode。

Known-variant SVG oracle 记录 draw 前后 MIP/尺寸、局部 hull、中心、缩放和完整 backing RGBA。快照在两边明确调用 `setConvexHullDirty()` 后重算 bounds；这只是固定测试步骤，不能证明上游自动 MIP→bounds 失效传播已经正确。RGBA 使用真实 canvas backing 宽高，包含 devicePixelRatio，单维上限4096。

使用现有验证过的 WebView2 NuGet 目录配置：

```powershell
cmake -S experiments/scratch-renderer-correctness -B out/renderer-correctness-x64 -A x64 -DSOUI_WEBVIEW2_SDK_ROOT=<WebView2包目录>
cmake --build out/renderer-correctness-x64 --config Release
out/renderer-correctness-x64/bin/Release/ScratchRendererCorrectness.exe --resources <已部署Scratch资源目录> --output <不存在的证据目录>
```

消费者独占新 profile，只加载本地映射资源，保留固定 Bridge bootstrap/app.info 适配以启动正常页面。没有 CDP、任意脚本入口、F6 或鼠标键盘注入；窗口以 `SW_SHOWNOACTIVATE` 打开，不调用 MoveFocus/SetForegroundWindow/SetCursorPos。临时 canvas 在页面外保持真实 CSS 尺寸，适合 renderer 几何/查询正确性，不能作为显示帧率或输入延迟数据。

固定脚本上限80秒，原生 fixture watchdog 90秒，初始化90秒，原生 Controller/回调/实际 browser 退出观察10秒。失败保留非零；报告包括源脚本、全部资源 SHA、EXE、Runtime、browser 创建身份和自然关闭结果。脚本完成不等于 native close 完成。

覆盖范围不包含完整 VM clone/project reload/undo/redo、context loss/restore、131072点饱和或物理输入/上屏；相应缺口明确写入输出。共享 skin 的多个 drawable 不能冒充完整 VM clone 验收。两个路径同错也不等于语义正确，因此这是已有 Node oracle 之外的补充证据。

上游 `getBoundsForBubble` 对空 hull 会抛出异常；实验对这个调用比较真实的值/异常结果，保留该既有行为，不能把 off/on 同样抛出称为完整 bubble 语义通过。

## 实际执行状态：未通过

2026-10-29，显式 LLVM22/MSVC142、x64 Release 独立实验构建成功；不代表默认 MSVC143 或正式应用构建资格。最终 build r5 自然0/0，10.018秒，6276项所选源码/依赖前后不变；实际1个TU、24个头文件在预先绑定的输入闭包内，EXE确实内嵌固定脚本。EXE为465920B，SHA-256 `731f8708e7ee87f46f35760c47e99ea8e06334df48513c4115b1a54f232411c5`。

真实 WebView2 Runtime `156.0.4314.50` 的第一次运行自然2/2，195断言、21个已执行case后，在连续SVG更新的已知variant1 oracle失败。为调查该失败，第二次只新增真实raster/MIP/几何差值观测，并对齐两次oracle的draw/geometry采样次序；强known-variant断言保留。第二次仍自然2/2，11.010秒，237断言、21个case后在同一断言失败；9个进程全部自然结束，无job强杀，Controller/实际browser退出已观察，外部请求阻断数0。

第二次记录表明 oracle与final、draw前后均为DPR1、backing480×360、largestMIP1、silhouette64×48、skin64×48、center[12,18,0]、scale[100,100]、direction90、SVG已解码。该证据排除了本次“draw提升MIP导致oracle采样不一致”的假设。失败发生在关闭缓存的renderer与其先前已知variant的比较；off/on的21个相互差分case此前一致，不能据此认定是新缓存的单边缺陷。已知variant hull为8点、最终为9点，斜边采样点不同；bubble bottom亦为5.5与约8.50000095。真实原因仍待调查，不能移除oracle或把两个路径同时变化当通过。

原始记录由验证负责人保存于独立证据集合 `renderer-correctness-r1`、`renderer-correctness-r2`（含result、session、完整脚本、资源清单、before/after raster、firstDifference及owned process终态）；构建集合为 `scratch-renderer-correctness-x64-r5`（含actual-read-closure）。两次失败都保留。后面的pen/stamp、LRU容量与销毁case尚未执行到，不能把脚本中存在这些case写成已验证。
