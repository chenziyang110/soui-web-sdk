# 版本与升级

0.4.0-preview.2 整合两条功能分支，SDK 内容发生变化，因此使用新版本。已发布的 v0.4.0-preview.1 标签和固定包不覆盖；整合验证不继承旧版完整交付通过状态。

0.4.0-preview.3 更新 Workbench 依赖和 SDK 内的依赖清单、授权材料，因此再次使用新身份。Workbench 构建要求 Node.js 22.12 以上；基础 SDK 和兼容生成项目仍保留 Node.js 20.19 范围。详见 [ADR-0018](adr/0018-workbench-build-node22.md)。

版本入口是根目录 `VERSION`，当前开发版本为 `0.4.0-preview.3`。构建工具检查 C++ 版本字符串、Web 包和锁文件保持一致，CMake 从该版本推导数值版本。生成项目以 `sdk-lock.json` 确认 SDK 版本，升级不覆盖业务项目自己的 VERSION。C++ API 使用源码编译，不承诺不同编译器、工具集或配置之间的稳定二进制 ABI。

`tools/New-Project.ps1` 把模板与固定 SDK 包组合，生成 `project.json` 和 `sdk-lock.json`。默认 SDK 包位于 `dist/sdk/<版本>/`，包含 SDK 源码、接入工具和应用必需的固定依赖；完整底层源码保留在完整交付包中。

安装器校验路径、大小和 SHA-256，拒绝越界路径、链接目录、文件冲突、本地修改过的 SDK，以及同版本不同内容。业务源码不在 SDK 所有权清单内。preview.13 为进程中断增加持久事务和恢复入口；冲突时保留现场并输出位置，不能将其忽略后继续安装。

## 升级

1. 修改 SDK 后更新 VERSION，记录兼容性变化并完成验证。
2. 按当前 VERSION 生成新包，例如 `node tools/sdk/soui-sdk.cjs pack --source . --output dist/sdk/0.4.0-preview.3 --version 0.4.0-preview.3`；开发中应在验证收敛后生成，已生成的同版本包不允许覆盖。
3. 在业务项目中提交或备份修改。
4. 执行 `node tools/sdk/soui-sdk.cjs install --package <新SDK包目录> --project <业务项目目录>`。
5. 重新编译、回归业务，保留旧包用于回退。

默认生成器检查缓存包与当前 SDK 源码是否相符。回退通过重新安装旧版本包后重新编译完成。模板升级不自动覆盖业务文件，按变更说明手动迁移。

## preview.13 安装中断恢复

停止目标项目的构建、运行和其他写入。从完整的新 SDK 交付目录调用新安装器（回退后项目内的安装器也会恢复旧版）：

```powershell
node tools/sdk/soui-sdk.cjs recover --project <业务项目目录>
node tools/sdk/soui-sdk.cjs verify-project --project <业务项目目录>
```

`recover` 仅收敛遗留事务，`install` 会先恢复再开始指定升级。提交点之前恢复旧版本，提交点之后要求新锁和全部新文件一致才能清理。恢复自身中断时可再次执行；本地修改、损坏备份、未知锁、仍存活的持有者或其他主机的持有者均失败并保留 `.soui-sdk-transaction`，禁止直接删除事务目录。PID 被复用也按占用处理；确定没有持有者之前不能清锁。

旧 `.sdk-install-*` 不包含足够的事务记录，新工具会拒绝猜测恢复；保留原项目、备份和升级前后 SDK 包，在独立副本按散列核对恢复，不能声称支持自动迁移。中断可能留下 `.soui-sdk-prepare-*` 或 `.soui-sdk-retired-*`；这些准备/退休目录不参与新的 SDK 身份，不能据此忽略固定活动事务目录。新项目忽略这些内部目录，已有项目可补齐相同 Git 忽略规则。

此契约验证进程终止，不宣称覆盖断电、磁盘损坏、网络共享或外部同时改写。完整设计见 [ADR-0015](adr/0015-restartable-sdk-installation.md)。

## preview.13 → preview.15

完整升级并重新构建，使用带就绪任务等待补偿的双架构新内核。持续压力下跨优先级执行次序可能变化，具体契约见 [ADR-0016](adr/0016-scheduler-ready-task-fairness.md)。SDK Core 不再携带两份历史 `node.dll.before-gpu-lcd`；它们仍在完整源码和旧包中保留。安装器按旧锁退役受管备份，回退旧包时恢复，业务文件不受影响。不要手工删除历史现场，或用新 DLL 覆盖既有同版本包。新候选仍须完成应用、Win7、资源和真实交互验收。

原 `soui-miniblink-ai-devkit` 0.1 系列使用另一 SDK 身份与目录，不能直接混装。迁移到 0.2 应创建新模板工程，再迁入页面、资源和业务方法。

## preview.9 → preview.10 的动作和界面语义

