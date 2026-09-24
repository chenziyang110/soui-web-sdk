# 版本与升级

版本入口是根目录 `VERSION`，当前开发版本为 `0.3.0-preview.6`。构建工具检查 C++ 版本字符串、Web 包和锁文件保持一致，CMake 从该版本推导数值版本。生成项目以 `sdk-lock.json` 确认 SDK 版本，升级不覆盖业务项目自己的 VERSION。C++ API 使用源码编译，不承诺不同编译器、工具集或配置之间的稳定二进制 ABI。

`tools/New-Project.ps1` 把模板与固定 SDK 包组合，生成 `project.json` 和 `sdk-lock.json`。默认 SDK 包位于 `dist/sdk/<版本>/`，包含 SDK 源码、接入工具和应用必需的固定依赖；完整底层源码保留在完整交付包中。

安装器校验路径、大小和 SHA-256，拒绝越界路径、链接目录、文件冲突、本地修改过的 SDK，以及同版本不同内容。业务源码不在 SDK 所有权清单内。失败时回滚；极端文件系统错误时保留恢复目录并输出位置。

## 升级

1. 修改 SDK 后更新 VERSION，记录兼容性变化并完成验证。
2. 按当前 VERSION 生成新包，例如 `node tools/sdk/soui-sdk.cjs pack --source . --output dist/sdk/0.3.0-preview.6 --version 0.3.0-preview.6`；开发中应在验证收敛后生成，已生成的同版本包不允许覆盖。
3. 在业务项目中提交或备份修改。
4. 执行 `node tools/sdk/soui-sdk.cjs install --package <新SDK包目录> --project <业务项目目录>`。
5. 重新编译、回归业务，保留旧包用于回退。

默认生成器检查缓存包与当前 SDK 源码是否相符。回退通过重新安装旧版本包后重新编译完成。模板升级不自动覆盖业务文件，按变更说明手动迁移。

原 `soui-miniblink-ai-devkit` 0.1 系列使用另一 SDK 身份与目录，不能直接混装。迁移到 0.2 应创建新模板工程，再迁入页面、资源和业务方法。

## 0.2 → 0.3 基础切片

SDK 安装格式仍为 v1，旧包的校验/安装/回退继续保留。新包增加可选 `sourceNormalization`：源码打包及 `verify-source` 按此规则消除文本换行差异，包和安装后 SDK 仍按实际字节锁定。新生成项目的 `.gitattributes` 使 SDK 受管文件不再被 Git 自动改写；已有业务项目应为锁清单内文件设置 `-text` 后再提交，不能手工修改锁中散列。

升级前将产品数据迁出运行目录的 `resources/`；它现在由构建工具整体替换。首次升级后完整重建，旧 v1 build/smoke 记录不能授权新包发布。源代码、底层 DLL 和既有公开 Bridge API 未通过此切片更换；异步通信、桌面模块和多窗口的迁移说明随后续工作包提供。

## preview.2 → preview.3 的运行时退出

更新 SDK 后完整重编译。业务应用的最终退出路径在所有 Host/窗口销毁之后、应用 COM 清理之前调用 `shutdown_miniblink_runtime()`，并处理失败结果。已有业务 `app/` 不由 SDK 安装器自动修改；参考当前模板的退出顺序手动迁移。不要在关闭某个子窗口时调用此函数；仅关闭窗口使用 Host 的 `destroy()`。内核最终关闭后不能在同一进程重新启动，进程内重建必须发生在最终退出之前。

## preview.3 → preview.4 的固定内核

preview.4 同步替换 x86/x64 固定 Miniblink DLL，使 HTTPS 连接同时校验受信证书链与主机名；依赖此前错误接受无效证书的页面须修正服务器证书或受信配置。安装和回退都应使用整套固定 SDK 包及项目锁，不局部覆盖单个 DLL。旧 preview.3 包不适合要求受信 HTTPS 的产品；回退决策必须明示这一差异。两版都仍处于预览验收，具体缺口见[中期准入判定](acceptance/interim-decision.md)。

## preview.5 → preview.6 的声明材料交付

preview.6 将现有第三方声明和 `third_party/licenses/` 放入 CMake 安装目录及应用的 `resources/legal/`，并将它们纳入构建身份；许可原文在源码 SDK 打包时保持原始字节。升级项目须完整重编译并重新分发运行目录，不能沿用 preview.5 的同名运行包。此变更只补足当前材料的交付路径，完整链接组件和再分发审查仍未通过，`releaseReady=false`。
