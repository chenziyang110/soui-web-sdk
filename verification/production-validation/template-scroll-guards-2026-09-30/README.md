# 模板真实滚动前的身份、遮挡与载荷验证

本记录延续[独立模板双架构构建](../template-scrollbar-2026-09-30/README.md)，绑定同一冻结 preview.15 SDK、生成源码及 x64 buildId `5a5224a2-883d-41da-ac2a-45c7b010413e`。模板和 SDK Core 没有新增修改。本次仅归档验收探针、自测、实际尝试及拒绝记录，**模板真实滚动仍未通过**。

原生标题栏探针要求 PID、进程创建时间、HWND 相符，窗口可见且点位无遮挡，`WM_NCHITTEST=HTCAPTION` 才发送真实鼠标点击；点击后重新核对前台。源码 SHA-256 `788785ffd296190c5a7d67741c4a8c6f15cd788001975ac66de3dc19db2e6437`，未调用 `SetForegroundWindow`。x64 `activation-build-x64-attempt1` 和 x86 `activation-build-x86-attempt2` 编译捕获原始退出 0，冻结 EXE 分别为 `2f693ff73673c492459ea3fa6fa1656f2329c64c4c095566a1afc958ea0d69ca`、`43d2819c40e067719b5c749315c59d52227fea541597558ec3718752254f6834`。受外部负载打断的 x86 attempt1 保留，并被冻结负控拒绝；只读 freeze.json SHA-256 `d79ac48ebd0ee8a5b9de8ce6fb4648fca84c93856087d2997a93b805a16914b6`。EXE 原件留在仓库外，归档只包含源码、冻结身份与编译日志。

只读遮挡检测的第一次自测被 QEMU 门禁拒绝，没有运行 API；第二次实际观察前台 HWND 所属进程并拒绝无效 HWND，正控原始退出 0、负控按预期退出 1。自测前后门禁无已知竞争进程，检测脚本和驱动前后散列一致。检测报告自身退出 0 只证明观察成功；目标被遮挡时仍拒绝发送输入。

同次运行目录的载荷验证器检查全部声明文件、五份法律文件、额外文件及导航日志。初版八项微型夹具正负控通过，但 v4 的 `template-scroll-x64-attempt3` 在启动前误拒了日志中重复出现的同一合法导航地址，原始退出 1、目标为空、无输入。新 v2 验证器接受每行均为规范合法地址的重复日志；新增混合外部地址、拒绝记录和空日志负控，`live-payload-verifier-selftest-attempt2` **13/13、原始退出 0**。改变 DLL 或页面、缺通知、额外 DLL、路径逃逸依然拒绝。微型夹具证明校验器边界，不能证明产品输入或内核安全。

| 实际 x64 尝试 | 原始结果 | 证明范围 |
| --- | --- | --- |
| attempt2 / v2 | 标题点被其他窗口遮挡，点击探针退出 1 | 点击、滚轮、截图均未发生 |
| attempt3 / v4 | 重复合法导航被校验器误拒，驱动退出 1 | 未启动应用；失败原件保留 |
| attempt4 / v5 | 启动前 71 文件、五份 legal 核对通过；GameViewer 遮住标题点；驱动退出 1 | PID/创建身份/HWND 已取得，只读观察成功；点击、滚轮、截图均为 0 |

attempt4 的标题点为 `[680,142]`，应用矩形 `[130,130,1230,850]`；只读观察记录 GameViewer 所属遮挡根窗口，另一次观察矩形 `[-8,-8,1928,1040]`。驱动仅停止持有的自身应用，`nativeExitCode=null`，不能算原生完整关闭。没有产品真实滚动失败或通过结论。v5 SHA-256 `e83f62df874d8cc9c17867183e57d02bff1273b6fd24a9e447862237456a3b79`，绑定 v2 验证器 SHA-256 `25d69e803701eb3a28fece8785ada686477c7196bc03e47df164084f892115be`。

`raw/` 保存 80 份精选原件：`template/` 对应仓库外 `template-scrollbar-preparation-20260929`，`capture/` 对应 `benchmark-git-load-gate-20260930` 中专属于这些模板命令的捕获记录；未混入基准源码或自动化配置。微型夹具的伪 EXE/DLL 不入库，其生成源码及结果入库。`Verify-Archive.cjs` 核对固定清单及可选 Git index；本机可同时传入 `--template-originals`、`--capture-originals` 两个绝对原件根。校验只证明字节一致。

`audit/template-guards-audit-attempt3/` 保存 SDK Web 依赖审计的原始 stdout、stderr 和退出记录。审计实际退出 0、报告零漏洞，未安装或更新依赖；执行前后查询未见已知竞争进程。此前两次外层门禁因外部 Electron/Git 负载拒绝，没有启动审计子进程或生成捕获目录。该审计不证明原生依赖授权或产品安全。

固定清单创建和两个外部原件根核对原始退出均为 0。暂存区核对第一次在捕获器启动前被外部 Electron 测试拒绝；新 `template-guards-index-attempt2` 核对全部 80 份原件、工作树与 Git index，原始退出 0。三份审计记录也与外部原件及 Git index 字节一致。原件的换行保持不变，未把校验失败或未发生的输入改为通过。

后续使用 v5 的新 x64 attempt5、x86 attempt2，在宿主空闲且桌面无遮挡时串行执行，保留 16 次真实滚轮、五截图、前后完整载荷及原生退出；还须人工核对同次内容变化。Win7 模板必须另建冻结输入与客体。此归档不关闭真实滚动、中文 IME、DPI/多屏、长稳或 Electron 性能；`sourceDeliveryVerified=false`、`benchmarkValid=false`、`releaseReady=false`。
