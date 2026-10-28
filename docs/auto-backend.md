# 自动选择桌面后端

新增的 `auto` 是显式可选模式。已有 compatibility 与 modern 项目的默认值、
后端和启动行为保持不变。可选 `Soui::AutoHost` 复用公共 Host/Bridge，基础 SDK
不因此依赖 WebView2。设计和边界见 [ADR 0127](adr/0127-automatic-renderer-host.md)。

## 启动策略

Windows 10/11 常规桌面先通过实际 Loader 探测 WebView2 Runtime，再准备 Controller。
系统不符合现代路径或明确不存在可用 Runtime 时，auto 选择随包 MiniBlink。
要求现代 Web 能力时不可降级。配置、权限、页面错误及异步准备失败保留错误，
不会自动重放或改换后端；业务页面挂载后，本次宿主保持选择不变。

选中 WebView2 后，宿主先只读检查用户数据路径：已存在的叶子或父级必须为目录，
系统明确返回的非法路径或查询错误会直接报告，不创建 Runtime、不降级。
尚不存在的目录仍允许由 Runtime 创建；预检不代替实际读写权限和异步初始化结果。
启动完成回调若抛异常，宿主保留 `E_CALLBACK`、拒绝运行操作和就绪状态，并保留清理所有权。

自动模式不下载或安装 Runtime。指定 Fixed Runtime 时路径必须是现有绝对目录；
空目录表示该指定位置不存在 Runtime，可以触发 auto 的兼容选择。这个条件不等同
整台机器未安装 WebView2。环境或注册表覆盖仍按现有 WebView2 准入契约检查。

## 构建、生成与验证

从完整源码根运行：

```powershell
powershell.exe -File tools/Build.ps1 -Architecture x64 -WithAutoHost
powershell.exe -File tools/Test-Auto.ps1 -Architecture x64
powershell.exe -File tools/Package-Applications.ps1 -Architecture x64 -Profile auto
powershell.exe -File tools/New-Project.ps1 -Name MyAutoApp -OutputRoot ..\MyAutoApp -Profile auto
```

x86 使用相同入口与 `-Architecture x86`。生成项目通过自己的 `tools/Build.ps1`
读取 `project.json` 的 auto 配置。外部项目的验证工具可以显式指定完整源码根：

```powershell
powershell.exe -File tools/Test-Auto.ps1 -Architecture x64 -DevkitRoot ..\soui-sdk
```

本轮自动启动验证入口使用 MSVC143。先完成所选架构的实际应用构建和测量工具准备；
需要时使用 `tools/internal/Build-ValidationTools.ps1 -Architecture x64`（x86 同理）。
验证不自动替换准备记录或借用旧构建。
默认 ZIP 和显式压缩档位沿用原有打包行为。

## 页面、资源和离线依赖

auto 模板共享业务源与原生动作协议，分别生成现代与 Chrome 49 兼容目标。
部署到 `resources/web-modern` 与 `resources/web-compat`，各自保留构建/资源清单。
公共文件只在明确声明的目标内复制。不能把仅现代内核可用的页面原样声明为兼容。

发行包包含 MiniBlink、所需本地依赖和两份页面资源；WebView2 为系统或调用方
配置的外部依赖。省略兼容内核或兼容资源的包不得声明具备离线自动降级能力。

## 验证记录

`Test-Auto.ps1` 串行运行同一构建的两条真实路径：系统 Runtime 的 WebView2 启动，
以及指定现有空 Fixed Runtime 目录后的 MiniBlink 降级。两条路径均要求真实页面
业务往返、就绪、Host 清理和进程组自然退出；绑定构建 ID、源码与全部载荷散列。
已成功启动同一构建的两个后端才可通过 auto 应用打包门槛。

诊断记录保留具体选择、真实 Runtime 探测结果和退出状态。此验证不代表整机卸载
Runtime、Win7/PE 实机、高刷、长期稳定或生产准入通过；执行结果另存本轮证据目录。

已完成的双架构、仓库外项目及 ZIP 解压启动结果见
[2026-10-23 验证报告](../verification/production-validation/auto-host-2026-10-23/README.md)。
