# preview.9 公共托盘：双架构契约、安装消费与宿主短测

日期：2026-09-26。版本：`0.3.0-preview.9`。来源基线 `fcdc070b087e5e3e60e551f64cb51e235b21f1a0` 加本切片变更；编译输入 SHA-256：`9cc0e1046693c01d20f6256c1d2bfe06a4699be2725a1f519ab24e3d86476c78`。完整输入在 [source-inputs.json](raw/source-inputs.json)，结果及安装文件/消费者/测试 EXE 身份在 [summary.json](raw/summary.json)。开发机为 Windows 11 Pro `10.0.26200` x64，MSVC v143、Windows SDK `10.0.19041.0`。

## 本轮证明的范围

| 验证 | 实际结果 |
|---|---|
| SDK 独立构建、安装 | x86/x64 Release，`tools/Build.ps1 -SdkOnly -Install` 均退出 0；两架构各 33 个安装文件，库散列与各自构建记录一致，未安装私有平台头 |
| 无桌面原生契约 | 每架构各 5 项 CTest 通过：SDK、任务服务、Bridge 预算、任务表容量和托盘；托盘包含 14 组失败/边界契约，不创建 HWND 或调用 Shell |
| 回归有效性 | 在仓库外复制当前托盘实现，仅还原重建通知时清空 `cleanup_pending` 的旧写法；相同测试退出 1，明确报告丢失旧注册。正确实现的两架构测试通过；反例来源与日志保留在 [negative-cleanup-variant](raw/negative-cleanup-variant/result.json) |
| 安装后独立消费者 | 源码复制到仓库外，只从安装 SDK `find_package(SouiSdk)` 并链接 `Soui::Sdk`；x86/x64 Release 均编译成功，PE 架构分别为 `0x014C` / `0x8664`。无效参数在创建窗口前退出 2 |
| 窗口线程停滞 | 当前新输入的 x86/x64 `soui_desktop_unicode_tests.exe --window-thread-timeout` 串行退出 0；见各架构 `host-*-window-timeout.*` |
| 公共目录取消 | x86/x64 `soui_directory_dialog_tests.exe --cancel` 串行退出 0，输出 `public directory dialog cancellation passed` |
| 真实 Shell 托盘短测 | x86/x64 独立消费者 `--smoke-test` 均退出 0；各保留 12 个顺序事件，安装、中文提示更新、移除、服务析构、窗口销毁、类注销和正常退出完整。实际鼠标通知数和 Explorer 重建通知数均为 0 |
| Web 与交付工具 | Web SDK 构建及 35 项测试通过；SDK 包行为 14 项、身份绑定 7 项通过；五个已有 npm 锁文件审计均为 0 漏洞 |
| SDK 源包 | 411 文件，包内校验及当前源码集合/散列匹配通过；这不是完整应用/运行包或生产批准 |

编译前后重新计算输入身份，保持不变。Shell/窗口/目录短测在重新确认其他 QEMU、SouiAcceptance 和应用进程均已退出后串行执行；每次启动前后进程快照、原始退出码、超时标志和 EXE SHA-256 都在对应 `host-*.result.json`。这些前后观察不是持续外部负载监控，本轮不生成性能或资源隔离结论。

## 原样保留的失败

- 第一次 SDK 源包发布目录改名返回 `EPERM`，记录在 [attempt1](raw/sdk-package-attempt1-failure.json)。未改源码的第二次打包在新目标目录成功并通过包内和源码匹配检查；占用根因未确定，不能把重试通过当作该工具问题已修复。
- 首次依赖审计误指向没有锁文件的 `tests/acceptance-app/web`，原 `ENOLOCK` 结果保留；随后针对实际 `tests/acceptance/package-lock.json` 执行成功。五项审计仅统计真实锁文件，未把错误路径结果当作通过。
- `raw/negative-cleanup-variant/` 是有意恢复缺陷的反例，不是 SDK 实现，不能用于分发编译。

## 重现入口

从交付根为每个架构执行 SDK `tools/Build.ps1 -SdkOnly -Architecture <x86|x64> -Configuration Release -Install`。无桌面测试使用根 CMake，设置 `SOUI_BUILD_TEMPLATE=OFF`、`SOUI_BUILD_DEMOS=OFF`，再以 Release 构建并执行：

```powershell
ctest --test-dir out/tray-tests-x64 -C Release --output-on-failure -R '^sdk.native.(contract|task-service|bridge-budget|task-store-capacity|tray-service)$'
```

各次完整配置、构建与执行参数和退出码保存在 `raw/*result.json`；独立安装消费者步骤见 [tests/tray-consumer](../../../tests/tray-consumer/README.md)。原始文件 SHA-256 清单见 [raw-manifest.json](raw-manifest.json)。这里不复制安装库/EXE 本身，只保留身份，复测必须重新构建并登记新载荷。

## 尚未证明

图标实际可见、中文提示显示、鼠标/键盘激活、Esc 后实际焦点、真实 Explorer 重启、Shell 重入、真实 message-only 窗口拒绝与 Win7 SP1 x86 尚未执行。模拟平台的行为不能替代系统验收。短测没有打开菜单，不覆盖 `return_focus()` 的真实交互效果。Debug、完整应用构建/启动、全部生命周期与长稳、V02 IME/缩放/任务并行、公共菜单/快捷键、Workbench 驻留、长会话续接及 Electron 对照继续待完成。

`sourceDeliveryVerified=false`、`benchmarkValid=false`、`releaseReady=false`。这一切片不关闭完整 V04 或三项最终准入结论。
