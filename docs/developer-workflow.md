# 统一开发入口（首个有限切片）

在完整源码交付根运行 `powershell.exe -File tools/Soui.ps1 <command>`。原脚本继续保留；本入口只复用它们已有的功能，不改变 SDK 包身份或安装事务。当前候选尚未实际执行，05-09a 的完整验收仍未关闭。

## 参数与输出

命令是 `create`、`doctor`、`dev`、`build`、`test`、`package`、`upgrade` 之一。参数一律是独立的 `--name value`，布尔开关不带值。拒绝未知、重复选项、任意命令/额外 argv、控制字符、UNC/device 路径和通配符。路径用本机绝对路径；空格路径应按 PowerShell 规则整体加引号。参数通过结构化 argv 传入 `-File` 或直接 executable，从不拼成 shell 命令。

所有命令支持：

| 参数 | 默认与边界 |
|---|---|
| `--architecture` | `x64`；仅 `x86`、`x64` |
| `--configuration` | `Release`；仅 `Debug`、`Release` |
| `--format` | `human`；或 `json` |
| `--timeout-ms` | build/test 为 900000，其他为 120000；1000–86400000 |
| `--drain-ms` | 2000；0–60000，不改变失败接受规则 |
| `--devkit-root` | 完整源码工具包根；安装项目需显式指向已准备验证工具的完整根 |
| `--runner` | 可选，但必须精确等于所选 devkit/架构当前 prepared OwnedProcessRunner |

`--format json` 向 stdout 写逐行 `soui.devkit-event/v1`：`command`、`stage`、`status`、稳定 `code`、`fix` 和必要结果字段。完整 `soui.devkit-result/v1` 保存在新的 `out/devkit/attempt-<uuid>/result.json`。拒绝和失败返回非零；stderr/raw 是诊断资料。setup 阶段的 PowerShell 错误也输出同一事件 schema。JSON 调用者以最终事件和退出码共同判断结果。

常见错误：`E_ARGUMENT`/`E_PATH` 修正参数；`E_DESKTOP_BUSY` 等当前持有者自然结束；`E_VALIDATION_TOOLS` 在完整工具包中准备现有 `Build-ValidationTools.ps1`；`E_STAGE_FAILED` 检查本轮日志；`E_INPUT_CHANGED` 保留证据并核对源码；`E_WORKSPACE_CAPABILITY` 选择有该能力的交付根。

## 各命令

| 命令 | 参数与实际行为 |
|---|---|
| `create` | 必须 `--name` 和新的 `--output-root`；可选 `--sdk-package`、`--profile compatibility|modern`。调用真实 `New-Project.ps1`。仅完整源码根拥有模板。 |
| `doctor` | 调用真实 `Doctor.ps1`；环境/工具查询也由 owned Job 监督。Doctor 的 source presence 字段不代表目标系统兼容通过。 |
| `build` | 开关：`--sdk-only`、`--skip-web`、`--with-webview2`、`--with-gui-bindings`、`--install`、`--fresh-configure`。完整源码根复用真实 `standard-build.cjs`。安装项目调用其实际 Build.ps1 并复核现有 build-evidence，要求自然闭合。 |
| `test` | `--scope unit|native|contract|desktop`。源码根默认 unit，实际调用 Test.ps1 的 UnitOnly；安装项目默认 contract，实际 verify-project。native 复核当前 build、真实非空 CTest registry、自然 CTest 和无失败/错误/skip 的对应 JUnit。desktop 在源码根先监督 UnitOnly，再直接运行共用 Owned smoke helper；安装项目只运行本项目实际 smoke，不借用源码单元套件。 |
| `package` | `--kind source|sdk|applications`，默认 source。source 可给 `--output`；sdk 必须给新的 `--output`，可加 `--with-gui-bindings`；applications 保留现有固定归档路径。仅完整源码根，复用实际 Package/Package-Applications/SDK pack。 |
| `upgrade` | 必须 `--sdk-package` 和 `--project`。实际 SDK install 持久事务后 verify-project；不重放业务请求，不接受 force，同版本不同内容继续拒绝。不能原地替换正在运行的入口；从完整 devkit 对目标安装项目执行。 |
| `dev` | 可选 `--web-root`、`--port`（默认 5173，1024–65535）、`--duration-ms`（默认 900000，1000–3600000）。源码根默认实际 desktop template web，安装项目默认 web。 |

源码标准 build 保留已有显式 `--compiler-tail --tail-image <absolute> --tail-sha256 <reviewed>` 选项；精确政策由现有 caller 再核验。原始 generic false/124 和清理记录保持，不改为自然成功。安装项目在这个切片不接受 compiler-tail 特例。test、preview、环境查询和业务运行从不继承该政策。

