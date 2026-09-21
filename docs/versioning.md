# 版本与升级

版本入口是根目录 `VERSION`，当前为 `0.2.0-preview.2`。C++ API 使用源码编译，不承诺不同编译器、工具集或配置之间的稳定二进制 ABI。

`tools/New-Project.ps1` 把模板与固定 SDK 包组合，生成 `project.json` 和 `sdk-lock.json`。默认 SDK 包位于 `dist/sdk/<版本>/`，包含 SDK 源码、接入工具和应用必需的固定依赖；完整底层源码保留在完整交付包中。

安装器校验路径、大小和 SHA-256，拒绝越界路径、链接目录、文件冲突、本地修改过的 SDK，以及同版本不同内容。业务源码不在 SDK 所有权清单内。失败时回滚；极端文件系统错误时保留恢复目录并输出位置。

## 升级

1. 修改 SDK 后更新 VERSION，记录兼容性变化并完成验证。
2. 执行 `node tools/sdk/soui-sdk.cjs pack --source . --output dist/sdk/0.2.0-preview.3 --version 0.2.0-preview.3`。
3. 在业务项目中提交或备份修改。
4. 执行 `node tools/sdk/soui-sdk.cjs install --package <新SDK包目录> --project <业务项目目录>`。
5. 重新编译、回归业务，保留旧包用于回退。

默认生成器检查缓存包与当前 SDK 源码是否相符。回退通过重新安装旧版本包后重新编译完成。模板升级不自动覆盖业务文件，按变更说明手动迁移。

原 `soui-miniblink-ai-devkit` 0.1 系列使用另一 SDK 身份与目录，不能直接混装。迁移到 0.2 应创建新模板工程，再迁入页面、资源和业务方法。
