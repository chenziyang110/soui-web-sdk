仅准备任务 VM 的托盘恢复短测；本目录脚本尚未由作者执行。六个控制文件和 data finalizer 已向 Root 报告冻结哈希，未经沟通不再修改。Root 独占 VM、介质和产品执行。C++ 三个冻结文件保持不变；现有托盘、菜单、焦点用例不重跑。

本次实际两架构构建已由 Root 完成：`tray-recovery-current/attempt-37db1a86-0b86-46bc-87bf-6de1a7b63b47/summary.json`，17246B，SHA256 `f6cece91cf2f92ea583dd7958ee6e61f1852f5c15ac86257f710cdc44bc3a430`。x64 新 EXE100352B SHA256 `981f90a1437c77ed9e0c88247c6f74992d5979d40f62c505fd4cc0cd2b6a7344`；x86 新 EXE86528B SHA256 `5b716498d853ba14dfa550922b121fe4fdab76cb98e7fce96fec0072af4e1d42`。构建通过不代表恢复行为通过。

Root 审查后可执行以下纯数据 finalizer；它只核对实际构建记录、复制两个新 EXE、生成 manifest 和显式 ISO 文件选择，不启动构建、产品、ISO 工具或 VM：

```powershell
node 'F:/work/SOUI_WEB/out/open-issues-closeout-20261029/tray-recovery-vm-current/Finalize-Payload.cjs' 'F:/work/SOUI_WEB/out/open-issues-closeout-20261029/tray-recovery-current/attempt-37db1a86-0b86-46bc-87bf-6de1a7b63b47/summary.json' f6cece91cf2f92ea583dd7958ee6e61f1852f5c15ac86257f710cdc44bc3a430 ROOT_REVIEWED_TRAY_RECOVERY_DATA_FINALIZER_V1
```

介质只含 `ControlIso.selection.actual.json` 的九个平面文件：六个控制文件、两个 EXE、manifest。Root 创建并挂载到本任务命名 VM 的只读 `D:`。工具复用 `C:\issues8\tools` 四个固定文件，串口 exporter 复用原 `C:\issues8\Guest-Export.ps1`。阶段复制29个 CRT DLL（x64为15个、x86为14个）及两个新 EXE到全新 `C:\trayrc-a1\runtime-{arch}`，排除旧 EXE；不重建 SDK、内核或 Web。原 R1 manifest285033B SHA256 `c6bb34afb81252ca16dd2bec955e3a8b36bfe830041e52ba417e9c34445eb897` 提供工具和 CRT 复用身份。

以下命令只供 Root 在本任务 VM 内执行。`MANIFEST_SHA` 使用 finalizer 生成的实际 manifest SHA，`STAGE_SHA` 使用已独立审阅的实际 `Stage.actual.json` SHA。每个证据目录必须首次使用，已有文件一律保留。

```powershell
powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -File D:\Stage-TrayRecovery.ps1 MANIFEST_SHA
powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -File D:\Export-TrayRecovery.ps1 stage MANIFEST_SHA
```

导出并审阅 `C:\issue-closeout-evidence\trayrc-a1-stage` 后，先把 Task Manager 的 Explorer 重启操作准备到位，再启动一个架构。启动及全部 UI 阶段共用消费者原始90秒；Owned 保持100000ms+2000ms drain：

```powershell
powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -File D:\Run-TrayRecovery.ps1 x64 MANIFEST_SHA STAGE_SHA ROOT_REVIEWED_TRAY_RECOVERY_STAGE_V1
```

Root 观察消费者实际窗口就绪，使用 Task Manager UI 真实重启 Explorer 第一次。程序确认旧进程退出和新 Shell 身份、公共 SDK 恢复成功后才启用“移除图标”。Root 点击该按钮，待实际移除确认后再使用 Task Manager UI 重启第二次。程序确认未复活并完成固定500ms观察后，Root点击“结束验证”。控制文件既不重启 Explorer，也不注入广播或按钮消息。不要以 Owned ready 记录代替窗口就绪。

等待原始 Owned/native 及 PowerShell、Node、wrapper 全部结束后，单独导出该次完整证据；失败同样保留。不得在 x64 失败后自动运行 x86。x86 必须由 Root 明确另发同一格式命令，将 `x64` 替换为 `x86`。

```powershell
powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -File D:\Export-TrayRecovery.ps1 x64 MANIFEST_SHA
```

证据命名固定为 `trayrc-a1-stage`、`trayrc-a1-x64`、`trayrc-a1-x86`。导出先验证实际 lease/wrapper PID出生时间身份已结束；顶层 Node PID 从实际 `Inputs.actual.json` 记录，必须现场查无该 PID。未采集 Node 出生时间，因此绝不假设 PID 重用或凭未记录 PID 声称关闭。运行证据另验证 native PID出生时间、完整 Owned 上下文和 Job 已关闭。传输不把失败升级为通过。

导出复用原始 Base64 JSON/串口协议，完整目录上限8MiB/512文件，前后散列和成员闭合检查不变，绝不截断。宿主离线解码命令只复制一个明确 tag 的完整帧：

```powershell
node 'F:/work/SOUI_WEB/out/open-issues-closeout-20261029/tray-recovery-vm-current/Decode-TrayRecoveryExport.cjs' SERIAL_FILE ISSUESHORT_SELECTED_TAG 'F:/work/SOUI_WEB/out/open-issues-closeout-20261029/current-decoded/trayrc-a1-x64'
```

decoder 相对 `Decode-FocusExport.cjs` 只改允许的证据命名正则；原 Base64、路径、SHA、EOF、唯一完整帧和字节上限均保持。所需 native NDJSON最多128行，验证同一 PID/出生时间/runId、阶段顺序、两次真实进程身份变化、移除恰好一次、500ms观察、native0/Job0/自然关闭、无强停和完整清理。`sourceVerified:false` 与外部重启证据要求始终保留；完整 Explorer UI 操作仍由 Root 独立记录并组合审查。

本次不能单独关闭焦点、Win7、完整 Issue 或生产资格。所有相关通过标记以实际短测和独立审查为准，长测继续后置。