示例（从对应工作空间根）：

```powershell
powershell.exe -File tools/Soui.ps1 doctor --format json
powershell.exe -File tools/Soui.ps1 create --name MyApp --output-root C:\Apps\MyApp
powershell.exe -File tools/Soui.ps1 build --architecture x64 --configuration Release
powershell.exe -File tools/Soui.ps1 test --scope native --format json
powershell.exe -File tools/Soui.ps1 dev --duration-ms 60000
powershell.exe -File tools/Soui.ps1 upgrade --sdk-package C:\SdkPackages\Reviewed --project C:\Apps\MyApp
```

## 真实预览与关闭

dev 从所选 web 根现有 node_modules 加载其实际 Vite 6.4.3/8.3.3 JavaScript entry，通过真实 createServer/listen 提供资源 watch/HMR。使用原 vite.config、固定 127.0.0.1 和 strictPort；不自动打开浏览器、不安装或升级依赖。使用冻结 package-lock 手工安装依赖后再运行；工具检查 package/lock/Vite 入口在本轮未漂移，不声称完整依赖树认证。

ready 事件给出实际 loopback URL。浏览器里的模板/组件可以显示和更新；普通浏览器没有原生 SOUI Bridge，因此业务连接拒绝不代表宿主已经启动。Vite 开发服务器面向当前开发者浏览器，不是 Chrome49 产物验收，也不要求 Win7 目标机安装 Node。现代 Vite profile 遵守模板的 Node>=22.12；兼容 profile 遵守当前 Doctor 支持的版本窗口。

worker 到 duration 预算后 await server.close，关闭文件 watcher、HTTP/WebSocket 后自然退出；父层必须观察到原生 Job 自然 0/0、active0、无强制关闭以及实际 preview-closed receipt，才接受。中断先给本轮 nonce 绑定的私有停止请求，2000ms 后才请求 owned Job 停止；中断仍是 `E_CANCELLED` 或失败，不能冒充正常预览成功。任何异常兜底只交给本轮 held supervisor/Job，不按 PID/image 搜索或清理外部进程。

## 交付与能力边界

SDK 的既有 managedFiles 增加 Soui.ps1、Doctor.ps1 和本说明；devkit-command.cjs 已属于 tools/internal managed root。Core SDK 不引入模板、Workbench、原生验证工具二进制或新的安装器。旧 SDK manifest/lock 仍按原格式读取，新增入口随新内容正常出包；同版本包不覆盖。

完整源码 ZIP 是完整 devkit 根，保留真实模板、现有测试与准备工具源码。生成的安装项目带统一入口，可以在显式选定完整 devkit prepared 工具后 doctor、build、contract test、已有 native tests 和 dev；没有真实 CTest registry 就拒绝 native。Core 中不存在 create/full-source/package应用的完整素材，这些能力给出阶段/修复建议，不能借 Demo 或模板假装存在。

桌面迁移候选保留 native-smoke/v2 的构建/源码/载荷字段、模板与 Workbench default/react 顺序、45 秒预算、application-ready 和 host-close-complete 的实际日志要求。新增应用 Owned 扩展绑定每个真实 Job 的 raw/context/finished 与执行参数；controller 的自然退出另由统一入口核验，不能代替应用退出。独立 Test.ps1 自己持有桌面租约，统一入口则直接选择 helper，避免重复取得租约。每次保存旧 smoke JSON 和应用旧日志，新原始样本放在独立 smoke-owned attempt 中，故障不会被重试改写。

源码 desktop 的 UnitOnly 当前仍调用 Build-ValidationTools；它可能试图重建正在使用的 runner。入口在该阶段前后核对 prepared runner 字节并保留失败，不绕过文件锁、不重标编译尾部、不自动选择其他版本。独立 Test.ps1 的前置单元命令未全部受 Owned 监督；本变化只迁移其桌面应用段。unit、native、desktop 和 session/52 分钟/8 小时仍有各自证据含义，不把 reader 测试通过当成实际长时运行。

当前未关闭：迁移候选的实际源码/安装项目 smoke、失败/忙租约/中断及旧 evidence 验收；独立 Test.ps1 前置单元命令的监督；Root 实际派发/JSON；Vite 实际资源修改与关闭两 profile 测试；出包/安装/搬移后入口实际运行。05-09a 仍未关闭，所有记录继续 qualificationPassed=false、releaseReady=false；没有安装运行、完整 DLL/GC/卸载、长期稳定性或 OS 支持结论。
