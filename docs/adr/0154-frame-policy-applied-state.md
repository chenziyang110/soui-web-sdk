# ADR-0154：MiniBlink 主帧策略协商与实际应用状态

状态：query/apply 与原协商已完成两架构受影响内核reader闭包/链接及SDK针对性契约验证；后续 owner 显示参考修复随 candidate-owner-admission-sdk-r2 完成两架构实际编译及新增片段回归。匹配新SDK的应用联合载荷及物理性能待验。

## 背景

SDK 的帧率输入允许 30–1000，当前 MiniBlink 的 `WebPageImpl::setMainFrameRate` 只接受 0 或 30–240。既有 host 调用无返回值的 debug setter 后直接缓存 requested；大于 240 的请求可能被内核忽略，但诊断仍显示配置完成。这个源码契约缺口不证明任何既有 FPS 读数的原因。

现有分数 QPC 相位、主帧/合成 deadline、legacy compositor、CPU interval override 与主帧 0 禁用语义继续保留。本次不改画质、事件语义、GPU 默认或主帧最大支持范围。

## 决定

在现有 `wkeGetInputUiApi(1)` 的函数表尾部增加可选 capability `WKE_INPUT_UI_CAP_FRAME_POLICY_STATE=0x800`，以及 `queryFramePolicy`、`applyFramePolicy` 两个调用。原字段位置不变，旧调用方继续使用原前缀；新 host 检查 ABI 版本、结构长度、capability 和两个函数指针后调用。

`wkeInputUiFramePolicy` 为 32 字节、仅含 32 位整数的结构：`structSize`、`abiVersion`、`policyVersion`、`minRate`、`maxRate`、`appliedRate`、两个保留字段。结构 ABI=1，策略版本=1，当前 min=30/max=240；`appliedRate=0` 表示主帧控制器禁用，不表示 0 FPS。保留字段输入必须为 0。调用方不能用 requested 推断 applied。

两项操作均在真实引擎 owner 线程执行，验证 live view 与 paint lifetime。query 返回当前已存储策略；apply 只接受 0 或当前范围内的整数，返回实际快照。范围外输入返回 INVALID，不静默 clamp；host 负责先选择有效值。OK 确认配置状态，不是帧完成或物理呈现凭证。单个调度 callback 不可由本接口抢占。

provider 输出在错误时不作为成功状态使用。尤其现有 setter 会安排 commit；若安排阶段抛出，REJECTED 不保证此前配置字段完全未变。host 在可查询时重新读取真实 applied，错误与读回状态分别保留，不能报告 apply 成功，也不重复回放失败操作。

host 诊断区分 mode、requested、display limit、backend limit、effective、attempted、applied 与最后结果。managed 统一路径将有效非零帧率取合法请求、显示上限和 backend 上限的最小值；未知显示沿用现有 60Hz 策略。legacy/显式 override 的优先级由既有 `host_frame_policy::main_rate` 决定，原选择为 0 时保持 0，不强制增加 pacing。managed 路径在 Host 内部使用已确认的 applied 参与提交节奏；外部实际 pulse 只有在应用显示策略/缓存一致、UI 已采纳反馈并报告相同 active 时才可标记同步，不能由 managed 模式本身推导。

attempt cache 与 applied cache 分开。相同 view generation、模式、策略输入和能力身份下，成功不重复设置；INVALID/REJECTED 不逐 pulse 重试。BUSY/UNAVAILABLE 在后续已有 pump/owner 机会最多额外重试三次，退避 100/200/400ms，不创建新 timer 或后台轮询；耗尽后等待策略、能力或视图身份变化。旧 generation 的完成不得更新当前状态。

host 不能直接控制应用持有的 `frame_pacer`。公共 `bridge_adapter` 增加可选 `bool set_backend_frame_rate_limit(unsigned)`，默认 no-op 并返回 false；0 表示清除后端限制，不是停表。true 只确认 consumer 接收并存储该限制，不证明 pacer 已改变。host 仅在已确认的 managed 主帧模式发布后端上限，退出模式/关闭清 0。回调运行于 Engine owner，consumer 只能同步发布有界状态，不能跨线程调用 UI、stop/join pacer 或引入阻塞。SDK 的公共 `clamp_frame_rate_to_limits(requested, display, backend)` 复用既有显示上限规则，0 backend 不追加限制；非法非零 backend 返回 0。

SDK 自有 pacer consumer 在已有 UI pulse/配置机会读取该反馈，用原 requested、缓存显示上限和 backend limit 重算，必要时在 UI owner 重启 pacer，再通过既有 active_frame_rate 报告真实选定频率。旧 consumer 的 no-op 保持原行为；诊断必须保留 feedback-supported/actual-pulse-synchronized 的未知或 false，不假称宿主已经控制外部时钟。公共 C++ 新虚函数是源码兼容扩展，应用与 SDK 必须配套重编，不能混用旧二进制 vtable。

