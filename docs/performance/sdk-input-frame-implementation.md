# SDK 输入与帧策略性能候选

当前源码版本为0.7.0-preview.2，未发布。此项优化公共SDK及宿主/内核路径；不修改Scratch工作量，不以rAF或短启动代替物理性能验收。

## 最终默认载荷接续

`preview.2` 已将双架构 K1 产物受控导入默认 prebuilt，并同步 runtime-files、依赖和新的来源记录；见 `third_party/manifests/kernel-build-070-preview2.json`。23,066 份完整物理源码与原整树、已接纳祖先和六处 F01 修改一致；两架构静态必需导出及真实加载的函数表 ABI 已检查，未初始化 renderer 或创建窗口。此为增量受影响 reader 闭包的采用，不是 fresh whole-kernel/public build、v143 或应用性能资格。

新的原始人工页面被动观测工具已实际编译 10 个工具、15 个对象；局部 waitable timer 修复后，单独重编两个 CPU fixture，通过 31 项被动观测及 18 项资源采样合同。生产工具复用同源已成功编译的切片；原整批因 fixture 误用进程伪句柄而失败的记录保留，不追认为整批通过。原生 API 反例已核对：MsgWait 拒绝伪句柄、接受 DuplicateHandle 得到的真实进程句柄。生产观察器原本就持有真实目标句柄。页面/独立读取器另有针对性 Node 合同。

当前 r3 观察器使用本地高精度 waitable timer，不可用时回退标准 timer；保留原绝对截止、进程身份、DXGI 丢帧及生命周期检查，不设置全局 timer resolution，也不激活、移动、注入或关闭用户窗口。两轮真实人工配对均未合格：r2 因 window-destroyed 提前结束，r3 因 window-hidden 在约 29.7791/8.6659 秒结束，并有 7/6 次未知观察；不能从它们判定 SDK 输入到屏幕改善。r3 的观察循环间隔明显缩短，只说明诊断工具本身，不是 SDK 收益。页面 Finish/保存接纳仍不能代表原生 30 秒观察完成；不以文字提示冒充技术完成门。完整入口已绑定 SDK2 消费者，首次 Node 别名路径误判和各轮失败原件保留。暂不继续要求用户重复同类采样。

