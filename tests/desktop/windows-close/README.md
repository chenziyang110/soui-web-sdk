# Windows DOM 关闭原生回归

此独立安装消费者要求 Windows x64、MSVC v143、Windows SDK、实际 WebView2 Runtime
及交互式桌面。只链接已安装的 `Soui::Desktop`，不使用 fake、语法检查或外部 UI 注入。
从仓库根目录执行，`out/desktop-install` 应为待验证源码安装所得 SDK：

```powershell
cmake -S tests/desktop/windows-close -B out/windows-dom-close -G "Visual Studio 17 2022" -A x64 -T v143 -DCMAKE_PREFIX_PATH="$PWD/out/desktop-install" -DSOUI_DESKTOP_GUI_TESTS=ON
cmake --build out/windows-dom-close --config Release --target soui_windows_dom_close
ctest --test-dir out/windows-dom-close -C Release -R '^sdk.desktop.windows-dom-close$' --output-on-failure
```

测试默认不注册；只有显式启用 `SOUI_DESKTOP_GUI_TESTS` 才运行。程序有 30 秒公共
`dispatch`/`quit` 看门狗，超时失败，不强杀应用；CTest 的外部上限为 40 秒。
权限只开启本窗口的控件及资源服务，不使用系统对话框、剪贴板或快捷键。

断言包括：

- root 页面真实连续调用三次 DOM `window.close()`；宿主全部否决后同文档 Bridge
  和窗口状态仍可读。WebView2 可能抑制即时重复事件，只要求 1–3 次审批。
- 保持 root 存活，依次新建两个 child，均由首次 DOM 关闭触发一次批准和一次 closed。
- 批准回调中两次重入公共 `request_close()` 均拒绝 `close_busy`，不重复 closed。
- 已关闭 child 的两次请求均拒绝 `closed`；程序 `close()` 幂等。回调退出后销毁
  child handle，观察捕获对象释放；后一 child 不改变旧实例的审批和关闭计数。

每次运行向 stdout 输出一份固定 JSON 报告，成功退出 0。应同时保留源码、SDK、exe
散列与实际进程退出码。它验证正常注册/销毁路径和可观察回调生命周期，不证明 COM
`remove_*` 调用次数，不包含注册失败、队列饱和、异常、失效 sender 或导航竞态注入，
不代替可见界面验收、原模板证据或完整平台矩阵。

2026-10-26 的扩展回归首轮自然退出 0；其后仅修正 fixture 空 handle 失败路径和页面
错误记录。生产候选及链接的 SDK 字节未变。最终 fixture 配置/编译成功，但正常启动
收到 Windows 拒绝，**最终 fixture 运行仍受阻**，不能沿用首轮 exe 的通过结论。
现有记录将错误解释为应用程序控制拦截；本地化原文已乱码，没有可靠原生数值码，
不据此确定具体策略产品。未重试、改安全策略或采用替代执行路径。精确身份、原失败、
通过范围及未运行项见 [ADR 0138](../../../docs/adr/0138-cancellable-user-close.md)。