managed Host 的显示参考统一为 `host_config.owner_window`，与 SDK 自有 Workbench/通用消费者的 pacer 使用同一 owner HWND；仅当 owner 为空时回退 child。monitor 缓存身份与刷新率查询都使用这个引用，不新增窗口 subclass、timer、公共 API 或每 pulse 显示枚举。成功查询及已有 UI/pump 机会下，这消除 owner 与 child 几何归属不同导致的稳定显示上限分歧。UI 与 Engine 异步采纳仍可暂时不同，只有 active 与实际 applied 一致且反馈支持时才保留现有同步标记，不声明瞬时或物理同步。legacy/显式 override 的选择优先级、main0、backend-max 反馈、旧 provider 与 receipt 生命周期保持不变。

两端仍独立缓存，查询失败返回 unknown 时沿用 conservative60；若随后没有 monitor/request/pulse/dirty 变化，Host 不主动重查，只在既有失效机会恢复。应用未处理失效、pacer 调整失败或外部 consumer 自选第三个显示参考也不由本修复保证收敛。不能简单把 backend-max 反馈改成 applied：当前 selected_main 受 active 影响，可能造成降到75后无法回升100的反馈自锁。强于当前合同的原子代次握手或查询失败主动恢复另行设计，不作为本次 owner 参考修复的默认新增行为。

## 兼容与回退

源码SDK必须收录候选来源记录所引用的 `third_party/patches/miniblink/0033-frame-policy-applied-state.patch`。该记录的补丁散列采用原始字节，因此交付归一化对这个精确路径使用none；当前补丁本来为LF，此规则还保护未来合法CRLF字节不被静默改写。该补丁随包不代表六个内核源码文件或新DLL也已随包，既有prebuilt身份继续保留；使用新能力须显式部署匹配候选或按来源重建。

新函数表能力缺失时继续旧 debug 路径，明确标注 applied 未确认。已知旧 fractional 主帧实现的正 rate 保守限制在 240；不能把该范围推断应用到任意未知内核，不能把保守 legacy 模式伪装成 negotiated。请求范围、显示范围与内核范围分别记录。主帧禁用后，legacy draw interval 的恢复仍由 host 既有逻辑负责。

公共 provider ABI 镜像头由单一 owner 同步；不增加必需导出、不改已有字段、不引入 runtime 依赖。新版本可整体退回旧协商路径，但保留真实状态标签；不得重新将无返回值 setter 调用解释为已确认成功。对外新载荷按仓库版本规则分配版本，不复用已发布版本身份。

## 备选方案

补充公共反馈读取：可选 `requested_frame_rate()` 默认返回既有 active_frame_rate；支持者返回原请求，以避免后端反馈改变 active 后丢失原意图。与回调同样要求应用同步状态；新增虚函数须匹配重编。

- 仅在 host 限制 240：能避免已知拒绝，但不能确认 actual 状态与失败；作为旧内核兼容分支，不代替主方案。
- 移除内核上限：没有数值/真实载荷支持证据，拒绝。
- 更换帧时钟、改为 1ms 固定 tick：与缺陷无关，会重做现有 pacing，拒绝。
- 每个 pulse 重新设置直到成功：造成失败热循环，拒绝。

## 验证

最小契约包括 60/120/144/240/360/1000 请求、未知显示、合法 0、CPU override/legacy mode、旧前缀/缺 capability/空指针、无效快照、相同策略去重、generation/能力变更、永久拒绝与三次退避、关闭/失效及实际 setter/getter 读回。source projection 只证明所编译方法，不替代真实 vendor reader 和最终 x86/x64 载荷构建。

owner参考回归抽取production Host的真实选择/monitor缓存/query片段，使用fake Win32检查两调用同一owner、child单独变化不污染owner参考、owner显示变化在已有pulse/dirty机会重查、owner-null fallback与unknown0→60。它不替代真实多屏、DPI、窗口生命周期或物理帧率验证。

实施和测试证据由本次 PR 记录，未执行的图形/物理帧率、焦点、DPI、多屏和长期稳定性保持 pending；不得据本修复宣称提升固定百分比或达到 Electron 水平。

## 最终默认 SDK 集成决定（2026-11-03，实施中）

`preview.1` 源码 SDK 是已冻结的工作树开发快照，保留旧 prebuilt；它与选定 Git 闭集及实际测试的 K1 external companion 不同。最终集成使用新的 `0.7.0-preview.2` 身份，不覆盖或重打原包。普通默认消费者采用 K1 前，须从可追溯的双架构增量来源受控导入，同步运行时文件表、依赖/来源清单与版本锁，并从新安装包生成、构建及运行默认消费者。

已完成受控默认导入、两架构函数表实际加载和运行时/依赖清单同步；来源见 `third_party/manifests/kernel-build-070-preview2.json` 及其引用的增量构建记录。完整物理源码身份由原整树、已接纳的 buffer-retirement 祖先及六处 F01 修改核对，不能简化为补丁散列。普通双架构消费者编译及默认部署已完成；x64 两后端私有非输入桌面的固定 Bridge 工作、保存和自然关闭通过，同闭集 runtime/ZIP 配对增量另记于性能文档。新源码包交付、真实输入和性能验收仍待完成。K1 的增量闭包与 LLVM 工具链资格保持原范围，不改成 fresh whole-kernel/public build，也不转为 v143、物理帧率或生产资格。旧 provider 缺能力时的 unverified 回退继续保留。
