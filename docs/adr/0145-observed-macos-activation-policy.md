# ADR-0145：以观测策略确认 Mac 初始化及保守错误提示

状态：接受。限定启动健壮性修复；真实根因仍需目标复验。

## 新证据

2026-10-24 正常 CUA 启动 StartupTraceConsumer 取得真实阶段记录：SDK 已经过
listener、sharedApplication、activation 的 enter/return；应用 initialize 返回后完成
析构，进入错误提示，停留在 dialog_modal_enter 而未记录 return。与此前普通 shell
的 bind/EPERM 不是同一启动上下文，不互相套用。当前轨迹支持定位到激活检查失败
后的错误流程，不证明 AppKit 死锁，也尚未记录当时真实 activationPolicy。

## 决定

- 初始化首先确认 NSApplication 非空，避免 Objective-C nil 消息的零返回值被视为
  Regular。已有 ownership/event-loop 检查不变。
- 以当前策略为 Regular 作为条件。初始已 Regular 时不重复调用 setter；否则仅
  调用一次，再读取实际策略。无论 setter 返回什么，最终非 Regular 都返回原有
  E_APPLICATION；实际已 Regular 才继续。不是忽略权限失败或强制前台激活。
- 诊断枚举只在旧编号后追加固定 initial/setter/observed 分类，不输出任意运行数据。
  首次轨迹没有这些分类，因此不能宣称已证实“原本 Regular 却误判”的具体根因。
- 生成应用错误框在 runModal 前要求 NSApp.isActive，否则记录 dialog_inactive_skip
  并返回 false，保留 stderr 与非零退出。不改变激活策略、不自动重试、不增加 timer
  或新的事件循环。活跃是瞬时状态，不保证可见，也不能消除检查后失焦；可能跳过
  本来稍后能显示的警告，这是 best-effort 错误提示的保守取舍。

## 官方依据

[setActivationPolicy:](https://developer.apple.com/documentation/appkit/nsapplication/setactivationpolicy(_:)?language=objc)
报告切换尝试是否成功；[activationPolicy](https://developer.apple.com/documentation/appkit/nsapplication/activationpolicy()?language=objc)
返回当前策略。Apple 文档没有保证重复设置相同策略的特定布尔结果，本实现不依赖猜测。
[isActive](https://developer.apple.com/documentation/appkit/nsapplication/isactive?language=objc)
仅表示当前活跃状态；[NSAlert.runModal](https://developer.apple.com/documentation/appkit/nsalert/runmodal()?language=objc)
是同步 app-modal 调用。不能以新 alert 尚未显示时的 isVisible 检查代替正常呈现，
也不把 app running 或 modal_enter 当窗口可见证明。

## 验证与兼容

私有 C++ 决策 helper 使用 8 项组合检查初始状态、setter 布尔结果、后置观测和
调用次数：已满足不调用 setter/getter，未满足只各调用一次，以真实后置状态决定。
这不是 AppKit 或系统权限测试。新增固定 trace 后运行生产 sink 专项，并核对 SDK/
模板策略头字节一致。Mac 编译、正常桌面运行和新阶段证据另行记录，当前不继承
上一构建的运行资格，不替换仍在运行的旧包或复用已有记录文件。

## 2026-10-24 新候选的限定 Mac 实跑

在独立新消费者上，5 个构建必需源文件 blob 与云端冻结候选一致；Release
arm64/x86_64 SDK 及消费者的构建、安装、staging、verify 成功。通过正常 CUA
启动确切新包，实际看到窗口；`app.info` 返回正确名称、标识和版本，模块加载与
同源 JSON fetch 成功。阶段记录显示初始策略已 Regular，观测仍 Regular，因此
未重复调用 setter，随后 window/WebView/show/activate/load 均返回并进入 run loop。
正常关闭后记录 run return、quit、destroy、app_exit，应用清单 isRunning=false；
后续 close 返回 procNotFound 与已退出相符。

这验证了新候选在该 Mac 的最小窗口/Bridge/模块/资源与关闭链路，不代表完整六纵切、
Intel slice 执行或其他平台通过。新包成功且初始 Regular，支持避免无必要策略切换的
修复方向，但旧包未记录初始策略，不能倒推旧次调用的真实状态或泛化全部 Finder
失败原因。本次正常路径没有进入错误框，不计为 inactive skip 或错误提示可见性实测。
未改安全策略、强制激活、签名或复用旧 trace 文件。

### 可定位产物与证据

路径均相对该次任务源码目录，不提交用户主目录：

- SDK build/install：`out/startup-policy-sdk-build`、`out/startup-policy-sdk-install`。
- 消费者源码/构建：`out/StartupPolicyConsumer`、`out/StartupPolicyConsumer-build`。
- 应用：`out/startup-policy-consumer-staged/StartupPolicyConsumer.app`；
  标识 `org.soui.startuppolicyconsumer`。
- staging：12 个 manifest 文件、1,463,205 字节；manifest SHA-256
  `96e3387390350d1407ea4590b69d3c771ffb26e7a9b6a4d34bea9f9bcaffd097`；
  executable SHA-256 `4a5bc5d382f42b2f333362d680a23616fb669de946af1fcb382ab82b5ae19be9`。
  清单仍为 `nativeRuntimeVerified:false`，没有因独立 GUI 验证而篡改生成字段。
- `uname` 为 arm64，两个 Release SDK 静态库及 executable 均含 arm64/x86_64。
  未单独记录进程实际 slice，因此这里只声称 Apple Silicon 主机实测；x86_64
  只有编译证据，无 Intel 运行证据。本轮未保存截图，不提供虚构截图附件。

SDK 阶段文件 `out/startup-policy-sdk.trace` 为 818 字节，应用阶段文件
`out/startup-policy-app.trace` 为 374 字节；两者均为 0600、单硬链接。
以下名称每行均带固定前缀 `SOUI_STARTUP_TRACE/1`，按各文件自身顺序记录：

- SDK：sdk_listener_enter → sdk_listener_return → mac_shared_application_enter →
  mac_shared_application_return → mac_activation_enter → mac_policy_initial_regular →
  mac_policy_observed_regular → mac_activation_return → mac_window_enter →
  mac_window_return → mac_webview_enter → mac_webview_return → mac_show_enter →
  mac_show_return → mac_activate_enter → mac_activate_return → mac_load_enter →
  mac_load_return → mac_run_enter → mac_run_return。
- 应用：app_main_enter → app_initialize_enter → app_initialize_return → app_run_enter →
  app_run_return → app_quit_enter → app_quit_return → app_destroy_enter →
  app_destroy_return → app_exit。

这些路径和散列定位本次保留的本地产物，未上传二进制或私有 trace 文件。
