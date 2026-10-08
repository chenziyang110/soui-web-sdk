# ADR-0025：WebView2 准备取消与异步初始化收尾

- 日期：2026-10-08
- 状态：implemented；本机 x86/x64 Release 独立短验证通过，完整集成验收另行执行
- 范围：现代 Host 的环境/控制器准备生命周期；不改变 Bridge 容量策略

## 问题与证据

`prepare_async()` 可以在环境创建完成前返回。当前 `unmount()` 在环境和控制器尚为空时返回成功，随后 adapter、HWND 与 STA 可以被消费者销毁。创建回调只持有 weak state，普通 `boundary()` 对关闭或失效状态直接返回，未记录异步创建是否已经真正收尾。

`webview2_prepare_tests.cpp` 的取消用例恰好执行这一路径：发起环境创建，立即 unmount，销毁 adapter，然后结束 STA。2026-10-08 x86 Release 的 22 项 CTest 均通过，但独立 owned Job 在 root exit 0 后仍有一个 msedgewebview2.exe，10 秒及另一轮 30 秒 drain 均未自然归零；监督器正确记录失败并关闭本 Job。正常模板的两架构 Release 短启动闭合。以上支持优先调查取消路径，尚不等于已证明全部残留根因。

原始证据保留在 `out/0.5.0-preview.1-desktop-fd6c4693-ba28-416f-b6e9-151f85a8104e/native-x86/owned.ndjson` 和另一轮 `out/0.5.0-preview.1-desktop-c47af484-ee2b-4b32-8103-f1915d8b16ea/native-x86/owned.ndjson`；忽略目录不是可搬移发布证据，正式归档由集成验证负责。

## 决定

1. 区分用户可见的准备完成、原生异步创建 settled、Bridge worker 完成和浏览器进程组退出。取消结果回调立即且恰好一次；这不表示所有原生资源已释放。
2. 在调用异步创建 API **之前**登记 pending。在 HRESULT 同步失败或真实完成回调到达后恰好释放一次。取消不凭 elapsed time 清除 pending。
3. 取消后不再创建新的 controller。已经发起的 controller 创建在回调到达后，若返回真实 controller，必须在所属 STA Close；不安装页面、策略或 Bridge，也不改变取消结果。
4. 创建完成回调使用独立的准备生命周期上下文；关闭后的清理不能被正常业务 `live()` 判断吞掉。上下文不使用消费者栈引用或失效的原始 Host 指针，迟到失败、异常和同步提交失败同样收尾。
5. `unmount()` 在初始化回调尚未 settled 或原生回调仍在执行时返回 `E_BUSY`，保留可重试的状态。调用者保持 HWND 与 STA message pump，在顶层有界循环中重试；不能在 WebView2 回调中嵌套消息泵、阻塞 Wait 或 Sleep 猜测完成。
6. 回调已经 settled 后关闭 controller，释放 core/controller/environment 等自有 COM 引用。公开契约原本要求消费者在 unmount 成功后才销毁 adapter/Bridge；违反这一契约不允许 SDK 虚报已经完成清理。
7. 不以进程名称杀运行时，不排除 Job 中的浏览器成员，也不修改全局运行时配置。测试拥有唯一 profile；需要进程组退出证明时使用真实 API/持有身份与 owned Job 原始报告。

## 初始化 settled 与浏览器退出的边界

