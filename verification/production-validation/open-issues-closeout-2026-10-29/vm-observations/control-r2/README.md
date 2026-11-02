# r2 resident 日志边界与通用 launcher 候选

仅 output-only 准备。未构建、运行产品、操作 VM 或 Git，未编辑旧 payload、原控制器或运行证据。

`payload/` 仅含需替换的两个控制文件。Root 在 VM 中以旧 `C:/issues8` 完整复制出新的 `C:/issues9`，再替换 `Guest-Shorts.cjs` 和 `Payload.manifest.json`；其余 36 个载荷行以及两个 resident 各 671 个不可变文件的原 SHA/bytes 不变。`Guest-Invoke.ps1` 放在 payload 外，例如 `C:/issue-control/`，不加入/放宽 payload 闭集。

准确可变集合只有 runtime 根 `webview-smoke.log`。当前 Workbench main.cpp:1066 和 SDK host:324 均使用输出根路径；ADR-0087 已删除生产 source-navigation writer，因此旧 `resources/source-navigation.log` 与错误的 `resources/webview-smoke.log` 不再作为排除。运行前后记录实际发现的全部 `.log` 的路径、bytes、SHA；超过 16 MiB 或非普通文件即失败，失败路径也尝试记录后态，不允许任意新文件。root/resource 下的 source-navigation 路径继续受完整闭集约束。

原 tray 190000ms、resident 200000ms、drain 2000ms、内部 ready 45000ms/smoke 180000ms，以及自然退出/Job 闭合谓词不变。原 Shell 焦点失败和 resident1 预检失败不改写。

launcher 接受显式 cases、fresh output、精确 manifest SHA 和实际客体机器名；使用同一个 `Local\SouiDevKit.DesktopValidation`。验证脚本自身、Node、控制器、完整 x64 CRT prefix 的闭集与散列后，只在当前进程清除 Node/SOUI 污染环境并前置已 pin 的 `tray-x64`，finally 验证恢复。保存环境的存在性/长度/散列，不输出原环境值；记录真实 native exit，PS5 native stderr 不触发虚假的提前退出。控制器仍要求原来的全新输出目录。

launcher 的租约、环境、stdout/stderr、真实退出放在 `<output>-launcher`，另有与既有 Guest-Export.ps1 兼容的独立 catalog；它与控制器的原 catalog 分开，不重写控制器结果。launcher stdout/stderr 是 PowerShell 文本重定向，产品 Owned/raw 文件仍由原控制器保存。

Root 独立核对客体名、复制和精确 SHA 后的调用形式（此处没有执行）：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File C:\issue-control\Guest-Invoke.ps1 -PayloadDirectory C:\issues9 -Cases resident-x64,resident-x86 -EvidenceDirectory C:\issue-closeout-evidence\resident-r2-UNIQUE -ManifestSha256 2f91cd8aa2a55e9b38e297bdd775ff376033adea8a131bca46ad01de7dea2efe -ExpectedMachine TASK_VM_NAME
```

`UNIQUE` 和 `TASK_VM_NAME` 必须由 Root 以本次全新身份及实际客体名替换。目录已存在即拒绝，不续写或覆盖。按原 Guest-Export.ps1 分别导出 controller 与 launcher 目录。

`Preparation.actual.json` 保留生成时 `independentReviewPending=true` 的真实状态；`ReviewerMessage.receipt.json` 只是本 agent 收到独立审查消息的记录，不冒充新的 reviewer 执行。审查者 `/root/issues_ui_fixture_audit` 已对精确三文件报告静态 0 blockers，范围不含产品/VM/PS5 运行。Node `--check` 与 PowerShell parser 均无语法错误，不能据此称功能通过。

`Find-ResidentTag.cjs` 与 `Prepare-ResidentDecode.cjs` 仅用于已发生 resident1 的串口离线提取。新目录 `../decoded-resident1/` 完整保存指定 tag `ISSUESHORTA7F54F3D84CB47B9BF24983D0396ED0F`：原 catalog+2 文件，合计 3479 字节，原 Summary 明确 selected 两 resident、outcomes 0、预检只多 `webview-smoke.log`；没有产品启动或 GUI 通过。

`releaseReady=false`、`productionQualificationComplete=false`、`fullDevtoolsGoalComplete=false`。