完整重编译 SDK 和应用。`register_action` 是新的受权同步方法入口，要求公布的有界schema子集；输入在原生处理器之前校验，成功结果也必须匹配。`bridge.describe` 的方法项可包含可选 `action`，旧方法不受影响，新Web客户端保留并校验该信息。原生 `set_session_capabilities` 在首个bootstrap前配置，默认保持原授予行为；不能通过网页字段设置权限。

新模板在业务就绪前核对动作契约、界面语义调用和原生非法参数拒绝，因此不能直接回退到只有preview.9 SDK的库。已有使用旧API的业务项目仍可按固定包升级/回退；新功能的迁移由业务项目自己负责。WebView2仍作为独立实验随完整源码提供，未加入默认SDK安装包。Demo移除Tailwind构建插件并保留静态CSS，新增样式使用明确CSS，不再依赖自动生成Tailwind工具类。

接口和已验证范围见 [ADR-0014](adr/0014-action-contracts-and-gui-semantics.md) 与本版发布说明。

## preview.8 → preview.9 的能力发现与适配器

完整重编译 C++ SDK 与应用；自定义适配器可改为包含 `soui/host/webview_host.h`，原包含路径仍兼容。控制操作通过虚接口真实执行，未实现操作返回 `E_NOT_SUPPORTED`，不再得到测试适配器的默认成功。销毁仍保持幂等；失败卸载保留可重试状态。

Web `describe()`/`discover()` 需要原生 `bridge.describe` 支持。连接本身不自动发现，因此旧应用连接行为保持；新模板依赖 preview.9 的发现结果检查所需服务，不能仅替换 Web 文件而沿用旧原生 SDK。旧宿主错误显式传播，不自动猜测方法清单。注册方法名现在与已有调用规则一致，权限标识限制为1–512 UTF-8字节；检查注册返回值。

本版不更换预编译引擎，不移除历史版本验收。旧 `acceptance.json` 中的版本和 scope 仍只说明对应历史验证；preview.9 的本轮结果单独记录，正式汇总和完整源码打包须重新核验当前内容。

## 0.2 → 0.3 基础切片

SDK 安装格式仍为 v1，旧包的校验/安装/回退继续保留。新包增加可选 `sourceNormalization`：源码打包及 `verify-source` 按此规则消除文本换行差异，包和安装后 SDK 仍按实际字节锁定。新生成项目的 `.gitattributes` 使 SDK 受管文件不再被 Git 自动改写；已有业务项目应为锁清单内文件设置 `-text` 后再提交，不能手工修改锁中散列。

升级前将产品数据迁出运行目录的 `resources/`；它现在由构建工具整体替换。首次升级后完整重建，旧 v1 build/smoke 记录不能授权新包发布。源代码、底层 DLL 和既有公开 Bridge API 未通过此切片更换；异步通信、桌面模块和多窗口的迁移说明随后续工作包提供。

## preview.2 → preview.3 的运行时退出

更新 SDK 后完整重编译。业务应用的最终退出路径在所有 Host/窗口销毁之后、应用 COM 清理之前调用 `shutdown_miniblink_runtime()`，并处理失败结果。已有业务 `app/` 不由 SDK 安装器自动修改；参考当前模板的退出顺序手动迁移。不要在关闭某个子窗口时调用此函数；仅关闭窗口使用 Host 的 `destroy()`。内核最终关闭后不能在同一进程重新启动，进程内重建必须发生在最终退出之前。

## preview.3 → preview.4 的固定内核

preview.4 同步替换 x86/x64 固定 Miniblink DLL，使 HTTPS 连接同时校验受信证书链与主机名；依赖此前错误接受无效证书的页面须修正服务器证书或受信配置。安装和回退都应使用整套固定 SDK 包及项目锁，不局部覆盖单个 DLL。旧 preview.3 包不适合要求受信 HTTPS 的产品；回退决策必须明示这一差异。两版都仍处于预览验收，具体缺口见[中期准入判定](acceptance/interim-decision.md)。

## preview.5 → preview.6 的声明材料交付

preview.6 将现有第三方声明和 `third_party/licenses/` 放入 CMake 安装目录及应用的 `resources/legal/`，并将它们纳入构建身份；许可原文在源码 SDK 打包时保持原始字节。升级项目须完整重编译并重新分发运行目录，不能沿用 preview.5 的同名运行包。此变更只补足当前材料的交付路径，完整链接组件和再分发审查仍未通过，`releaseReady=false`。

## preview.6 → preview.7 的源码重建修复

preview.7 给五个第三方 UTF-8 文件增加 BOM，并使依赖构建在 C4819/C4828 编码警告时失败。公共 SDK API 与固定的预编译 Miniblink DLL 未因这次修复改变；完整源码包和 SDK 包的身份已经变化，生成项目须使用 preview.7 新包及新锁定文件，不能沿用 preview.6 的同版本缓存。源码重编和目标系统仍按[验收记录](acceptance/interim-decision.md)逐项判定，`releaseReady=false`。
