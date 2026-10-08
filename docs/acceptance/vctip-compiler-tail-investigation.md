# MSVC x86 编译器尾进程调查

日期：2026-10-08。范围：本机 MSVC 19.44.35226.0 / toolset 14.44.35207；仅隔离夹具，未修改生产源码、VS 安装、注册表或全局环境。

## 原始问题

`out/0.5.0-preview.1-build-x86-cdc5a1aa5dab4610be7efc007c816776/build.log` 显示 Release 编译成功。对应原始 `owned.ndjson` 记录 root exit 0 后仍有 vctip.exe PID 106224、创建 FILETIME 134359320173048078；30 秒 drain 后监督器终止自有 Job，closureConfirmed=true、activeProcesses=0、passed=false。编译成功和自然进程闭合失败必须分别保留。

## 本地定位

- `C:/Program Files/Microsoft Visual Studio/2022/Community/Common7/Tools/VsDevCmd.bat:250` 的 VSCMD_SKIP_SENDTELEMETRY 只控制异步 DevShell PowerShell 遥测。
- `MSBuild/Microsoft/VC/v170/Microsoft.Cpp.DesignTime.targets:549` 设置供 vctip 使用的 VSTEL_* 标签，不提供关闭契约。
- 当前 cl.exe/c1xx.dll/c2.dll 中可见 VCTIP 的版本化 pipe/ready/mutex、VSTEL_EWDK 等字符串；字符串仅用于提出夹具假设，不能证明环境变量可以禁用进程。
- 官方 [CL 环境变量](https://learn.microsoft.com/en-us/cpp/build/reference/cl-environment-variables) 说明 CL/_CL_ 传递选项，但本次未找到官方保证关闭 vctip 的编译选项或局部环境变量。

## 独立真实夹具

入口：`out/vctip-owned-fixture-20261008-mergegraph/run.cjs`。新建受控 Job，cl 以 /c /O2 编译无外部头文件的代码；static_assert 及 COFF machine 0x014c 独立确认 x86。每项 timeout 10 秒、drain 2.5 秒。开始前没有观察到已有 vctip；每项继续前均要求自己的 Job 闭合，不管理其他任务的进程。

固定工具身份：

- cl.exe SHA256：8de3c1e7af40f4a6c83188ef7cca6270223a00652e8c703022694f64add54a14
- vctip.exe SHA256：1c1ca5f9c04171c02caca6863e29eca3550333219486d23e65bf01fb02951311
- OwnedProcessRunner SHA256：1a4cb4bea83a70ac06efb9c50e26a5c3345aab589f9c2e1306b244f68b09713f

| 条件（均仅对子进程生效） | cl 结果 | vctip/Job 结果 |
|---|---|---|
| 默认 | exit 0、生成 x86 OBJ | vctip 续存，totalProcesses=3，最终强制闭合、passed=false |
| VSCMD_SKIP_SENDTELEMETRY=1 | exit 0、生成 x86 OBJ | 同上，未抑制 compiler helper |
| VSTEL_EWDK=1 | exit 0、生成 x86 OBJ | 同上 |
| EnterpriseWDK=true | exit 0、生成 x86 OBJ | 同上 |
| /d2:-vctip | C1007、exit 1、未生成 OBJ | 仍创建 vctip；自有 Job 已清理 |
| /d2novctip | C1007、exit 1、未生成 OBJ | 同上 |
| /d1novctip | C1007、exit 2、未生成 OBJ | 同上 |

每项保留原始 native NDJSON、固定输入上下文、OBJ、工具 hash 与终态，汇总为同目录 summary.json。首轮 stdout/stderr 由 Node UTF-8 解码，中文诊断产生有损转码；不能作为逐字原始 console 字节引用。C1007 ASCII 编码、原生退出与 OBJ 存在性可独立判断。首轮 summary 的 compilerRejectedOption 只识别 D9002/英语文本，未识别 C1007，不能采用该字段判断后面三项选项合法性；上表依据退出码、C1007 与产物核验。

## 结论与限制

上述范围没有已验证可推荐的抑制变量或选项。VSCMD_SKIP_SENDTELEMETRY 可以关闭 DevShell 遥测提交，但不能据此宣称编译器不创建 vctip。不建议改全局遥测配置、删除 VS 的 vctip.exe 或使用无文档私有选项。

当前可靠的关闭动作是对精确持有且确认归属的 Job 进行整体清理，并验证 Job 归零；这已经由每项原始 terminal 证明。若未来制定编译专用尾进程政策，必须核验完整成员、创建身份及固定 image hash，保持 totalProcesses 与强制清理记录，未知或不可读成员拒绝；不能把 generic runner 的 passed=false 翻成自然退出通过。普通 GUI/宿主生命周期不得继承该编译政策。

短夹具不保证其他编译器版本，也不替代完整 SDK 构建。忽略目录证据尚不是可搬移发布归档。

## 最小 SDK 目标的后续核验

WebView2 收尾修复的首轮 x86/x64 最小目标编译夹具遗漏 MSBUILDDISABLENODEREUSE=1。两个 native 编译 exit 0，但各自 Job 在短 drain 后仍有 MSBuild 节点、conhost 及 vctip，共三个尾成员。保留原始失败在 `out/webview2-preparation-owned-20261008-mergegraph/build-4e5214b7-dacf-4fc6-b22c-cd979192bb6f/`，不能把这轮全部尾成员归因于 vctip。

随后以局部子进程环境 MSBUILDDISABLENODEREUSE=1 和显式 /nr:false 执行两个目标的 /t:Rebuild，native x86/x64 均 exit 0。原始记录在同目录 `build-27227746-dbba-4bc3-93e4-6e1b2633edf3/`。每轮 drain=2.5 秒后只有一个 vctip 尾成员，MSBuild 与 console helper 已自然退出。监督器终止精确自有 Job 并确认归零，仍保留 passed=false。这区分可局部禁止复用的构建节点与尚无验证抑制方式的编译器 helper；不改变 GUI runtime 的自然退出准入要求。
