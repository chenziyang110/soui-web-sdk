# Web 构建目标、资源清单和体积报告

已有项目不配置新字段时保持单一网页入口、原 npm build 脚本和 web/dist。
本能力属于未发布候选；已安装的 0.5.0 工具需要通过后续 SDK 版本取得。

## 声明独立目标

在生成项目的 project.json 保留原字段，并添加：

```json
{
  "webTargets": {
    "default": "main",
    "targets": [
      {"name": "main", "entry": "web/main/index.html", "publicAssets": ["assets/logo.png"]},
      {"name": "recovery", "entry": "web/recovery/index.html", "publicAssets": ["assets/logo.png", "assets/account-key.png"]}
    ]
  }
}
```

每个入口都叫 index.html。入口可共享 web/src 内组件，但要通过各自的模块
入口只导入需要的页面。例如 web/main/index.html 的模块路径可以是
../src/main.tsx；另一入口指向 ../src/recovery.tsx。构建工具不会自动拆开一个
同时导入全部业务页面的模块。

publicAssets 是相对于 web/public 的明确文件路径，发布后保留原相对路径。
未声明的公共图片不复制；静态 import 的资源仍由 Vite 收集。动态拼接 URL
使用的资源也必须声明。名称、路径、缺失文件、重复和保留路径冲突会被拒绝。

```powershell
# 从生成项目根目录构建所有 Web 目标；沿用已安装的 Web 依赖。
node tools/internal/web-targets.cjs build .
# 标准构建还会安装依赖、验证 SDK、编译原生部分并部署默认目标。
powershell.exe -File tools/Build.ps1 -Architecture x64 -Configuration Release
```

输出分别位于 web/dist/main、web/dist/recovery。默认目标部署到应用的
resources；其他目标独立保留，供业务的其他宿主或交付过程使用。兼容 CMake
消费者仍可显式指定 WEB_ROOT。所有目标都纳入标准构建证据；缺少非默认目标
也不能通过 SkipWeb。更换默认目标需要重新构建和部署。

每个目标的 .soui-web-target.json 记录目标、配置身份及全部产物文件身份。
不要手动修改输出或清单。诊断档仍通过 -WebProfile source-diagnostic 选择，
与业务目标无关；应用构建默认是 distribution。

## 查看与比较体积

应用打包保留原逐文件、分类和压缩报告，追加 sizeAnalysis：文件类型统计、
大文件、相同内容的重复文件，以及可选基线差异。重复潜力是未压缩字节，
不表示归档一定能减少同等大小。报告不会删除重复文件。

```powershell
# 使用已有应用报告；替换为自己的实际文件名。
node tools/internal/size-report.cjs --report dist/SouiStarter-0.5.0-windows-x64.zip.json
node tools/internal/size-report.cjs --report current.json --baseline previous.json --budget size-budget.json
```

预算文件可包含：

```json
{"totalBytes": 50000000, "growthBytes": 1000000}
```

totalBytes 限制未压缩载荷，growthBytes 限制相对于基线的净增长。增长预算
必须有可比基线：target、architecture、configuration、rendererBackend、
webTarget 必须一致；版本可以不同。没有预算时仅报告。

SDK 仓库应用打包可指定 -SizeBaselineDirectory 和 -SizeBudgetPath；基线目录
按原生目标命名文件，如 SouiStarter.json，内容为此前完整应用体积报告。
预算超限返回非零，归档前留下 size-analysis 报告。最终 archive.bytes 仍记录
实际压缩大小，与 payload.bytes 分开。

新测试通过 tools/Test.ps1 执行，也可从 SDK 仓库根目录单独运行：

```powershell
node --test tests/sdk/web-targets.test.cjs tests/sdk/web-targets-integration.test.cjs tests/sdk/size-report.test.cjs
```