WebView2 要求拥有消息泵的 UI STA，创建及完成回调都在该线程；阻塞或提前结束泵会阻止完成回调。依据：[线程模型](https://learn.microsoft.com/en-us/microsoft-edge/webview2/concepts/threading-model)。

`ICoreWebView2Environment5::BrowserProcessExited` 可用于观察对应浏览器进程组及用户数据目录释放，必须 QueryInterface 成功、在清理前注册并保持必要环境引用；不把事件接口可用当作已经发生退出。如果环境创建尚未完成，则还没有可注册该事件的环境对象。若环境创建失败且没有实际环境，也不能声称等待过 BrowserProcessExited。

共享 profile 可能有其他消费者，不能要求普通 unmount 阻塞等待别人的浏览器。初始化 settled 与取消后不创建 controller 是本修复的必需契约；浏览器退出是独立、按实际拥有范围验证的结果，无法取得或未收到事件时记录不可验证/未完成，不降级为成功。依据：[Environment5](https://learn.microsoft.com/en-us/microsoft-edge/webview2/reference/win32/icorewebview2environment5)。

## 验证与完成条件

- 复用旧失败原始 Job 记录作为 RED，保留 CTest 本体通过与外层闭合失败的差别。
- 私有生命周期测试用受控回调顺序覆盖：同步 API 失败、真实回调异常、立即取消、取消后的环境成功/失败、已经创建 controller 的迟到完成、重复用户回调拒绝及结果回调中的重入关闭。无需依靠定时 Sleep。
- 真实准备测试在唯一 profile、UI STA 和有界顶层泵中确认第一次取消时 unmount 为 E_BUSY，用户回调一次，随后真实回调 settled 后关闭成功。不能启动取消后的 controller。
- x86/x64 独立 owned Job 短跑记录实际 EXE/创建时间、当前 runtime/profile、原生退出及完整 Job 归零；不延长既定 drain 以掩盖问题。
- 原生异常或超时必须返回失败/保持 E_BUSY，不能仅凭用户取消回调、COM Release 或 root exit 0 计为闭合通过。

## 限制

这项修复不构成 Win7、PE、全部 WebView2 Runtime、长期资源收敛或完整发布准入证明。当前为本机明确运行时的取消与迟到回调验证切片；更多环境由 05-10 单独验收。

## 2026-10-08 实施与局部证据

私有 `preparation_lifetime.h` 记录环境、控制器和预加载的 pending 与当前执行回调，不改变公开 `engine_thread` 或 Host 接口。取消环境完成时不发起 controller；已提交的 controller 迟到时关闭它。若该 Close 失败，保留 controller 供后续 unmount 重试，并保留 E_WEBVIEW2_CLOSE。准备结果回调的 E_CALLBACK 在关闭成功后仍可查询，清理成功不能抹去调用失败。

`tests/native/webview2_preparation_lifetime_tests.cpp` 使用受控顺序验证 pending 环境、pending controller、同步提交失败、重复完成、回调异常和重入时计数尚未释放。它验证生命周期账目，不把模拟计数等同于真实 COM Close 失败或实际环境失败。

`tests/native/webview2_prepare_tests.cpp` 可按 `invalid`、`failure`、`cancel`、`reentrant`、`callback-exception` 隔离执行，也支持原有无参数完整运行。真实立即取消先返回 E_BUSY，保持 STA 顶层泵至晚到环境回调收尾，再成功关闭；用户结果仍只回调一次。真实 controller 创建及预加载失败、成功结果回调中的重入关闭、结果回调抛异常均独立检查。

局部执行入口为 `out/webview2-preparation-owned-20261008-mergegraph/run.cjs native`；执行时持有 Local\SouiDevKit.DesktopValidation。原始结果在 `out/webview2-preparation-owned-20261008-mergegraph/native-fe9aee17-9740-46e5-81a6-35b0b481c40e/`。每架构七项（生命周期、五个独立分支、无参数完整运行），共 14 项，均 native exit 0、reason=completed、activeProcesses=0、closureConfirmed=true、terminatedOwnedJob=false、passed=true。固定 timeout=60 秒、drain=2 秒；未扩大 drain、忽略浏览器成员或使用按名称清理。Job 总成员数原样保留，包含 console helper，不能当作浏览器或 Node 数量。

实际 Host 架构由 32/64 位测试输出确认，准备完成的 runtime identity 为 154.0.4258.62。每项保留固定 EXE/runner SHA、PID/FILETIME、完整 NDJSON 和 stdout/stderr 原始字节，读取后核对原始报告 SHA。上述 ignored out 目录仍需由根集成验收归档；本轮局部夹具只绑定执行 EXE 与 runner，完整冻结源树的构建绑定由正式 Build/Test 验证提供。

尚未独立注入真实 COM Close 失败、环境 API 同步失败或取消后返回失败 HRESULT，也没有以真实 UI 调度确定性停在 controller-pending 状态。其所有权规则经源码与受控私有生命周期测试审查，不宣称这些原生错误分支已经实际触发。

局部 14 项完成后，加强了 callback-exception 用例：先保存并断言准备结果为成功，再验证用户回调异常产生 E_CALLBACK，避免准备本身失败被该断言误接纳。上述局部证据属于加强断言前的历史切片（其实际 runtime identity 非空）；加强后的测试由根的完整四配置 Build/Test 重新验证，不宣称已经本地重跑。