后续已实现显式原生完成门，详见 [Host 契约](../../benchmarks/input-ui-host/README.md#原生观察完成门)：独立 controller/Host incarnation、run/nonce/HWND 与原绝对截止绑定，页面提前点击得到 pending，下一次人工点击重新取数，不自动重试。gate16、signal13 合同各在 x86/x64 通过，页面16项通过；相同源/LLVM22 的 baseline0.6 与 SDK2 Host 均已编译，独立 Control 实际编链及真实控制器身份查询通过。旧 InputTrace 字节身份及原10工具/34源码清单保持不变。新的两个入口纯文件预检通过，没有因此启动窗口或认证人工30秒观察，原失败不追认为通过。

正向窗口组合另在一个私有非输入桌面检查：实际 HWND/class、未置位 gate、独立 Control 信号、原 Bridge 接纳、正常关窗后持久化及 checked-close 均通过，617 份普通冻结文件和两份固定工具身份前后一致，Host/controller/broker 自然退出、Job0、无强清。这是有意主动发送结束信号的功能 fixture，没有运行实际 observer，pointer/wheel 计数均0；不认证实际提前点击回执、持续前台、30秒物理观察或性能。此前两次 fixture 准备/启动接线失败保留，成功仅绑定修正后的独立轮次。

`preview.2` 已以 LLVM22/MSVC142/19041 完成双架构 SDK/WebView2 库增量编译和各 471 文件 SDK-only 安装，546 份选定来源构建期间不变；旧库归档与 `preview.1` 安装未改。这一版本接续未重复未改动的 16/31 项旧合同，也不代表 v143 资格。Web/React/Vue 编译、客户端 31 项、交付契约 28 项通过。三份 npm 与原生 SBOM 已实际生成，七份 raw/delivery 文档通过官方 1.5 schema 校验；原生 136 文件重新计算散列，22 静态链接输入沿原 producer 记录且明确未重建、未重算大 archive，传递组成保持 incomplete。

普通七源消费者另从这两架构安装库各编译一个真实 TU，并执行生产 runtime/resources 默认部署；各 270 文件闭集核对通过（EXE、59 DLL、206 许可材料、3 页面资源及部署收据）。运行时表直接选中默认 K1，没有 external kernel override；两侧旧源码、库、基线和 Git index 未改。十阶段在同一 300 秒预算内约 41.73 秒自然退出、Job 清空、无强清。x64 EXE 为 2,266,624B，x86 为 1,893,376B。

这份普通 x64 消费者随后在私有非输入桌面分别运行 MiniBlink 与 WebView2：各完成 32 次预热、1,024 次正确的 4KiB Bridge 往返、最多 8 个在途请求、保存与自然关闭，Host、监督器及受持有 broker 均自然退出，Job 清空，无强清。MiniBlink 使用默认 K1；WebView2 使用实际核对的本机固定运行时 156.0.4314.58，未认证完整外部运行时闭集。这是当前载荷功能证据；没有执行 x86 应用，也不认证真实前台输入、IME、有效帧或性能改善。

同源消费者的 x64 体积对照使用相同 270 文件布局、三页面及固定 ZIP 规则：EXE 2,196,992→2,266,624B（+69,632B）；DLL 合计 44,335,328→44,336,352B（+1,024B）；runtime 47,514,083→47,584,617B（+70,534B，0.1484486%）；ZIP 17,502,157→17,528,079B（+25,922B，0.1481075%）。许可材料及页面字节相同，收据减少 122B，没有新增 DLL。旧基线的许可目录仅在新的配对副本映射为当前部署布局，原载荷不改。此为应用、SDK、内核和版本元数据合计增量，不能归因单项优化，也不包括完整安装 SDK 或外部 WebView2。x86 仅有候选清单，缺少匹配基线 EXE，整包配对增量保持 unknown；原零增长判定仍失败。

以下 `candidate-script-source-sdk-r1`、SDK-only 安装、运行和体积数据仍绑定 `preview.1` 的原始产物。`preview.2` 的普通应用功能及同闭集体积以本节新记录为准，最终源码包与性能仍待验，不继承历史资格。K1 相对 SDK 原默认内核的增量是 x64 6,656B、x86 5,632B，与下文 K0→K1 的 1,024/1,536B 属于不同基线。

以下历史 `preview.1` 验证包含 ADR0159 的私有三段诊断、消费者缓存解析及后续 script builder，对应 BuildID 为 `candidate-script-source-sdk-r1`；当前 `preview.2` 构建身份以本节记录为准。此前 `candidate-native-phases-sdk-r1` 与 owner/admission 候选及其 ZIP 保持不可变，下文按各自身份说明验证。新增观测默认关闭，不改变15项trace/WKE布局或调度策略。

在该编译候选之后，当前源码另整合了局部script builder，以`candidate-script-source-sdk-r1`完成独立代码审查和双架构相关编译。它直接向同一局部owning string追加JSON字面量，普通字节按连续区间复制；resolver/reply的最终JS文本、原jsEval/wkeRunJS与全部guard保持不变。该变化只涉及C++构建成本，不能把它描述为消除JS编译或降低以下API内采样均值；旧构建、真实功能与体积仍保持各自身份。

## 实现与接入

- 输入接纳在既有mailbox事务中生成唤醒receipt，仅empty→pending通知Engine；拒绝后的首次ordered recovery也有通知。保留FIFO、ordinal、容量、IME/capture、终态和协作截止。
- MiniBlink provider提供可选query/apply尾扩展，当前非零主帧范围30..240、0禁用。host区分requested/effective/attempted/applied，失败有界重试/去重。旧内核void setter明确unverified，不将调用成功冒充实际应用。
- 公共adapter的可选后端上限反馈只发布状态；应用在既有UI机会通过公共clamp helper更新自己的pacer。旧adapter默认不支持，须配套重编。详见[API](../api.md)及[ADR0154](../adr/0154-frame-policy-applied-state.md)。
- managed策略的monitor身份与显示查询统一使用owner窗口，owner为空才回退child，与SDK自有pacer对齐参考。两端仍独立缓存，未知显示沿用60Hz且只在原失效机会重查；异步采纳、pacer失败及自选第三个参考的consumer不保证同步。backend-only反馈不能直接改成effective，否则可能覆盖回升意图；当前限制见ADR0154。
- WebView2缓存有界method/session字段并保留原短路错误/超时语义，原Bridge协议与授权解析仍在。缓存超限或分配失败回原有解析路径。
- MiniBlink排队worker只复用接纳阶段的bootstrap/probe/ordinary分类，随同一不可变请求按值保存；direct入口仍在原guard之间解析，实际Bridge授权与session清理不变。设计见[ADR0157](../adr/0157-miniblink-queued-method-route.md)。该独立优化不是已定位的吞吐下降修复。
- 原生回调另以私有同步metadata复用首次解析的独立ID/method/timeout，仅最终enqueue移动owned raw。公共enqueue保留原解析，worker不捕获视图，原receipt与接纳顺序不变；见[ADR0158](../adr/0158-miniblink-native-admission-metadata.md)。原serialization埋点只包围被省去的第二次parse，不能将其样本减少当作全部JSON成本消失。
- WebView2显式输出准备对必败大response跳过重复试算，event仍允许parse/dump压缩；prepare默认保持关闭。可选冷诊断 `SOUI_WEBVIEW2_OUTPUT_TIMING=1` 使用原trace阶段，无新公开布局。原bridge_response仍为COM前观测，不能解释为页面执行/显示完成。

## 已有证据的准确范围

最新script builder候选只构建受影响SDK与两个fixture，每架构精确2项CTest实际通过：16组脚本字节等价合同和31组原生阶段合同。348份所选源构建期间不变，两个新SDK-only前缀各471文件，六个旧安装树和四个归档库未改；工具链仍限定LLVM22/MSVC142/19041。x64同一fixture另执行一次固定CPU片段，每实现每种输入128次，输出字节和checksum相同。普通4KiB/near64KiB reply每次分配由4次减至1次，pending由3次减至1次；重转义即时回复由6次减至4次，但该片段累计requestedAllocationBytes约增加10.75%，不能称全部内存指标改善。这不是峰值/RSS或JS API/FPS数据，固定旧新顺序的wall time仅作局部观察。

匹配该候选的普通七源x64消费者另完成实际配置、编译与链接，EXE为2266624B；构建与一次独立功能轮均自然退出、Job清空，无强清。该轮真实bootstrap/probe、32次预热及1024次4KiB echo全部正确，失败/超时/错包/待回均0，报告实际保存后只发送一次owned WM_CLOSE，Host/Runtime正常关闭，137项冻结输入前后不变。它没有运行重负载采样，也不继承前一phase候选的耗时分布。

最终同七源、同LLVM的269文件运行闭集相对既有基线：EXE增加69632B、内核增加1024B，runtime增加70656B（约0.148759%）；同一固定ZIP规则增加25970B（约0.148449%），59个DLL名称集不变。原零增长gate仍false，收集exit0只表示记录完成；小增长按用户要求如实报告，不增设数值通过线。此项是整个候选接入、版本/BuildID、反馈开关和K0/K1的总差值，不能归因到单一builder，也不代表完整安装SDK或外部WebView2 Runtime。源码SDK小包合同在纳入ADR0159及本次私有文件后实际16/16通过，仍不是完整source SDK打包或官方生成器实测。

新增三段诊断的SDK以LLVM22/MSVC142/19041完成x86/x64 Release受影响目标编译，各9项精确CTest实际通过，新增helper各31项CPU合同通过；两个新安装前缀各471文件，362份所选源在构建期间不变。诊断消费者另完成配置、编译和COM/窗口初始化前的35项CPU合同，均自然退出、Job清空。另用旧七份普通消费者输入链接新SDK，仅用于与既有同七源、同LLVM基线核对体积；不能将八源诊断工具的解析器成本算作普通接入必需增长。新载荷的物理输入、有效呈现和性能收益仍待验，未提升performanceVerified或releaseReady。

该候选的唯一MiniBlink Bridge4KiB/30秒私有非输入desktop轮正常保存和关闭，133份冻结输入前后不变、Job清空、无强清；旧scalar与新三段分别合格。首次离线分析因静态身份字段路径错误失败，原件保留，修正后只重读同次已保存数据。phase缓存捕获相距29.5353482秒，两个cache age为295.4663/763.9956ms。首次解析完成28740次、平均inclusive约21.038µs、P95/P99桶上界均51.1µs；resolver完成28740次、约135.342µs、均≤409.5µs；reply完成28733次、约122.220µs、P95≤204.7µs/P99≤819.1µs，结束仍1次in-flight。三段可能重叠/跨界，不能合计CPU份额、相减分位数或当输入到屏幕耗时。实际内核jsCall会强制gesture，不能等价替换原resolver jsEval；realm内函数缓存尚缺可信早期安装证明，因此当前不采用。

同七源、同LLVM的普通消费者配对已有基线，实际269文件运行闭集增加71168B（约0.149837%），其中EXE增加70144B、内核增加1024B；相同固定ZIP规则增加25657B（约0.146660%），59个DLL名称无增删。原零增长gate仍false，收集成功不代表零增长通过；用户允许小量增长，未另设通过线。该结果属于`candidate-native-phases-sdk-r1`与普通七源接入，不包含后续script builder，也不代表完整安装SDK或外部WebView2 Runtime。

F02当前限定决定是不在普通opaque SDK DIRECT_HWND且无DC/readback consumer的默认路径引入damage-history：源代码在同一composite owner内同步持有lease、完成绘制/GdiFlush并释放后才进行下一次buffer写入，与Canvas/Bridge实际copy0及非零callback/lease工作吻合。透明、持久DC、readback和旧consumer可有不同路径，不能据此认定它们无切槽成本。GPU/相位路线保持原默认；缺少due-lateness、真实transfer完成与物理呈现的部分继续标为未测。

F03 已按 [ADR0160](../adr/0160-private-render-phase-cost-observation.md) 实现独立、默认关闭的七指标成本诊断；不改变公开 trace v1 布局。固定 bank 的 14 组合同及实际 hooks 的 6+1 组合同均已在 x64/x86 原生执行通过。诊断接线以八份 vendor 源码、七个 TU 的外部 overlay 保存，默认 vendor 物理树、K1 DLL、SDK2 库及安装前缀未改。关闭或普通未注册线程不新增 QPC，指标分别记录同一 WTF 时域的唤醒迟到，以及 raster、复制、原生提交调用的 inclusive wall duration；不能合计为 CPU 占比或解释为物理呈现。

首次实际诊断内核构建的两个 x64 编译器均自然成功，但原 InputFrameTrace 已使用的三个 MSVC STL 头未被旧 40TU 工具清单覆盖，严格 include 守卫因此拒绝整轮。原失败保留；新增同一有限 MSVC142 include 根的逐文件清单后，复用唯一已完整通过守卫的 WebThread 对象，13 次新编译和两次链接实际自然成功，约 25.55 秒完成，无强清。6,040 份实际最终链接输入前后核对、两份诊断 DLL 的架构正确，默认 vendor/K1/SDK2 与真实 Git index 不变。真实冷 ABI 两架构正负探针通过，迟到配置拒绝、旧 K1 在加载前拒绝；诊断消费者各一个 TU 编译并显式部署 K2，各 270 文件核对通过。原探针缺少标准头和原消费者 profile 接线失败保留，成功不转挂原失败。

该 x64 诊断消费者随后只运行一轮私有非输入桌面 Canvas240：原预算 0、请求 60Hz，原 10 秒预热及完整 30 秒记录，1,799 模拟步、1,800 绘制、432,000 角色绘制调用。Begin/End、报告和最终 scalar/private 快照落盘后由应用自行关闭，343 冻结文件前后相同，App/controller/broker 自然 0、Job0、无强清或系统输入。rAF 60Hz 仅是页面回调，不是物理 FPS。SDK/kernel/private 同 run `4c3bd3990a6b27b89e667b3d838f42ba`、PID102012/incarnation/QPF10MHz，六个 producer 的固定直方图守恒；原生捕获窗约 30.0025 秒，私有累计约 41.3466 秒，包含预热和关闭。

| 私有指标 | 样本数 | P95 桶范围（ms） | P99 桶范围（ms） |
|---|---:|---:|---:|
| Shared wake lateness | 2,952 | 1.024–2.047 | 2.048–4.095 |
| Task due lateness | 24,979 | 0.512–1.023 | 1.024–2.047 |
| Raster inclusive wall | 2,603 | 0.8192–1.6383 | 0.8192–1.6383 |
| Context submit inclusive wall | 5,030 | 0.8192–1.6383 | 1.6384–3.2767 |

迟到分位数仅包含有效 due 且非提前 dispatch；另有合计 2,467 次提前、8 次缺 due，不按某一指标分摊。Spare/mirror/layered 三项样本为 0，成本保持 unknown。尾部的两个 task 观察落在 131.072–262.143ms 桶、一个 raster 在 26.2144–52.4287ms 桶；范围不是精确最大值，缺少逐帧关联时不能归因启动或重复等待。各范围不可相加、不可计算 CPU 份额或相减构造阶段，也不能用新诊断 DLL 对照默认载荷宣称收益。本轮保留默认 GPU、相位源及 GDI/租约屏障；当前未建立切换策略的收益依据，其他透明/DC/readback、不同负载及物理呈现仍待相应证据。

最新owner显示参考和native admission metadata以独立BuildID `candidate-owner-admission-sdk-r2` 完成x86/x64 Release受影响SDK/Host实际编译，各8项精确CTest通过（metadata各33+4内部例），官方SDK子目录安装各471文件，356份所选源在构建期间不变，旧route安装不变。工具链为LLVM22/MSVC142/19041，不称全SDK v143验证；没有重编内核。首次x64编译已自然成功，但随后CTest筛选使用不支持的非捕获组，原始输出被JSON解析拒绝，整体r1失败保留。修复为普通捕获组后，实际CTest只读预检精确8项；续接按同源及当前产物快照复用已成功x64编译，不重复编译，x86仅一次。匹配新SDK的有限LLVM消费者功能及下述同编译器运行包体积已完成；物理性能仍待验。旧route载荷、人工输入、模板和体积结果不转挂给新改动。构建后只更新非编译文档状态，构建时源清单仍保留原字节身份。

新x64独立generic消费者通过原工具链验证hook，以LLVM22/MSVC142/19041重新配置、单TU编译和实际链接新安装库，两个阶段均自然0/Job0。原v143模板已出现owned vctip退场失败，不在没有新假设时机械重试；本轮不算v143监督资格。私有功能首轮因监督JSON尚未写完即被读取而整体失败，应用虽已正常退出，控制器仍被清理，原失败不追认通过。仅将任务本地publisher改成同目录临时文件关闭后Windows原子且不覆盖rename，四个实际无窗口文件合同通过；新轮用同SDK/EXE、同driver/validator/截止与负载，完成真实bootstrap/probe、32预热+1024次4KiB异步echo、primary+saved保存和一次正常关闭，零错误/超时/错包/待回，Job0、broker完整枚举缺席、无强清，131输入前后不变。这是正常路径功能，不是导航重入、可见输入或FPS验证。

为消除新LLVM EXE与旧v143 EXE的编译器混杂，仅从既有a4fc安装SDK新编译一次同LLVM的基线generic消费者，未重建SDK/内核、未启动基线窗口。两侧七个原始构建输入、三资源和实际编译器profile相同；旧API反馈OFF与新反馈ON、版本/BuildID及K0/K1仍属于整体接入差异。实际269文件闭集（EXE/59DLL/206legal/3资源）：EXE2196992→2218496B，增加21504B；node增加1024B；runtime47496950→47519478B，增加22528B（0.047430%）；同固定ZIP规则17494230→17503044B，增加8814B（0.050382%）。DLL名称集无增删，原输入前后不变。原零增长gate仍false，外层收集exit0只说明记录完成；小量增长按用户要求单列，不归因某一处parse，不代表整个SDK安装或外部WebView2体积。

0.7.0-preview.1的前一开发构建（route优化前）已完成x86/x64实际SDK/WebView2宿主增量编译，两架构各5项选定CTest通过、无skip；相应安装各471文件与构建库及公共头一致。该构建还从安装SDK在仓库外编译并运行原公共消费者，共四个无renderer进程自然退出0；这些历史结果不改挂给后续route源码。实际内核每架构40个受影响reader及链接通过，其余源等价对象/库有证据复用，不是fresh整内核。

此前route开发构建以独立BuildID `candidate-route-sdk-r2` 记录：两架构受影响SDK目标实际编译链接、41个方法语义用例各通过；官方SDK子目录安装到新前缀，各471文件，旧前缀不变。首次根安装的x86步骤因未构建RuntimeDoctor失败，保留失败；后续SDK-only安装成功不冒称完整根分发通过。对应x64 v143消费者实际链接该构建库，单次私有非输入desktop真实bootstrap/probe、32次warmup及1024次4KiB异步echo、保存和正常关闭完成，零错误/超时/错包，Job清空、未强杀。该轮不作吞吐收益比较，也未覆盖bootstrap中途失效或任意并发。

内核七文件的原始字节正反重放、补丁0033及两架构候选DLL散列见 `third_party/manifests/miniblink-frame-policy-state-20261102.json`；相对a4fc匹配基线的内核增量为x64 1024B、x86 1536B。这只是内核组件大小，未代表完整EXE/runtime/ZIP增量。仓库既有prebuilt和历史导入SOURCE-MANIFEST未替换，消费新能力需部署同一已验证候选内核或后续匹配重建；缺capability时仍明确回退unverified。

x64真实公共Host在独占、非输入Win32 desktop挂载，用独立控制器持有Engine；公共focus在释放Engine之前完成真实owner→child焦点切换，FIFO fence、Host销毁、Runtime关闭和Job全部进程自然退出。该轮使用明确冻结的中间候选库/内核，验证无Engine同步等待及新capability接线；不能推断可见输入、IME或FPS。

合成held-owner burst中，输入receipt减少重复通知；WebView2 CPU切片确认少一次路由DOM解析，大回复guard减少失败准备的重复工作。它们不包含真实显示链路，不将局部耗时比例折算为FPS提升。无窗口输出准备对照还发现部分场景prepare增加总成本，故未默认启用。

正式InputUiHost另外核对完整SDK版本：根VERSION可用preview/rc，CMake包选择使用数值EXACT，实际imported头仍必须与完整版本相等，不能将相同数值的两个preview视为同一SDK。14项真实CMake配置契约及4项Node检查通过；精确a4fc基线0.6.0与当前候选分别用MSVC v143/19041完成x64 Release正式Host构建。Workbench也以安装候选SDK、原应用CMake及四个实际C++翻译单元/RC完成同配置编译链接，独立审查无阻断；两项均未启动GUI，不代替完整部署/交互验收。

通用[SDK对照消费者](../../benchmarks/sdk-performance-host/README.md)保留30秒Canvas/Bridge模式，并新增独立固定工作模式：32次warmup后完成1024次正确Bridge echo目标，8在途、MessageChannel有界续发、共同deadline与drain。固定模式正式段不运行rAF或周期仪表，报告成功工作量、总耗时、RTT和续发原语；它包含JS/native transport与调度，不能称纯native极限或物理输入延迟。31项页面契约和独立审查通过，实际吞吐验证继续进行。原rAF报告仍按显示/调度观测解释，不关闭vsync制造性能结论。

route优化前的私有非输入desktop固定4KiB/1024次Bridge诊断取得两后端各一次ABBA，均真实保存并自然退出。MiniBlink当时的候选两中点相对基线约下降8.31%，基线两次spread约3.65%、两对同向；按预先固定规则保留为需调查的历史下降信号，未归因到SDK或内核。WebView2表面中点约增加18.01%，但基线spread约23.22%超过预定5%，因此结论为不确定，不能宣称端到端改善。每variant仅两次，不估置信区间、不反复加测到满意；任务副本移除了原UI/rAF，不能与人工30秒页面或物理性能合并。监督器首次因desktop身份查询失败的轮次仍失败，未纳入统计。

新的route源码对应独立MiniBlink匹配ABBA亦已执行一组，原页面、32暖机/1024成功目标、4KiB/8pending和预先固定判据不变；四轮自然退出、Job清空、未强杀，严格进程快照另确认原broker已退出。基线范围3465.55–4247.46次/秒、spread约20.275%，候选范围2990.57–4252.76次/秒；候选中点差约-6.090%，两配对差约-13.706%/+0.125%，方向相反。因基线波动超过5%且两对异号，本轮结论为不确定，不确认旧下降仍存在或已经消除，不归因route，不追加轮次。旧负样本继续保持历史身份；新一组不替代物理输入/帧率验收。

x64 r2消费者的指定269文件随包闭集已测量：EXE增加20480B、匹配MiniBlink增加1024B、app-local runtime合计增加21504B（0.045469%）、固定ZIP规则增加9169B（0.052519%），没有新增或移除DLL。该口径不包括外部WebView2/OS或整个安装SDK；新fixed-r3资源有独立身份，不能沿用r2的体积数字。旧零增长检查的失败保留，用户允许小量增长不改变旧门槛的真实结果。

最新route消费者采用相同正式fixed-r3三资源和269文件闭集重新核对：EXE相对基线增加20992B、内核增加1024B，runtime共增加22016B（0.046533%），实际固定规则ZIP增加9498B（0.054387%），59个DLL名称集不变。这是对应消费者运行包体积，不能与前一轮不同页面的ZIP大小直接相减来归因route修改，也不代表全部SDK或外部依赖大小。

当前route SDK的人工输入三预算记录中，0和1000µs正常退出，SDK/kernel scalar完整；生命周期分别24.4105s、8.2497s，SDK输入队列样本1580、765，P95均落在上界0.8191ms的桶，P99均落在上界1.6383ms的桶。这是入队到取出前的等待，不含handler/绘制/呈现；同桶不能证明两者相同或改善。1000µs有一次admission拒绝，scalar无法还原原因。2000µs触及150秒活动保护期限返回3，正常清理且原scalar可读，只作失败诊断。实际人工负载/持续前台不可比，故默认预算保持0，采用决策仍pending；不能从kernel含合法timer延迟的task_queue桶推断输入阻塞。

原desktop-app模板另以当前安装SDK完成x64 Release、MSVC v143/19041的真实编译链接、正式Web构建及官方runtime/resources/manifest部署。配置和编译的命令根均为0，但各自监督器因owned vctip进程未按期退出判124，失败和清理记录保留，完整构建验收仍false；后续三个独立部署阶段自然0不覆盖这两项失败。新载荷275文件，在私有非输入desktop执行原smoke-test，真实应用就绪、Host关闭和Runtime退出，应用/controller/broker自然0、Job清空、无强杀，1313项冻结输入前后相同。首次可选窗口观察器提前中断的失败保留；修正该观察器后单独一轮通过，未改变SDK、模板、隔离或期限。该结论仅为当前载荷功能，不代表官方New-Project、完整Build流程、可见交互或性能通过。

普通重负载页面原已支持scalar初末与最终导出；route候选的Canvas240/Bridge4KiB在私有非输入desktop各完成一次30秒诊断，SDK/kernel scalar合格、112份冻结输入前后不变，进程自然退出、Job0、无强清。index仅调用原控件，benchmark.js/CSS及工作量不变；真实private DOM焦点/可见性原样保留为有效，仍不认证前台/物理性能。Canvas完成1799固定step/1800draw，kernel composite P95/P99桶上界约1.639/3.277ms；Bridge attempted28036、success28032、pending4，SDK provider约0.052/3.277ms、FIFO约1.639/3.277ms。两轮无input_queue样本，copy_ops/bytes均0只适用于该私有路径，不排除真实窗口F02成本；含故意timer等待的task_queue不当UI积压。首次native parse及独立copy/raster耗时仍缺，不能分位相减、估CPU占比、把counter分数当CPU平均成本或unique帧率。这些样本仍绑定旧route-r2，未含新的owner/native metadata改动。

## 待验与回退

完整物理输入/有效帧、0/1000/2000µs预算默认决策、持续owner公平性及F03未测阶段仍缺相同设备/工作量数据；F02默认路径的限定不采用不扩展到DC等消费者。最新普通消费者的EXE/runtime/ZIP已按上文限定口径核对，源包、官方模板生成器/完整构建监督、可见应用交互与最终整合审查的结论另以对应交付收据记录。已取得的限定模板功能不替代这些门槛。当前performanceVerified/releaseReady均不提升。

性能候选可以逐项回退，保留权限、generation、输入终态、lease/GDI fence与关闭修复。帧接口需要缺能力回退，不能回退成无返回值setter伪成功。设计：[输入receipt](../adr/0153-input-publication-wake-receipt.md)、[帧策略](../adr/0154-frame-policy-applied-state.md)、[metadata](../adr/0155-webview2-request-dispatch-metadata.md)、[输出准备](../adr/0156-webview2-output-preparation-observation.md)。
