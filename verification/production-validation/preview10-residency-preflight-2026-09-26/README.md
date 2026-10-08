# preview.10 公共菜单、快捷键与 Workbench 驻留预检

日期：2026-09-26。版本：`0.3.0-preview.10`。来源为 preview.9 提交 `385c04f87e977cb22970e0d8d6f838eaec612b6e` 加本切片；704 份编译输入 SHA-256 为 `934ea2a4229ebdd2e3f6a01762312856be99e8fca86530753e27b1017540069b`，见 [source-inputs.json](raw/source-inputs.json)。构建、安装、测试 EXE 及应用载荷身份见 [summary.json](raw/summary.json)。本记录未运行本版真实菜单、快捷键、驻留场景或 Win7 客体。

## 实现和已执行结果

新增公共 `menu_service`、`global_shortcut`，独立消费者仅依赖安装后的 `Soui::Sdk`。Workbench `--resident` 控制器管理三个独立 Host/adapter、空窗驻留及最终退出，按业务窗口消息栈和代际延迟释放；日志在运行载荷目录之外独占新建。设计见 [ADR-0013](../../../docs/adr/0013-public-menu-shortcut-and-residency.md)。这些路径的真实行为仍待桌面验收。

| 已执行项 | 结果与范围 |
|---|---|
| x86/x64 Release 编译 | SDK、Starter、Workbench 及原生测试目标成功；MSVC v143、Windows SDK 10.0.19041.0。未重编底层全部第三方内核，不是无 Git 完整源码复现结论 |
| 独立 SDK 构建和安装 | 两架构 SDK-only 成功，各安装 35 文件；源码、构建 ID、库与应用载荷复核一致 |
| 每架构 8 项 CTest | SDK contract、task-service、bridge-budget、task-store-capacity、tray-service、menu_service、global_shortcut、Demo public-task 全通过；其中菜单15组、快捷键9组、托盘14组为 fake platform 契约 |
| 仓库外独立消费者 | 两个独立源码副本分别从安装目录构建成功；PE 为 014C/8664；只运行 `--invalid`，均按预期退出 2，未调用真实服务 |
| Web 和交付回归 | SDK/Starter/Workbench Web 构建通过；Web 35 项、包测试 14 项、交付身份 7 项通过；5 个 npm 锁文件全依赖审计为 0 已知漏洞 |
| SDK 源包 | 第二个新目录打包 417 文件，完整性和当前源码匹配校验通过；首次失败保留 |

测试筛选排除 desktop-unicode、目录对话框和所有桌面 smoke。Demo public-task 在创建 SOUI 应用、业务窗口和 WebView 前返回，但入口仍初始化 COM STA，不能描述为系统绝无隐藏窗口。模拟成功不证明 Windows 实际显示、注册、派发或清理成功。编译保留 SOUI 第三方头文件既有 `/W4` 警告，未声称零警告。

## 打包失败

首次暂存包校验完成后，发布目录 rename 返回 `EPERM`，原退出码为 1，见 [原日志](raw/sdk-pack.log) 和 [退出记录](raw/sdk-pack.result.json)。现有发布逻辑有最多6次、合计约450ms的等待重试；日志未记录锁持有者或逐次原生错误，`finally` 已清理失败暂存目录。没有修改源码、权限或杀毒设置，新目标目录[第二次打包](raw/sdk-pack-attempt2.log)成功。

占用/权限根因未确证。当前权限、剩余空间、索引进程存在及重试成功都不能证明瞬时原因，也不能关闭这项重复出现的问题。

## 复现和原始记录

从交付根目录按架构串行执行，每轮使用新的仓库外安装和消费者目录：

```powershell
powershell.exe -NoProfile -File tools/Build.ps1 -Architecture x86 -Configuration Release
powershell.exe -NoProfile -File tools/Build.ps1 -SdkOnly -Architecture x86 -Configuration Release
ctest --test-dir out/x86 -C Release --output-on-failure -R '^(sdk.native.(contract|task-service|bridge-budget|task-store-capacity|tray-service|menu_service|global_shortcut)|demo.native.public-task)$'
```

x64 替换架构与输出目录。安装后接入见[消费者说明](../../../tests/tray-consumer/README.md)。实际参数、退出码、构建/Web 日志和 JUnit 位于 `raw/x86-attempt1`、`raw/x64-attempt1`；[当次脚本](raw/run-build.ps1)中的绝对路径仅代表历史来源。归档文件散列见 [raw-manifest.json](raw-manifest.json)。

汇总器另经独立复核：精确核对测试名称、运行/跳过状态、日志中的数量、归档与当前构建身份，以及417项包和每架构35项安装文件。11个拒绝负例及正式汇总均退出0，见[校验退出码](raw/collector-results.json)。早期仅检查部分总数的汇总器与派生摘要保留为 `initial-*`；当前结论使用 `collect-summary.cjs` / `summary.json`，原始日志未修改。

## 未执行与准入边界

[门禁记录](raw/desktop-gate.json)确认历史八小时测试已有终态、其他 QEMU 仍运行。本轮未启动桌面或客体，未终止其他虚拟机；历史八小时受外部负载干扰，仍不能作隔离资源或时延结论。

待执行：实际菜单选择/取消/焦点、快捷键冲突与按键、Explorer 重建；Workbench 三窗任务隔离、拖拽及模态菜单期间关闭、空窗驻留和退出；Win7 SP1 x86；V02 IME、缩放、真实滚动与公共任务并行。还缺 Debug、当前版无 Git 源码复现、隔离长稳及 Electron 完整价值对照。旧 preview 的冻结结果不迁移成本版通过。

`sourceDeliveryVerified=false`、`benchmarkValid=false`、`releaseReady=false`。完整 V04 和三项最终准入结论未关闭。
