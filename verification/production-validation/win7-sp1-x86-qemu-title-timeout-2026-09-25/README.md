# Win7 SP1 x86 窗口线程停滞短测准备

状态：**冻结输入和脚本已准备，来宾尚未执行**；`releaseReady=false`。本候选来自提交 `6742ad4d`，源码清单、x86 SDK 静态库和测试 EXE 的 SHA-256 见 [input.json](input.json)。x86/x64 已编译新增的 `--window-thread-timeout` 可选场景，各自六项无窗口原生测试通过；可选 GUI 场景尚无运行结果。

该场景在另一线程创建真实 HWND，暂停其消息处理约 1.2 秒，要求公共 `window_service::set_title` 在 600 ms 内以 `E_WINDOW_TIMEOUT` 返回；随后检查被撤销的旧标题没有生效，并在窗口线程恢复后确认新中文标题成功。它直接验证公共 C++ 服务的调度边界，不运行模板 WebView，也不能关闭页面 Promise、关闭竞态或 V03 整组验收。

只读来宾传输位于 `D:\SOUI-SDK-QEMU-validation\guest-title-transfer-20260925`，包含测试 EXE、输入记录、[来宾命令](harness/run-title.cmd)和串口导出脚本；传输内容与冻结输入逐项核对。四份 PowerShell 脚本通过语法解析，串口校验器通过合成 Win7 x86 帧、长度、散列和退出码自检；启动器在宿主长测未结束时已验证会拒绝启动。宿主八小时测试形成终态且所有 x86/x64 QEMU 退出后，先运行[启动器](harness/launch-title.ps1)。它使用 `-snapshot`、只读 FAT 传输盘和无网卡，不写回基础镜像。确认 VNC 或监控截图是 Win7 桌面后，再运行[一次性命令发送器](harness/run-title-keys.ps1)。客体经 COM1 导出系统身份、EXE 哈希、退出码和原始日志；用[串口校验器](Verify-SerialExport.ps1)核对文件帧/散列、Win7 SP1 x86、冻结 EXE 和测试结果。失败时保存原始数据并以新输入记录复测。

本短测须与公共任务、preview.5 混合、剪贴板及[旧窗口候选](../win7-sp1-x86-qemu-desktop-2026-09-25/README.md)串行。它不覆盖真实页面按钮、物理设备、受限账户、多窗口、DPI/IME、窗口关闭响应或长期资源回收。
