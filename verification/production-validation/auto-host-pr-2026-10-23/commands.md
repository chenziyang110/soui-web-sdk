# 本候选复验命令

以下在完整源码仓库根目录执行，既定 npm 依赖须先安装。
执行结果与来源限制见 [README](README.md)，命令列表本身不代表执行通过。

## 交付与打包回归

```powershell
$testFiles = @(
  'tests/sdk/auto-packaging.test.cjs',
  'tests/sdk/auto-smoke.test.cjs',
  'tests/sdk/web-targets.test.cjs',
  'tests/sdk/web-targets-integration.test.cjs',
  'tests/sdk/web-build-profile.test.cjs',
  'tests/sdk/web-build-profile-integration.test.cjs',
  'tests/sdk/size-report.test.cjs',
  'tests/sdk/runtime-deployment.test.cjs',
  'tests/sdk/runtime-bundle.test.cjs',
  'tests/sdk/runtime-packaging-integration.test.cjs',
  'tests/sdk/runtime-build-evidence.test.cjs',
  'tests/sdk/compression-profiles.test.cjs',
  'tests/sdk/compression-command.test.cjs',
  'tests/sdk/compression-measurement.test.cjs',
  'tests/sdk/delivery-identity.test.cjs',
  'tests/sdk/application-package.test.cjs',
  'tests/sdk/gui-bindings-entrypoints.test.cjs'
)
node --test @testFiles
```

## 依赖审计

六个实际审计目录如下；审计使用各目录现有锁定依赖，未以自动升级修补结果。

```powershell
npm --prefix sdk/web audit --json
npm --prefix sdk/bindings/react audit --json
npm --prefix sdk/bindings/vue audit --json
npm --prefix templates/auto-app/web audit --json
npm --prefix tests/gui-bindings audit --json
npm --prefix tests/gui-bindings/react18 audit --json
```

## 原生与默认 auto 交付

每个架构使用同一候选源码，按下列入口构建、准备验证工具、运行双后端，再打包。
首次完整 Web 构建应省略 `-SkipWeb`；后续只有同源码/档位/输出证据有效时才可复用。

```powershell
powershell.exe -File tools/Build.ps1 -Architecture x64 -Configuration Release -WithAutoHost -Install
powershell.exe -File tools/internal/Build-ValidationTools.ps1 -Architecture x64
powershell.exe -File tools/Test-Auto.ps1 -Architecture x64 -Configuration Release
powershell.exe -File tools/Package-Applications.ps1 -Architecture x64 -Profile auto
```

x86 替换 `-Architecture`。两架构原生回归实际选择以下 10 项，未用完整原生套件的
数量代替定向结果：

```powershell
ctest --test-dir out/x64 -C Release --output-on-failure -R '^sdk\.native\.(auto-host-contract|auto-host-profile-path|auto-host-callback-exception|host-adapter|miniblink-unmount-continuation|miniblink-unmount-unused-transfer|runtime-profile|webview2-cancelled-controller|webview2-prepare|webview2-preparation-lifetime)$'
```

Web SDK 回归使用 `node --test tests/web/sdk-client.test.mjs`。仓库外生成项目使用
正式 SDK 包、`New-Project.ps1 -Profile auto` 和自己的 `tools/Build.ps1`，真实双后端
验证由 `tools/Test-Auto.ps1` 显式指定已准备验证工具的完整源码根。具体项目与载荷
身份见本目录摘要；本候选只对该外部项目执行 x64。
