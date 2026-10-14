# ADR-0083: portable host AMD64 build debug observer

Status: proposed; Source and pure checks only in this delivery change.

The LLVM build reader needs evidence for short lived compiler, RC and MT
processes which the original Job polling may report as unavailable. A real
independent experiment observed the complete SDKOnly x64 Release debug tree:
121 inner CREATE/EXIT identities, 122 naturally closed outer members and 59
paused whole-command queries. That experiment does not qualify this portable
integration or the normal application graph.

The default standard build and schema1 retain their existing route. The
explicit `--host64-build-observer` route consumes a separately prepared,
SHA-bound host AMD64 receipt. Preparation is a standalone CMake producer under
the unchanged LLVM22MSVC142/SDK19041 profile. Its three original Owned captures
prove natural configure/build/Ninja-command completion; compile database,
object, physical compiler/linker, actual link command and PE checks separately
prove the headless Windows subsystem and `wmainCRTStartup` CRT entry. These
captures do not claim complete CREATE observation. The build caller never
compiles a missing observer or selects an arbitrary command fallback.

Schema2 records two actual roots: the outer original Owned subject is the
observer, while the inner PowerShell Build subject is identified by held
CREATE, paused whole argv and EXIT. There is no synthesized schema1 start or
PowerShell context. The native program authenticates a bounded JSON input and
binds its SHA to every raw frame; current Source, tool, NTDLL, profile, producer
and existing runner pins are checked before and after the attempt. Twelve
fixed host AMD64 image roles reject unknown images. A short argv spelling is
accepted only when its held physical file matches the selected CREATE file.

Every inner member retains PID/positive FILETIME/main thread/Job/live/file
identity and a complete natural EXIT history. Only root, CLANG, RC and MT get
paused WMI whole-argv queries, each with a bounded worker and confirmed join.
The single initial loader breakpoint must belong to the pinned NTDLL code
range and main thread. Ordinary first chance exceptions are propagated with
`DBG_EXCEPTION_NOT_HANDLED`; last chance exceptions and RIP reject completion.
Tool probe nonzero exits remain numeric facts. Build, observer and outer Owned
must finish naturally with zero; outer total must equal inner history plus
exactly one. Failure closeout keeps the original records and checks inputs.

Hashing uses noncopyable CNG owners declared as provider, backing buffer, then
hash. Reverse destruction therefore destroys the hash before freeing its
backing, including when file I/O, hashing or allocation throws. This follows
the [BCryptCreateHash lifetime contract](https://learn.microsoft.com/en-us/windows/win32/api/bcrypt/nf-bcrypt-bcryptcreatehash).
The Source/static lifetime check does not claim an executed native fault.

Once preparation creates its fresh attempt directory, its guard covers plan
writing, runner and dependency checks, environment/argv setup, all captures,
complete producer validation and publication. An immutable
`producer-validation-closeout.json` supplies validation input; it is distinct
from the final attempt closeout. No prepared receipt is published until both
complete validations and the last successful Source/input postcheck finish.
Any later rejection checks known inputs again and marks the final closeout
failed. Only the attempt's new, unsealed, exact-pin-matched closeout can be
updated; original captures and sealed history remain intact. If that pin or
sink fails, a fresh `producer-failed-closeout.json` is attempted instead. The
original rejection remains the thrown Error; secondary closeout errors remain
on that Error even when all evidence sinks fail. Existing strict producer,
raw, profile and natural closure consumers remain mandatory.

The original polling raw stream, unavailable diagnostics and actualToolRows
are preserved. Independently raw-bound `fastToolEvidence` supplies a separate
process identity route. CMake/profile/ABI, current selected Ninja database and
default graph, dependency/header, RC wrapper/source/res, archive/object,
link/MT, actual output and installed SDK identity gates remain mandatory.
Configuration probes are not target graph resource coverage. Prior and
installed records explicitly distinguish schema1 and schema2 and re-read the
original raw captures. A projection SHA is never a raw SHA.

Host helper AMD64/Windows 8+ is separate from SDK x86/x64 output and Windows 7
runtime targets. Actual portable producer, both architecture normal builds,
installed consumers and runtime/OS checks remain pending. This change does
not promote memory quota, performance, compatibility, kernel, ABI or release
qualification.
# 最终收尾角色

独立复核发现公开消费者还会接纳内部 `validation-input` 快照。公开
`validatePrepared`、`loadPrepared` 及历史记录重读现在只接受 `phase=validated`。
准备过程中第一次完整验证使用模块私有 Symbol 能力，仅允许其内部快照；该能力
不导出，调用者自造同名 Symbol 不能授权。最终验证仍用公开默认规则。无 phase、
failed 和未知角色均拒绝；旧 producer 不会因此重新获得资格。角色回归是明确的
纯依赖模型，没有运行编译器或伪造原生成功。
