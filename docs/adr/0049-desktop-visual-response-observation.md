# ADR 0049：有限桌面视觉响应与帧观测

2026-10-09。状态：candidate；实现尚未编译或执行。范围为测量工具与共享基准页，不改变 SDK、默认渲染器或权限。

当前 Test.ps1 仅验证协议/原生身份及显式显示配置，实际呈现与输入关联尚缺失。0.5.0 的 60Hz 门槛仍须在 M0 冻结设备、负载、预热、样本/缺失规则和串行时间预算；历史预检不代表当前载荷。

在 InputTrace 的显式 full-desktop/probe-desktop 模式加入同一 Owned Job 内的观察线程。普通 full/probe 字节协议保持可读。桌面模式必须事先指定真实 expected FILETIME；持有 host 句柄、定位本轮 HWND，先把可能合成进桌面图像的指针移到标记区域外，再启动观察、取得可解码视觉标记基线，才发被测输入。每个被测操作在输入日志分配事件 ID；鼠标操作同时写 SendInput.dwExtraInfo，resize 是 Win32 SetWindowPos 而非 SendInput。页面实际收到 click/wheel/resize 后更新视觉 ACK 序号；串行输入只允许一个未确认操作，桌面解码必须恰好前进一个 ACK，否则失败，不从事件时间或 rAF 推造可见证据。dwExtraInfo 不被 DOM 暴露：关联来自独占、单未确认输入、连续 ACK 与实际动作顺序，不能声称任意外部输入已被认证。

共享页在固定 64×32 CSS 像素 canvas 输出 magic、页 nonce、场景、20-bit ACK、16-bit 视觉帧号、motion 标志和 CRC16，原始几何为 client(8,8)，初版只接受 DPR=1/96dpi、无旋转、窗口完全在单一 output。非动效只在实际事件后的 rAF 刷新标记；S05 在现有动效 rAF 中推进帧号。指标是含标记的共享基准页的视觉 ACK 和标记帧更新，不能直接变成未插桩产品或全部 CSS/compositor 帧率。相同页面用于两个宿主，插桩开销与负载必须先冻结。

观察线程从受控系统目录动态加载 DXGI/D3D11，模块借用保留到 COM 对象全释放；不增加默认 SDK 或 InputTrace 的静态图形 DLL 依赖。选择与窗口 monitor 相同的 output 与 adapter，用该 adapter 建 D3D11 device，DuplicateOutput 后 AcquireNextFrame(50ms)。只复制标记区域至小 staging texture，不保存桌面图像；读取 BGRA、二值像素及 CRC，记录原始 LastPresentTime/QPC、AccumulatedFrames、保护标记、nonce、ACK/帧号、owner HWND/几何、DPI、adapter LUID/output。pointer-only 的 LastPresentTime=0 记录为 pointer-only，不作帧样本；AccumulatedFrames>1 原样保存并使本轮严格统计失败，不补帧、不丢失败。access lost、保护像素、区域/owner/monitor改变、混淆标记、GPU读取超时和 ACK 超时均失败。同步 API 仍受外层 Owned deadline 管理。

LastPresentTime 是 OS 以 QPC 记录的桌面图像最后更新时间，不是显示器光子或单独应用的 Present 调用。仅在标记新状态首次出现在无合并桌面帧中计算输入 QPC 前/后至该时间的响应区间；两线程 ±1 tick 顺序不确定保留，不把小负差硬改 0。帧统计只取 S05 motion=true 的新标记帧号，原始数据/缺帧/全体样本保留。观测 timeout 与性能阈值不相同。finish-scene 是关闭控制，观察线程先正常结束，随后发 finish；该动作明确不属于视觉延迟样本，不等待已退出页面的 ACK。

复用 run-one/run-series 与其自然 closed Job 规则，显式 --desktop-observe 将同一 InputTrace 产物 desktop.ndjson 加入本轮散列及 reader。已有页面/输入/资源预检保持 benchmarkValid=false；新严格 reader 单独报告 desktopObservationAccepted、输入视觉响应和标记帧分布，也不设置 physicalPresentation/releaseReady。正式统计仍要求本轮固定 60Hz 配置、五轮与每轮完整时长，S03 真实 IME 尚未实现，不准入。

Win7 即使装 Platform Update，DuplicateOutput 也返回 E_NOTIMPL；Windows8+ 非DWM/断开会话/权限不足/组件缺失仍可能不可用，显式模式失败而普通输入功能可工作。Win7/PE 必须另做可归属的系统追踪或外部光学路径，本方案不关闭其目标矩阵。光学输入到屏幕指标继续 pending。

桌面 probe 有独立有限结果，不能充作 full 或五轮。前 5 秒只容许建立标记基线时的不可解码图像，逐条记录 baseline-reject；ready 后不再丢弃不可解码帧。页面 nonce 是本轮像素关联标识，不是安全随机身份或原生手势，ACK 不证明业务操作或全部内容已完成。collector 只持有本轮 process 句柄，不对相同进程内 HWND 代次作通用资格声明。

18 个已编写但未运行的算法测试覆盖 CRC/保留位/边界、身份/QPC、pointer-only/合并帧、跳 ACK、缺时长/错误几何/DPR/输出、带符号响应区间和分位数；不能代替真实 native/DOM 运行。Root 后续实际编译与真实窗口采集才证明原生路径。先实际短正控与错误身份/缺组件/遮挡失败，再同一插桩页五轮，不借旧载荷成功。

主源：[帧信息](https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_2/ns-dxgi1_2-dxgi_outdupl_frame_info)、[DuplicateOutput（含 Win7 E_NOTIMPL）](https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_2/nf-dxgi1_2-idxgioutput1-duplicateoutput)、[AcquireNextFrame 的有限等待](https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_2/nf-dxgi1_2-idxgioutputduplication-acquirenextframe)、[桌面 BGRA 与旋转](https://learn.microsoft.com/en-us/windows/win32/direct3ddxgi/desktop-dup-api)、[QPC 跨线程及 ±1 tick](https://learn.microsoft.com/en-us/windows/win32/sysinfo/acquiring-high-resolution-time-stamps)。
