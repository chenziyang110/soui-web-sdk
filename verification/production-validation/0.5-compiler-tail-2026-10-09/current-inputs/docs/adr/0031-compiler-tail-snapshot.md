# ADR-0031：编译尾进程的固定磁盘对象与闭合快照

- 日期：2026-10-08
- 状态：implemented-native-and-client；独立本机 x86/x64 无 GUI 切片验证，正式 Build.ps1 调用者接入与独立复审待完成
- 范围：独立原生 owned Job 监督器的可选证据；不改变通用通过条件或 SDK 身份

## 问题与边界

编译命令 exit 0 后仍可能有 MSVC vctip 辅助进程驻留。现有监督器正确判为 owned-descendants-remain、终止自己持有的整个 Job，并保留 passed=false。进程名称、事后磁盘散列、一次不完整 PID 列表不能把这次通用失败变成通过。编译例外只能建立独立、调用者事先声明且可重算的证据结论，不能隐藏 console helper 或未知成员，也不能借此放宽运行时退出要求。

## 接口

既有 CLI 与 soui.owned-process-run/v1 start 保持。仅显式编译调用可成组提供：

`--pin-tail-image ABS_PATH --pin-tail-sha256 LOWERCASE_SHA256 --snapshot-nonce LOWERCASE_HEX32`

三个选项必须同时出现且不可重复。调用者在启动前生成 nonce，另行固定 MSVC 14.44.35207、目标架构与允许的精确 vctip 文件；监督器不按名称白名单。测试可固定独立测试 helper，不能冒认实际 MSVC 实验。最终 terminal 增加 preCleanupSnapshotSequence 和 tailEvidenceComplete；它们不改变通用 passed、reason、退出码或整个 held Job 清理。

## 启动前固定

CreateProcess 前只接受本地固定 NTFS 上的规范驱动器绝对路径，拒绝 UNC、设备路径、ADS、点段、尾随点/空格和任何 reparse 组件。逐级打开并持有目录，核对属性与实际路径且不共享删除。最终文件由非继承 GENERIC_READ、FILE_SHARE_READ 句柄持有至 Job 闭合；拒绝目录和 reparse 文件，使用该同一文件句柄计算 SHA256，再核对预期散列。

保存 GetFinalPathNameByHandle 的规范 DOS 与 Volume GUID 路径、GetFileInformationByHandle 的 volume serial、64 位 FileIndex、文件大小和 SHA。FileIndex 与 FILETIME 使用十进制字符串，不转浮点。SHA 证明所持磁盘文件的 bytes，Windows API 返回的映像路径与打开磁盘对象对照不认证执行内存、注入或加载 DLL。

prepin 必须在创建 subject 前完成；成功记录先缓存在内存，Job 分配后先输出既有 v1 start，再输出 pin 事件，保持既有客户端的首行身份契约。pin 保存实际 prelaunch UTC FILETIME 与观测 tick 字符串，不把写出时间当作固定时间。初始化失败没有 subject/start，保留 pin.error 和失败 terminal，客户端不能声称 ready 或闭合通过。

prepin 的观测预算为 2000ms、单文件上界 64MiB、每块 64KiB；路径、metadata、读取和散列 API 前后检查预算。snapshot 总观测预算为 1000ms。同步内核 I/O 无法由 elapsed check 强制抢占，这些约束限制工作量并拒绝超预算结果，不构成硬实时保证；外层 owned Job 监督器仍须保留 timeout failure。

## 决定性清理前快照

