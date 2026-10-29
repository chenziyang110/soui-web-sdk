# 2026-10-29 本机候选验证

- SDK：0.6.0-preview.9，本地修改候选；Scratch GUI/VM：15.3.0。
- 架构/配置：Windows x64 Release；WebView2 Runtime 156.0.4314.50。
- `Build.ps1 -SkipWeb`：当前原生源和 Web 资源构建、部署成功。
- `SouiScratch_export_contract.exe`：当前原生导出边界回归通过，包含分块、顺序、Base64、会话、类型、ZIP、关闭和 16 MiB 限制。
- `sdk.native.local-editor-policy`：1/1 通过；覆盖默认 strict、显式模式、能力声明、未知策略、缺失资源及 MiniBlink 拒绝。
- `verify-web.cjs`：无页面异常、无缺失资源；旗帜程序将角色从 x=0,y=0,direction=90 改为 x=-85,y=0,direction=108；生成 42,356 字节 .sb3 并重新载入；绘图隔离沙盒存在；切换“按键漫游”后，真实浏览器 ArrowRight 按键将 x 从 0 改为 15。
- 当前完整资源的原生 `--smoke-test`：编辑器通过 SDK Bridge 报告 ready，Host 完成关闭，进程退出 0。
- 1,347 份本地素材均经 MD5 校验，附 SHA-256 清单。此计数是去重资源文件数，不是角色数量。

浏览器报告和截图在 SDK 的 `out/scratch-verification/`；原生启动日志在 Local AppData 的 `SouiScratch/smoke-<pid>/smoke.log`。
资源交付清单为运行目录的 `resources/.scratch-resources.json`，原生载荷身份记录在 `out/scratch-verification/native-report.json`。

桌面解锁后已确认完整原生宿主界面、实际 SDK 版本及积木/舞台显示；首次真实保存发现 32 KiB params 限制，已改为 16 KiB 二进制分块，当前独立打包构建和原生契约回归包含此修复。
用户随后用 Esc 停止界面控制，修复后的系统保存对话框、中文输入和原生拖拽没有继续自动操作；不将浏览器功能检查或导出边界回归替代这些结果。
未执行 x86、长期稳定性、网络/硬件扩展、整套 SDK/DevTools 或生产发布验收。

## 性能场景增补

- 三种中档场景已在官方 Scratch VM 验证启动、运动、计算变量、确定性克隆序号及停止。
- `verify-performance.cjs`：三个重档通过；运动场景 241 个目标/240 线程，粒子 282 个目标/280 线程，计算 121 个目标/120 线程；计数包含舞台及原始发射器时按报告解释。
- 页面异常 0，缺失资源 0；每个场景 `stopAll()` 后线程数归零。
- 完整 30 秒采样、报告大小、测量完成状态已检查；隐藏失效、统计窗口、生命周期等 5 项单元回归通过。
- `app.saveMetrics` 原生 JSON 导出验证通过类型、schema、深度、24 KiB 报告和 32 KiB 参数限制；实际另存为对话框未自动操作。
- 浏览器报告在 `out/scratch-verification/performance-functional-report.json`，采样例在 `performance-report-sample.json`。
- 这些是功能验证，不是 SDK 性能达标或版本提升数据；没有将 headless rAF 数值当作原生呈现结果。
