# 显式运行时清单和去重包

本能力属于未发布 SDK 候选。未更改 C++ ABI、Web 后端、最低系统或已发布的
0.5.0 固定包。原应用 ZIP 仍是默认格式，展开后仍为独立应用目录。

## 运行时部署

tools/internal/runtime-files.json 按架构列出源相对路径、目标 DLL 名称、字节数、
SHA-256 和组件依赖。CMake 使用 runtime-deployment.cjs 验证并部署，不再以
目录通配决定交付集合。默认兼容集合包含原部署的全部 59 个 DLL；现代模板
保留原来的 CRT 集合。未经声明的新 DLL 不会因为放进依赖目录而自动进入应用。

| 组件 | 内容 | 依赖 |
|---|---|---|
| crt | 当前固定 VC/UCRT 运行文件 | 无 |
| soui-core | SOUI、Utilities | crt |
| soui-gdi | GDI 渲染与图片解码组件 | soui-core |
| soui-resources | SOUI 系统资源 | soui-core |
| miniblink | 固定内核 node.dll | crt |

未配置 runtimeComponents 的项目保留完整默认集合。自定义宿主已经确认不需要
GDI 或系统资源组件时，可在 project.json 中显式选择：

```json
{"runtimeComponents": ["soui-core", "miniblink"]}
```

工具会补齐必需组件及依赖闭包；兼容后端始终要求核心、CRT 与内核。现代
后端只允许 crt。标准 SOUI 模板需要默认组件，不能仅凭静态导入列表删除
动态加载的渲染器、图片解码器或系统资源。

每次部署生成根目录 .soui-runtime.json，记录真实架构、后端、组件、SDK 清单
身份和部署文件身份。构建和打包验证该记录及实际文件；显式声明组件但缺少
记录时不能通过构建。已有自定义项目没有记录和组件声明时仍可沿用原载荷方式。

第一次采用新工具时，CMake 显式开启旧输出迁移：仅刷新本次清单声明的 SDK
文件，未声明业务文件保留。之后仅更新原记录拥有且未被修改的文件；选择减少
的组件会退休相应旧文件。源或目标身份异常会拒绝，普通发布失败恢复旧文件。
完全相同且已经验证的部署不重写 DLL。进程被强行中断或恢复本身失败时，保留
备份并阻止将未知残留作为正常载荷；不要手工删除业务数据来消除错误。

依赖升级后应根据已审查的构建结果更新 runtime-files.json
身份，并以新的 SDK 版本交付，不能自动接受修改过的依赖。

## 可选去重应用包

从 SDK 仓库根目录执行：

```powershell
# 默认：原来的独立应用 ZIP。
powershell.exe -File tools/Package-Applications.ps1 -Architecture x64
# 可选：同一包内存储一次相同内容。
powershell.exe -File tools/Package-Applications.ps1 -Architecture x64 -Bundle
```

Bundle 生成 SouiApplications-<版本>-windows-<架构>-<档位>.bundle.zip。
已有实际构建和启动证据要求继续执行；各应用的体积预算也仍在打包前检查。
-Profile modern 与 -Profile auto 同样支持 Bundle，但 WebView2 Runtime 的准备方式不变。
应用和 Bundle 均可使用[可选压缩档位](compression-profiles.md)；省略
-CompressionProfile 时继续生成原 ZIP。

包内 manifest.json 使用 soui.runtime-bundle/v1，保存每个逻辑应用的原完整载荷
报告，blobs/<SHA-256> 存储内容块。相同字节只存一次，DLL 同名但内容不同
会保留不同块。相同资源和许可文件也可以复用内容块；这不表示它们的运行时
职责或 ABI 可以互换。

先将 Bundle ZIP 解压为独立目录，再用 SDK 工具恢复可运行布局：

```powershell
node tools/internal/runtime-bundle.cjs verify --bundle path/to/extracted-bundle
node tools/internal/runtime-bundle.cjs restore --bundle path/to/extracted-bundle --output path/to/new-applications
```

恢复目标必须不存在。工具验证完整包，在独立临时目录复制所有文件、逐项
读回校验后发布。结果是 <目标>/<应用名>/ 下各自完整的普通文件，不使用
共享硬链接；运行这些恢复后的应用不需要 Node.js。Node.js 用于 SDK 构建和
恢复工具；新格式尚未自动接入下游 NSIS 安装器或自包含卸载器。

业务可以封装多个已有的、已验证暂存应用。applications.json 是数组：

```json
[
  {"name": "Main", "root": "staged/main", "reportFile": "reports/main.json"},
  {"name": "Worker", "root": "staged/worker", "reportFile": "reports/worker.json"}
]
```

root 和 reportFile 可以相对于该配置文件；报告文件放在载荷目录外。每份报告
使用 soui.application-size/v1，绑定完整文件集合、应用、架构、后端及来源身份。

```powershell
node tools/internal/runtime-bundle.cjs pack --applications applications.json --output new-bundle
```

去重统计同时报告 logicalBytes、uniqueBytes 和 savedUncompressedBytes；最后一项
是未压缩内容差值。最终 ZIP 字节数及散列在归档旁报告，不能由未压缩差值
推算压缩收益。新格式的散列用于内容一致性，不替代代码签名或来源认证。

设计见 [ADR 0125](adr/0125-runtime-manifests-and-bundles.md)。
