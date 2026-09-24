# 同负载 SOUI / Electron 对照

状态：宿主与共享页面接入中；尚无可用于采用决策的性能样本。完整门槛见 [`docs/roadmap/0.3-sdk-validation.md`](../docs/roadmap/0.3-sdk-validation.md)。本目录随完整源码包交付，**不进入 SDK Core 或应用运行包**。

## 构成

- `../demos/workbench/web/benchmarks/electron-smoothness/`：两端共用的 React 页面、确定性 10,000 行 fixture 和 S01–S05 场景。一次构建生成相同的 `benchmark.html` 与资源文件。
- `hosts/soui/`：仅链接已安装的公开 C++ SDK；页面通过公开 Web SDK Bridge 分块写结果。默认使用当前候选的软件呈现路径。
- `hosts/electron/`：精确锁定 Electron `44.4.4`，隔离预加载脚本只暴露写结果方法；`sandbox`、`contextIsolation` 和 `webSecurity` 启用，`nodeIntegration` 关闭。
- `protocol/`：同设备、同页面、同操作、测量和无效样本的规则；`check-page-pair.cjs` 对两端页面结果进行前置校验，独立 `ProcessTreeSampler` 保存 Win32 整进程树资源原始样本。

## 构建共享页面

先按仓库 `tools/Build.ps1` 构建公开 Web SDK，再在 `demos/workbench/web` 执行：

```powershell
npm ci
npx tsc -p benchmarks/electron-smoothness/tsconfig.json --noEmit
npm exec vite -- build --config benchmarks/electron-smoothness/vite.config.ts
node benchmarks/electron-smoothness/create-miniblink-entry.cjs
```

产物目录为 `demos/workbench/web/benchmarks/electron-smoothness/dist-legacy`。记录该目录逐文件散列；SOUI 与 Electron 必须使用同一份目录，禁止分别构建页面后直接对比。

## 启动最小宿主

SOUI 宿主需要已安装的 `SouiSdk 0.3.0` CMake 包及同架构 `node.dll`：

```powershell
cmake -S benchmarks/hosts/soui -B out/benchmark-soui-x64 -G "Visual Studio 17 2022" -A x64 -DSouiSdk_DIR=<SDK安装目录>/lib/cmake/SouiSdk
cmake --build out/benchmark-soui-x64 --config Release
out/benchmark-soui-x64/bin/Release/SouiBenchmark.exe --resources=<共享页面dist-legacy> --miniblink=<x64 node.dll> --result-dir=<本地结果目录>
```

Electron 宿主只需在 `benchmarks/hosts/electron` 执行：

```powershell
npm ci
node node_modules/electron/install.js
npm start -- --benchmark-page=<共享页面dist-legacy/benchmark.html> --result-dir=<本地结果目录>
```

两个宿主都只接收页面结果。SOUI 通过逐块 Bridge 请求写入，Electron 通过隔离的 IPC 写入；输出目录由启动命令指定，页面不能指定文件路径。整进程树原始采样器已可单独构建和运行，但尚未执行正式双端轮次；自动化输入、物理呈现采集和两端配对报告仍缺失。因此 `result-written` 仅证明页面数据落盘，**不代表性能门槛通过**。

两端的目标**内容区**均为 1100×720；历史 SOUI 宿主使用外框尺寸，实际页面为 1084×681，旧探针不可用于比较。已有两端页面结果时，可运行 `node benchmarks/protocol/check-page-pair.cjs <soui-result.json> <electron-result.json>`；退出码 0 只表示单对页面数据满足场景时长、fixture、内容区和基本动作条件。报告中的 `remainingEvidence` 仍须由外部输入、实际呈现、进程树、启动重复采样及逐文件散列补齐。

宿主写入链路可用 `--smoke` 做本地探针。Electron 在正常页面上自动提交一份 S01 结果；SOUI 需先把整个构建目录复制到新的测试目录，再以生成的 `benchmark-smoke.html` 覆盖该副本的 `index.html` 后使用 `--resources=<测试目录> --smoke`。这是因为 SDK 只允许受信的 `index.html`/`react.html` 本地入口；测试目录不能用于正式对照。三端已执行的原始探针在 [`verification/production-validation/benchmark-host-smoke-2026-09-23/`](../verification/production-validation/benchmark-host-smoke-2026-09-23/README.md)。
