# 构造外壳有限故障回归

一个测试 EXE，七项 CTest，每项 TIMEOUT 10。构建前强制检查五个 live 源文件的完整 bytes/SHA256、fixture/切片 hash 和十一个 source/fixture 精确字节区间。activation registry 直接编译匹配固定 bytes 的实际 ActivatingObjCheck.cpp/header；constructor、destructor、owned factory、init helpers 与 paint bind/invalidate/permit helpers 原字节复用，PaintLifetime.h 使用当前实际头，不重写生产所有权算法。
## Current source binding refresh (2026-10-26)

The current CWebView source adds the ViewDiagnosticsState include and one
view_created/view_destroyed call in its constructor/destructor. The extracted
constructor and destructor now contain those exact source calls. The other nine
extracted bodies are unchanged; all eleven intervals and full source/payload
pins remain strictly checked against current bytes.

The finite fixture supplies explicit noexcept observation hooks and counters.
The view-success case checks one creation call after live publication, then one
destruction call after close/unregister. Shell and recoverable-failure cases
require no completed-view observations; both terminate children must also show
zero calls before the existing strict witness/exit-86 check can succeed.
These support counters do not exercise DLL diagnostics or prove native resource
coverage. The production ownership bodies are not replaced or reimplemented.

Unified validation uses target soui_vendor_ctor_shell_tests and the
kernel.native.lifetime.ctor_shell- CTest prefix. The same seven names and their
10-second limits remain; the assertions above are new and await unified compile
and execution. Historical results below retain their original identities.


覆盖未 init 外壳成功/析构、handler 在 registry publication 前真实分配失败、完整构造成功注册/撤销、activation std::set 分配失败后的真实 Win32 lock 复用、外壳失败后的 activation remove、engine seam 与最终 live std::set 分配失败时实际 std::terminate。

两项终止由同一 EXE 子进程执行；父进程必须同时验证固定 witness 原始字节（LF/CRLF）及精确 exit 86，才能返回 0。不以 WILL_FAIL 接受任意失败。超时返回失败，外部测试运行器/Job 负责失败子进程的真实闭合；不声称超时自然排空。

string/callback declarations、PageNetExtraData/RefPtr/HashSet、WebPage::init/close 仅提供声明和故障支持。成功 fast allocation 不证明 WTF/PartitionAlloc OOM 可恢复，支持 HashSet 的 std::set 不证明生产 WTF allocator。engine seam 不执行实际 Blink/GC/compositor；权限支持不验收 broker ABI。该有限回归不关闭 pending_ctorOOM、compositor producer admission、worker/GC 退出、完整 vendor DLL 或源构建资格。

2026-10-26 仅更新 SOURCE：当前构造/析构确实包含 paint lifetime 绑定与撤销，fixture 提供冷 owner/状态声明 seam；live-insert 故障仍只在 engine seam 完成后触发，不能提前移到状态分配。成功构造项检查实际 idle paint policy 的 target 撤销与计数排空，deferred scheduler seam 不应被调用。未执行生成、配置、编译或测试；下方历史结果仅绑定当时源码。

切片保存于强制 LF 的 .h。局部 .gitattributes 强制 CMakeLists.txt、*.cmake 与属性文件自身使用 LF，并仅允许两个捕获 header 保留真实 vendor 行尾空格；完整源码与字节区间 gate 仍逐字节验证它们，不放宽测试判断。最后函数体之外的多余 EOF 空行不保留。

2026-10-09，full-native-suite-v5 在同一 Windows 开发机的 x64 与 Win32 各通过 87/87 项注册 CTest，其中本目录 7 项公共回归均通过。CTest Owned Job 的根进程/监督器退出码为 0/0，无强制 disposal 并自然闭合；配置/编译的 0/124 仍为 genericPassed=false 的受控尾进程 partial，整体 allGenericPassed=false。该有限实测不关闭 Win7/PE、GC/OOM 或 DLL 卸载资格。finite source-body scope=true，fullGCQualified=false、pending_ctorOOMClosed=false、fullVendorBuildQualified=false、releaseReady=false。相对路径支持整体源码搬移；父测试入口由维护者集成。
