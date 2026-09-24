# Win7 SP1 x86 公共目录对话框取消短测准备

状态：**输入、客体脚本与校验器已冻结，真实来宾尚未执行**；`releaseReady=false`。源码来自提交 `6e900958`，源码清单、安装的 x86 SDK 静态库和测试 EXE 散列固定在 [input.json](input.json)。该可选场景由另一线程创建可见 HWND，STA 工作线程经公共 `json_bridge` 打开真实 Shell 目录对话框；测试观察到对话框后发送关闭，核对 `{cancelled:true,path:""}`，15 秒无终态则以失败退出。

只读来宾传输放在 `D:\SOUI-SDK-QEMU-validation\guest-directory-transfer-20260925`，包含 EXE、输入记录、[来宾命令](harness/run-directory.cmd)及串口导出脚本。传输内容与冻结输入逐项核对；四份 PowerShell 脚本语法解析为 0 错误，串口校验器已用合成 Win7 x86 帧/哈希/退出码自检。启动器在宿主长测未结束时已验证拒绝启动。宿主八小时测试形成终态、且全部 x86/x64 QEMU 退出后，运行[启动器](harness/launch-directory.ps1)；它使用 `-snapshot`、只读 FAT 传输盘与无网卡，不写回基础镜像。先通过 VNC 或监控截图确认 Win7 桌面，再运行[一次性命令发送器](harness/run-directory-keys.ps1)。随后由[串口校验器](Verify-SerialExport.ps1)核对帧、每份文件散列、Win7 SP1 x86 身份、测试 EXE 身份、退出码和实际输出。失败时保留原始结果并用新输入记录复测。

此场景只验证公共 JSON 方法的真实对话框**取消路径**，不能代表模板网页按钮、含中文路径的正向选择、网络位置、受限账户、导航/关窗竞态、Bridge 队列延迟、多屏或物理 Win7 设备通过。必须与其他 Win7 客体及宿主长测串行执行。
