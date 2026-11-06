# 手动输入的被动画面观测

此协议独立于 BurstInput。操作者自己移动鼠标或滚轮；页面没有注入输入、自动动画、空闲 rAF、周期 timer 或输入期间的 Bridge 调用。原 `soui.manual-input-page/v1` 快照、Finish 提交与正常关闭后落盘行为保留。原 pointer/wheel 计数、坐标、背景公式、测量内容和反馈黑块均保留，操作提示按下述观察顺序说明。

页面的“已接纳”仅说明单次页面快照已被接纳，不表示外部观察器完成。按本轮说明完成采样；存在外部观察器时，须先等控制台报告观察结束，再保存、正常关窗。画布、初始提示和接纳提示采用同一顺序；这只是提示，不增加等待门、计时器、Bridge 调用或对用户关闭窗口的限制。

页面只在已有 render 的末尾写一个 64×32 的机器标记，位置为客户区 `[8,336]`，不覆盖旧文字或 Finish。预分配一份 ImageData，每次更新后调用一次 `putImageData`；它有 CPU/上传开销，尚未量化，不能声称零开销。光标/遮挡可能破坏标记或内容探针，采集器不会移动光标来避开它。

## 标记与实际内容

标记是 16 字节，按低位 bit 优先、16 列×8 行、每 bit 4×4 像素的纯黑/白块编码：

| 字节 | 含义 |
|---|---|
| 0–1 | `A7 4D`，与旧 Burst `A7 53` 不同 |
| 2–5 | little-endian nonce：完整 run 的末 8 hex，零映射为 1；完整 run 另由启动回执绑定 |
| 6–8 | little-endian 本地反馈 revision：原 pointer + wheel 计数，只允许 20 bit |
| 9–10 | little-endian 原 renders 计数，只允许 16 bit |
| 11 | bit0=overflow，bit1=geometryInvalid，其余保留为零 |
| 12–13 | little-endian CRC16-CCITT，初值 FFFF、poly1021，遍历全部 16 字节但跳过 12/13 |
| 14–15 | little-endian 本地 page epoch，正常范围 1–65535 |

revision 超过 1048575 或 renders 超过 65535 时，标记字段饱和且原快照的 `overflow` 变为 true；不回绕，不把新值伪装成新有效帧。原正常输入计数/渲染继续。`overflow` 因而也表示观测标记容量已用尽，不能只解释成原 31-bit 计数器耗尽。生命周期触发的 render 可增加 frame 而不增加 revision，不能算输入反馈内容变化。

`frame` 是页面 rAF 回调中的 render 序号，不是 compositor 或桌面呈现 ID。pointer/wheel 只累加事件并请求 render，`pending` 位合并同一个待执行 rAF 前的请求；多次本地输入可以只产生一次 render。反过来，相邻桌面采样之间也可能完成多次页面 render，较早的 canvas 状态可能在被观察前被替代。`frameSequenceGap` 及其总数保留未观察到的中间页面 render 序号数，不能据此单独断言 Desktop Duplication 丢帧。中间状态实际被合并、未提交还是未观察到，均不能由这些端点证明。

页面只在装载时更新 `window.name` 的自有前缀 `soui-manual-observation:<run>:<epoch>` 并回读校验。空 name 的新页为 1；同 context、同 run 再装载递增；合法自有前缀但不同 run 从 1 开始。外来非空 name、自有畸形值、读写不可用、写回不一致或 epoch 耗尽均使用 epoch=0 且 `overflow=true`，不覆盖外来名字、不悄悄复用 1，正常反馈继续。没有 session/token 存入 name。CRC 覆盖 epoch，observer 在 baseline 固定它，变化即失效。这只是本地装载代际，不能替代 native document ID、安全授权或跨新窗口的全局代际；完整 run/进程/HWND 仍需另验。

内容探针为原画布 `[10,330]` 的背景像素；没有新绘制的独立“内容计数像素”。其 RGB 应为 `(revision*73 % 256, revision*151 % 256, revision*29 % 256)`。采集的联合区域是 `[8,328,64,40]`。probe 颜色每 256 个 revision 重复，所以仅 marker 变化、probe 重复、遮挡或 probe 不符时，都不能计作有效内容帧；跳过的页面 render/反馈计数原样保留，不推算未见帧。revision 是本地 JavaScript 观察，不是 native event ID 或注入 ACK。

CRC16 可由独立读取器从 markerHex 重算。联合 ROI 的 `captureBgraCrc32` 是原生采集器对去掉 pitch padding 的 row-major BGRA 字节计算的 CRC32；报告不包含完整 ROI 原始像素，因此离线读取器只核其格式及与同 present 记录的一致性，不声称重新计算了全部原始像素 CRC32。

## 资格边界

独立 schema `soui.manual-desktop-observation/v1` 绑定完整 run、Host/observer 两个进程的 PID + creation FILETIME、HWND、QPF、实际 phase 和原件资源身份。`LastPresentTime` 是 Desktop Duplication API 的呈现标记，不是扫描输出或光子时间。

