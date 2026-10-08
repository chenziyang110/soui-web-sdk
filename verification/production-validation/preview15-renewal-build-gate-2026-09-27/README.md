# preview.15 真实续期构建门禁修正

这是外部验证工具的修正，没有修改 SDK、模板、Demo、固定包或已经冻结的客体。此前构建门禁只检查进程查询子命令的退出码与启动异常；Windows PowerShell 的非终止 CIM 错误可能留下错误输出却返回 0，进而错误允许编译。

先对实际 `Build-Sdk.cjs` 的进程查询和放行条件执行六项边界测试：三项失败，分别为查询报错、退出 0 但有错误输出、退出 0 但有异常标准输出。查询失败样本实际启动 Windows PowerShell 子进程并注入失败的 CIM 提供者，不启动编译器。修正增加 `$ErrorActionPreference='Stop'`、`Get-CimInstance -ErrorAction Stop`，并拒绝退出码、启动错误或任何非空输出异常；相同六项随后全部通过。空查询允许继续，观察到 QEMU、进程启动失败及查询异常均拒绝。

修正前驱动 SHA `bf467f3627d1bd4534cea88af4a9b18e3b40c91bdeb715c4893a9c4366137461`，修正后 SHA `b7749c586a8b32c27de4e8712238a6aac404faa77a6ef2ce6f586304a06935e5`。旧脚本、旧 v2 控制文件清单和失败结果保留；新的 v3 准备复核绑定修正后的驱动，其余既有控制文件保持原字节。

随后真实执行 `sdk-build-attempt2`：观察到外部 `qemu-system-x86_64.exe` PID 15620，实际驱动退出 2，状态为 `gate-rejected-no-compile`；没有产生 SDK 编译输出或启动应用。720 文件暂存输入、717 文件构建源码 SHA `b0d7cc7d52de3493084e0dec6813f92f9a90a1bbce2f75ad5a62532a6dd87d07` 与 419 文件 SDK 包身份不变。23 份原始记录见 `raw-manifest.json`。

本记录只证明构建门禁的上述拒绝行为。当前 SDK 两架构独立编译/安装、消费者编译与实际冻结绑定、30 秒 GUI 和 35 分钟真实续期仍未执行；`sdkCompiled=false`、`consumerCompiled=false`、`guiExecuted=false`、`realRenewalVerified=false`、`releaseReady=false`。待外部负载退出后使用新的命令记录继续，不覆盖第一次或第二次门禁拒绝。
