# V8 Win7 V02 交互输入准备

V7 冻结输入和失败原件保持不变。V7 在 1024×768 默认任务栏覆盖 Workbench 右下角时，12 次原生外层滚轮后于真实缩放探针失败；详见仓库 `verification/production-validation/win7-sp1-x86-v02-interaction-v7-resize-blocked-2026-09-28/README.md` 与 Issue #42。

V8 独立源目录 `layout-interaction-v8-source` 以 V7 输入为起点，增加 `move-drag -80,-80`：在自然前台的页面标题栏区域以真实鼠标左键拖动窗口，核对窗口左上角至少向左、向上各移动 60 像素，然后才执行 12 次原生外层滚轮和既有真实 `resize-drag 80,60`。这不使用脚本赋值替代滚动、缩放或 IME。120 秒应用单调时钟期限、客体一次命令和五秒宿主外部负载门禁保持不变。若标题栏拖动失败，保存原失败，不跳过此动作或放宽断言。

当前源文件 SHA-256：`V02InputProbe.cpp` 6e7d8b00f0819074dbe800c2dffcbee81ac425e1c27414c5539e75ff11e98d65；`run-layout-interaction.ps1` a19dba351c307d4a264cbfb98e1c0937606e9667c1debdb5557021ed06a22cb5；`Validate-Source.cjs` af07efc82df3e79e118ad6965c84986c90d567246ccff7f7b1c5751f7432c646。`Build-V8Probe.cjs` 锁定这些散列及静态 MSVC 运行库，期望 `probe-v8-build-attempt1`。V8 Prepare/Freeze/Verify/Launch/Run/Timeline/Review 控制脚本已另建；预期 40 个原生输入动作、118 串口帧。标题栏鼠标按下后留 120 毫秒供异步原生拖动命令接收，再移动指针。7 个 JS 语法检查、4 个 PowerShell 解析和源动作契约检查已在宿主返回 0，但尚未另存完整原始命令日志.

截至准备记录：未运行编译、未准备或冻结 V8 输入、未启动 V8 客体，`win7CurrentV02Verified=false`、`releaseReady=false`。执行前必须重新检查全部 QEMU/Soui/构建进程；其他 VM 运行时不编译或启动客体。编译完成后使用全新 `layout-interaction-v8-attempt1` 与 `guest-layout-interaction-v8-attempt1`，复核源码/EXE/输入散列，确认真实 Win7 桌面后只发送一次命令，并保留失败原样本。

V7 第 27 帧的真实截图显示：12 次外层滚轮后，列表仍占据画面上部，中文输入区从画面下半部开始；公共任务区在页面更靠下的位置。因此 V8 继承的固定 `click 635,600` 只是待观察的原生点击输入，不得在没有任务状态、进度事件与查询原件的情况下声称启动了 30 秒任务。V8 严格复核器即使机械检查通过，也明确保持 `taskStartedVerified=false`、`imeCompositionVerified=false` 和 `win7CurrentV02Verified=false`；后续须依据 V8 实际截图另建冻结输入定位任务与中文输入。