begin 可选字段 `waitTimerBackend` 仅接受 `high-resolution-waitable` 或 `standard-waitable`，读取结果原样保留；旧 v1 缺失时记为 `unknown`，不猜测旧工具实现。后端名称不改变资格，也不证明唤醒精度或采样无遗漏。`wait-timer-failed` 和 `wait-timer-close-failed` 是失败终态，不能标为 completed/qualified；外部进程监督仍须验证非零错误与真实终态，不由文件内容单独授予通过。

有效内容变化仅是被动采集区域内、标记与原背景一致的已观察变化。它不是完整屏幕 unique FPS、物理刷新率、硬件输入到光子延迟、确定性回放或无遗漏证明。头尾覆盖、采集积压、帧/反馈序号跳跃、指针更新、超时、未知样本都单独保留。缺少 phase/身份/资源/时钟证据不能填成零或通过。

本地 render 跳号本身不否定一个完整、probe 确实变化的桌面采样；该采样最多计一次 observed content change，不按 revision 差或 render 差推算多帧。DXGI `AccumulatedFrames>1`、未知采样、protected/cursor/probe/CRC/nonce/epoch 错误、序号倒退或同 frame 对应不同 revision 等原拒绝条件保留。读取结果固定 `allPageRendersObserved=false`、`intermediatePageRenderDisposition='unknown'`，即使 `traceQualified=true` 也不证明全部页面 render 都呈现或都被观察。旧工具把 render 跳号写成 `reason='frame-gap'` 的记录不能用新读取器重新判为通过；应保留旧工具身份和原始结论。

资源窗口读取器使用同一 QPF 和该原生 phase 的边界；不能用 JS `performance.now()` 减 QPC，也不能把采样覆盖外的整个应用生命周期 CPU/内存称为这段输入窗口成本。

## 读取接口

`InputTrace --observe-manual` 的末尾可选参数显式选择 `dxgi` 或 `gdi-bitblt`；省略时使用 DXGI。手动 A/B 应在各自 `expected` 中设置相同的 `captureBackend`。GDI 路径同步执行 BitBlt 与 GdiFlush，以 BitBlt 完成后的 QPC 作为软件可见 T7 时间；不自动回退，不能将其称为 DXGI 呈现时间、物理 FPS 或扫描输出。原 100 ms 单次采集预算和外层截止时间仍生效；同步调用不能抢占，超出预算/截止的样本会被读取器拒绝。

`manual-frame-observation.cjs` 导出 `analyzeManualObservation(rows, expected)` 和 `readManualObservation(absoluteFile, expected)`。前者只检查原始记录及声明的原件身份，返回 `traceQualified`；后者还按 held 文件读取 NDJSON 并核当前原件散列，返回 `artifactIdentity.currentFilesRevalidated`。二者始终保留 `observationQualified=false` 和 `performanceGate=false`：成功 end 行可能先于失败的最终 Flush，文件内容不能代替进程终态。外部监督器须另外核 held observer PID/creation、root/supervisor exit0、natural Job0/no-force，再与 trace/artifact 资格合成整轮判断。

`expected` 绑定 `run_id`、`targetPid/targetCreationFileTime`、`observerPid/observerCreationFileTime`、`hwnd`、`qpcFrequency`、`intervalMs/durationMs/startupMs`。所有 QPC、QPF、FILETIME、HWND 均为规范十进制 uint64 字符串；PID 是正 uint32。可额外给 `phaseBeginQpc/phaseEndQpc` 复验外部窗口边界。实际 `window` 从 validated phase 行及 end 原件导出，交给资源窗口读取器；nominal deadline 与实际尾部超出分开保存。

`expected.artifacts` 是 `frozen/before/after` 三份同形数组，每份恰含 `host/kernel/pageHtml/pageScript/collector/reader` 六个不同角色，每项 `{role,path,bytes,sha256}`。三份必须逐项相同；文件读取入口再核当前字节。它们由启动者的冻结清单与前后原件核验提供，不可用本次读取时临时捏造的同一散列代替历史回执。这仍只说明端点身份，不认证实际已加载模块或全窗连续身份。

只有 qualified trace 输出 `observedContentTiming`：相邻独立内容变化的 LastPresentQpc 差、精确 ticks 的 p50/p95/p99/最大值、超过 50ms 的观察间隔，以及整个实际 phase 内变化数/秒的精确分数。它包含操作者停顿；不是 rAF、唯一屏幕帧率、物理呈现或输入延迟。存在 unknown、DXGI 采集积压、记录缺失或不完整 phase 时，该计时摘要为 unavailable/null；本地 render 跳号单独保留，不自动使摘要不可用。原始观察和缺口不会被补成未见帧，未知不能填 0，人工负载仍是 `workloadComparable=unknown`。