独立事件 schema 为 soui.owned-tail-snapshot/v1，统一 nonce 与从 1 开始的严格 sequence。阶段包含 pin/prelaunch、snapshot.begin/member/end、pre-cleanup-confirm、cleanup.confirm 和 tail-evidence。每次候选有 attempt（最多三次），所有失败尝试也记录；总预算 1 秒，每次 PID 列表最多四次扩容且最多 64KiB。QueryInformationJobObject 即使成功，只要 Listed 小于 Assigned，也继续扩容。[官方 PID 列表契约](https://learn.microsoft.com/en-us/windows/win32/api/winnt/ns-winnt-jobobject_basic_process_id_list)。

snapshot.begin 保存 accountingBefore；snapshot.member 的 index 从零连续，包含 held PID、创建字符串、Job/存活核验与所打开 diskObject；snapshot.end 保存 accountingAfter、assigned/listed/uniquePids/members、第一次及第二次完整 pids、complete/eligible/stickyFailure。pre-cleanup-confirm 引用该 end.sequence 并记录最后核对。cleanup.member 记录同一 PID/FILETIME 的真实等待结果；cleanup.confirm 两次记录空 accounting、固定 total 与 held signal；tail-evidence 和 terminal 的引用必须相同。正式 reader 须从原始 stream 重算，不能仅信任布尔标记。

可决定的候选要求 accountingBefore.Active、Assigned、Listed、唯一 PID 数、持有成员数、accountingAfter.Active 全部相等；total 前后相同、limitTerminatedProcesses 为零；第二次完整 PID 列表一致。重复 PID、无效 PID、index 空洞拒绝。每个成员保留非继承 process handle，实际核对 IsProcessInJob、GetProcessTimes 的创建字符串、Wait(0) 存活和映像磁盘对象与 prepin 对象。

自然数量/集合变化可重试；权限、身份读取、image-open、对象或 hash 不匹配等错误 sticky，不能重试后洗成通过。未知成员或多个成员不能得到例外证据。适用原因限 rootExitCode=0、owned-descendants-remain，且决定性快照恰有一个 pinned 成员。超时、停止、断连、root 失败没有例外。

终止前再次核对 accounting、完整 PID 集合和每个 held FILETIME/Job/存活；不相符则拒绝例外。清理始终 TerminateJobObject 整个自己持有的 Job，不按名称或 PID 广杀。固定清理前 total，终止后要求两次 accounting 空、所有 held process signaled、final total 等于固定值；清理期间不更新这个基数。最终 terminal 引用决定性 snapshot.end 的 sequence，证据完整结论独立于通用 false。

Job 的 ActiveProcesses 归零可能早于 held process 对象 signaled。例外证据在既有 Job 清理后，以另一个总计 1000ms 观测预算等待 held process 实际 signal，并重读 FILETIME，再进行两次空 accounting；预算到期拒绝。不以 Sleep 或空 accounting 代替退出信号，不改变通用清理预算。

## 验证计划

先用无 GUI 原生 Job fixture 验证单一合法 pin、未知尾、多个尾、pin 写入/rename 拒绝、动态累计 total 变化，以及无关持有进程继续存活。所有 fixture 只管理新创建且 held 的进程。编译由基础监督器管理、节点复用关闭、SDK 19041、独立 out，失败与尾成员证据保留。不改全局 telemetry、私有 cl 选项、系统配置，不运行 GUI/VM 或完整 SDK 构建。

Windows 最低版本仍为 Win7，新增 CNG 普通 SHA256 不使用 Win8 才有的 reusable flag；FileIndex 使用 Win7 已有 API，不引入 FileIdInfo。NTFS 与规范路径限制只适用于 opt-in 证据。此设计不是 Win7/PCA 实机验证。

CNG 不成为默认 loader 依赖：只有 pin/hash 分支使用 GetSystemDirectoryW 的系统目录绝对路径加载 bcrypt.dll，并核对每个所需 export；不使用要求 Win7 额外更新的搜索标记。缺少 DLL/export 时，在创建 subject 前失败；默认 no-pin 路径不加载 CNG。这是源码/导入边界，不替代裁剪 PE 实机验证。

## 本机结果与未闭合门槛

最终独立最小构建的两次 configure 和两次 build 本体均 exit 0，无 warning/error 匹配。两个 configure Job 自然闭合；两个 build Job 各留一个 vctip，bootstrap 监督器按通用失败清理，passed=false 保留，没有启动前 pin 便不能追认为例外。两架构新增测试各 15/15，既有 generic runner/client/client-boundaries 各 22/22，测试外层 Job 自然闭合。真实九成员验证列表扩容；独立 TEST_ONLY API fixture 验证成功但 Listed<Assigned 的响应仍扩容、普通数量变化可第二次候选通过、重复/零 PID 与第一次 image 权限错误 sticky。TEST_ONLY mutation fixture 仅替换生产调用点，真实增加 suspended assigned 成员后执行真实 TerminateJobObject；total 从固定值增加一使例外拒绝，不能称为真实抢占调度复现。

首轮原始失败保留：Job Active=0 但 held process 尚未 signaled，未被投影成通过；后续改为预算内等待真实 signal。首轮编译 wchar→char 警告也保留，显式 ASCII hex 窄化后的构建无警告。

实际 MSVC 14.44.35207 的 Hostx64/x86 和 Hostx64/x64 文件另做同句柄 prelaunch pin，分别实测 335560/335520 bytes 与配置散列相符；该轮只运行 brief console-free fixture，不启动 vctip、不产生 compiler-tail 通过结论。Hostx86 下两个 305312-byte 文件不同，不可按 target 架构或 basename 自动等价。

全部 ignored out 原始记录尚待主代理正式归档，见 [本机证据说明](../acceptance/0.5-compiler-tail-evidence.md)。未运行 GUI/VM、实际 MSVC 编译尾的 opt-in 接受、Win7/PCA/PE 实机或对执行内存/DLL 的认证。JS context/reader/独立编译政策在本切片实现；正式 Build.ps1 调用者仍由主代理整合。通用 Job passed 与运行时退出门槛不改变。

## 官方依据

## JS 调用与独立接受政策

`launchOwned(executable,args,{...,tailImagePin:{image,sha256,nonce}})` 只接受完整组；`image` 为规范驱动器绝对路径，SHA 为 64 位小写 hex，nonce 为调用者在启动前生成的 32 位小写 hex。客户端将三者和实际 supervisor argv 写入 context；默认 no-pin 分支不加载新 reader、不调用 randomBytes，也不增加原生选项。首次 ready 仍只采用首行 v1 start 的 PID/FILETIME，停止、超时、断连与通用 passed/124 均保持。

启动时复制调用者 argv 和 pin，不保留调用者可变对象引用。context 的 `report` 和 `runnerArgs` 只加入 opt-in 分支，原默认 v1 context 不变。首次独占写入 bytes 的 SHA 为结束时基线，关闭时实际读取 context 文件重算；I/O 错误或被改写使 tailEvidence 拒绝，finished 仍 resolve failure。若实际原生 terminal 已确认 Job 归零，保留独立 closureConfirmed；不能用它绕过 context/report 错误。返回新增的 contextSha256 与 tailEvidence 也仅在 opt-in 分支出现。

`readTailEvidence(rawBuffer,{context,reportSha256})` 从同一实际读取 Buffer 重算完整 NDJSON、连续 sequence、nonce、唯一 prelaunch pin、全部候选和决定性快照、清理前最后确认、held process signal、两次空 accounting 与 terminal 的固定 total/reference。uint64 字段严格规范十进制字符串，拒绝溢出与浮点复原。自然成员变化仅允许有限候选；任何 sticky/error、未知枚举、未知镜像、多个尾、身份/磁盘对象不符或截断不能被后来的 true 洗成通过。返回独立 `accepted`，客户端将其作为 `tailEvidence` 暴露，不改 `passed`。

`acceptCompilerTail({reviewedInvocation,context,contextBytes,rawReport,buildReportBytes,result,actualScriptSha256,currentSourceSha256,currentArtifacts})` 仅由审查过的 Build.ps1 调用者显式调用。reviewedInvocation 固定可执行文件及 SHA、完整 argv、cwd、脚本 SHA、架构/配置/sdkOnly、source SHA、buildId、原始 context/report/build-report SHA 和产物身份；实际重新读取 bytes、脚本 SHA、当前 source SHA 和当前产物必须显式提供并完全相符，缺失不得跳过。Build.ps1 路径与标准 PowerShell 参数、MSVC 14.44.35207 的 Hostx64/x86 或 Hostx64/x64 精确文件及各自核过 SHA 同时限定。source-build/v2 必须为本次成功构建，buildId、source、产物和构建时刻须绑定新 subject；旧报告不追认。

reviewedInvocation 的 schema 为 `soui.reviewed-compiler-invocation/v1`，必填字段为 executable、executableSha256、runnerSha256、args、cwd、scriptSha256、sourceSha256、buildId、version、architecture、configuration、sdkOnly、contextSha256、reportSha256、buildReportSha256、artifacts。artifacts 按 source-build/v2 固定 `{payloads,optionalLibraries,...library,...optionalPayloads}`；每个实际产物 manifest 同时核对路径、文件 bytes/SHA、总 bytes 与 encoded files SHA。contextBytes/rawReport/buildReportBytes 必须来自实际新读取的 Buffer；result 为本次 launchOwned 的真实 finished。reader 和 policy 都不信任已有 result.tailEvidence，policy 再从同一 rawReport 重算。

argv 必须以唯一 PowerShell `-NoProfile`、`-ExecutionPolicy Bypass` 和 `-File ABS_CWD/tools/Build.ps1` 指定脚本，可选 `-NoLogo`/`-NonInteractive`；脚本参数显式且唯一 `-Architecture x86|x64` 与 `-Configuration Debug|Release`，仅允许 Build.ps1 已有的 SdkOnly/SkipWeb/WithWebView2/Install/FreshConfigure 开关。其他命令、未知参数或重复选项拒绝。Build startedAt/finishedAt 使用实际 v2 UTC 字符串，拒绝 start 早于 held subject 创建 FILETIME 的历史报告；该时钟一致性边界不提供可信时钟认证。

原始流拒绝重复 JSON key（包括转义后同名）、无效 UTF8、空行、尾部截断、过深嵌套和越界 count/uint64。FILETIME、FileIndex、size 从不转 Number 后做身份比较。观测数量/集合可以有限重试，但固定 total 不得变化；明确身份丢失、读取错误或任何 sticky 不能用后续成功候选覆盖。

返回 `compilationCompleted`、`genericSupervisionPassed` 与 `compilerTailAcceptance` 三个独立结论。例外只适用于 root exit 0、owned-descendants-remain、supervisor 124、单个已固定 helper、强制 whole Job 清理确认；STOP、timeout、spawn/report/观察错误、fallback、未知枚举、任意 GUI/test 调用和历史 unpinned stream 均拒绝。纯数据回归使用明确 synthetic fixture；真实 helper 夹具只能证明 reader/client 链路，不能证明 MSVC 编译接受。

## 2026-10-09：历史诊断与最终尾资格的边界修正

首个真实 x64 Release、WebView2、Install opt-in 编译本体 exit 0，通用监督仍 false/124，原例外政策拒绝 `Unknown generic member image`。原始尝试保留在 `out/actual-compiler-tail-9c838133-e825-4557-9b05-a09c4bc3c843`；不得改写 policy/report 或按新源追认这次构建为 fresh。完整 raw 有 67 条 image 查询失败、53 条成员不可用和 7 次历史 PID 不同 FILETIME，全部位于 root-exit 之前，均非 root 或最终尾 PID。最终三份完整 PID 列表、固定 total=647、同 disk pin、held signal 和双空 Job 已闭合。这些早期诊断本身没有 Wait 记录，不能逐个宣称该时刻已自然退出。

Root 批准的最小修正只限严格格式的 generic 历史诊断：事件必须位于唯一 root-exit 之前，PID 既非 root 也非最终 selected tail；image=null 必须有正 DWORD 错误码，成员不可用/不再属于 Job 必须匹配监督器已知字段形状。创建时间仍是正、规范 uint64 字符串，历史 PID 复用必须推进完整 FILETIME。保留并返回历史 image/unavailable/membership/reuse 计数，不把它们用作尾身份凭据。允许该范围不等于证明编译每个短进程的身份。

root 或最终尾 PID 的任何早期读取失败、成员不可用、membership 丢失或 FILETIME 不一致仍拒绝；root-exit 之后的上述异常及 PID 复用仍拒绝。未知 event/schema/字段/非法类型不能进入诊断豁免。所有权威 snapshot 的完整枚举、sticky error、PID/FILETIME/disk/nonce/固定总数与 cleanup 规则保持原样。历史 generic PID 记录不进入候选 held-identity map，避免把正常编译中退出后复用的 PID 当成同一 process 对象；候选/确认/清理间的对象身份校验不变。

本节限定前述“未知枚举/镜像/身份错误”的资格范围：权威快照与受保护 root/tail 始终严格拒绝，只有明确非候选、退出前的已知历史诊断按本节保留。原负例保留；新增历史正例、格式异常、protected PID、post-root、sticky/cleanup 拒绝回归。新证据单独保存于 `out/side-tail-history-98a0df4b-704e-439b-a7f8-de8f3806d619`，不覆盖 native/JS 旧 freeze/index。旧 raw 可只读验证新 reader；编译政策仍必须拒绝与当前源码 SHA 不符的旧构建。后续由 root 在修复冻结及独审后重新实际编译，不改变通用 false/124 或发布门槛。

这里的 reviewedInvocation 是调用者的显式审查数据与重新读取 bytes 的绑定，不能成为新的安全身份认证。同一用户可以改写整包 context/raw/build report/审查描述或客户端；nonce、SHA 与磁盘对象核对不防该主体重写证据，也不认证进程内存或 DLL。

- [CreateFile 共享与非继承句柄](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-createfilew)：不共享写/删除直到句柄关闭，删除权限也覆盖 rename。
- [GetFileInformationByHandle](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-getfileinformationbyhandle)：volume serial 与 FileIndex 对照磁盘对象。
- [GetFinalPathNameByHandle](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-getfinalpathnamebyhandlew)：实际规范路径与 Volume GUID。
- [BCryptOpenAlgorithmProvider](https://learn.microsoft.com/en-us/windows/win32/api/bcrypt/nf-bcrypt-bcryptopenalgorithmprovider)：常规 CNG API 的平台边界。
- [系统目录](https://learn.microsoft.com/en-us/windows/win32/api/sysinfoapi/nf-sysinfoapi-getsystemdirectoryw)、[LoadLibrary](https://learn.microsoft.com/en-us/windows/win32/api/libloaderapi/nf-libloaderapi-loadlibraryw)、[GetProcAddress](https://learn.microsoft.com/en-us/windows/win32/api/libloaderapi/nf-libloaderapi-getprocaddress)：仅 opt-in 时按系统目录绝对参数请求 DLL 并核对接口；不声称认证 loader 重定向、依赖 DLL 或执行内存。
