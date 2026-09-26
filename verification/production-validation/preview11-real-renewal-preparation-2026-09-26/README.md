# preview.11 真实时钟续期验收准备

状态：**仅准备与校验器合成自检，尚未编译夹具、运行 GUI 或完成真实长测**。此工作不改变 preview.11 SDK 的冻结内容，也不借用旧八小时循环作为同文档续期通过。

## 验证缺口

现有 `tests/acceptance-app` 没有启用同文档续期，混合驱动还会反复重载/重建窗口。Workbench V02 的公共任务约一秒结束，也没有长期保留任务与订阅的证据。因此需要独立安装消费者，使用公共 C++/Web SDK，保留同一个页面、两个原任务、一个原订阅，等待真实时间。

外部准备目录为 `C:\SOUI-SDK-production-validation\preview11-real-renewal-preparation-20260926`。该目录保存 937 个准备输入、273,676,732 字节，SHA-256 `29548090b14d3e8da3ef5524bdd413911db039a22b5d4d1732ddc39f651b6574`；其中 SDK 编译输入仍是 711 文件、`a0c1d7900d39555508288d5459e65d3852e543cf7ec3879126bd6af59d2c2550`，含对应两架构安装文件、构建记录、已构建 Web SDK 和 DLL/legal 输入。`Prepare.cjs prepare/verify` 只做复制和身份核对，实际退出 0。没有生成可运行消费者或最终 `compiled-inputs-frozen` 清单。

## 夹具与判定

- `InstalledRenewalProbe` 仅通过安装后的 `Soui::Sdk` 使用公共 Host/json_bridge，显式开启续期，一次挂载，不重载、不缩短期限、不使用模拟时钟。
- 页面仅建立一次标准连接，启动一个持续进度任务和一个结果为 42 的任务，保持原始订阅。透明端口观察记录白名单字段，不保存 bearer、不修改响应或业务重试；事件与查询必须同时持续成功。
- 支持 30 秒夹具 smoke、35 分钟真实续期、8 小时此单一场景。以原生 `ready` 到 `complete` 计算时长；长期模式必须有原生首次 `describe + 30 分钟` 之后产生、被页面后续心跳明确引用的相同任务事件。不能用到期前的最后一条事件重复填报。
- 校验器核对请求/真实 ACK、任务与订阅身份、实际调用次数、续期代际和有界同操作重取；结束必须取消原长任务、确认 cancelled、退订原订阅并再次读到 42，随后完整清理并取得数值退出码 0。
- 监督器核对冻结源码、构建、完整载荷及前后身份，使用桌面互斥锁，检查 QEMU/其他应用占用。出现外部负载或超时只终止本次探针，并保留失败。仅运行根部自动生成的 `source-navigation.log` 可作为诊断例外，必须另存原始字节并核对散列。

准确字段与边界见 [PROTOCOL.md](raw/PROTOCOL.md)。源程序、准备/构建/监督/分析脚本按原字节归档；[清单](raw-manifest.json)绑定这些副本。原脚本的开发机路径记录本轮夹具的历史输入，不替代 SDK 标准可移植构建入口。仓库未重复提交 273 MB 输入及全部合成样本。

[校验器自检](raw/self-test-result.json)为 88 项合成正反例，均通过；`actualGuiExecuted=false`、`actualClockRenewalVerified=false`。它只检验分析器拒绝缺失、错误身份、假时长、到期后无新事件、取消/退订/退出错误、载荷漂移及诊断例外滥用等情况。合成 35 分钟/8 小时记录绝不是实际运行。构建驱动、运行监督器当前只完成静态/语法复核，其失败路径未经过真实运行。

静态复核期间修正了日志刷新异常可能跳过清理的问题，并把监督器、分析器纳入构建后的冻结驱动清单。修复前未执行版本保留于 `raw/review-history/`；最终脚本已独立核对散列及失败收口。这仍不等于真实异常注入或桌面执行通过。

## 后续顺序

宿主和其他客体空闲后，在完整外部目录串行执行 `Build.ps1 -Architecture x86 -AttemptName x86-attempt1` 和 x64 对应构建；每步保存原始日志、退出码与超时状态，失败目录不覆盖。两次独立构建及 `--self-check` 成功后，`Prepare.cjs freeze x86-attempt1 x64-attempt1` 才生成最终冻结记录。

先运行 `Run.ps1 -Architecture x86 -DurationMs 30000 -AttemptName x86-smoke-attempt1` 验证夹具，按监督器打印的冻结校验器路径分析其 `native.ndjson`、`supervisor.json`，通过后才安排 35 分钟及后续长测。每次结果位于外部同级新 attempt；禁止向输入/资源目录写证据或把原失败清空再跑。复用运行目录会追加诊断日志，应在后续运行前完成本次分析与归档；后续 live 复核旧日志可能因追加而正确拒绝，不得修改旧证据让它重新通过。

这里不覆盖 Workbench V02 真实滚动、中文 IME、缩放和任务同时响应，也不覆盖八小时混合业务、Win7、资源门槛或 Electron 性能。`realRenewalVerified=false`、`sourceDeliveryVerified=false`、`benchmarkValid=false`、`releaseReady=false`。
