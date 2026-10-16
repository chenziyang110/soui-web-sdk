# 可选 UIA 构建与交付

此记录对应 ADR-0110，状态为待整合、待实际交付验证。

三个公开入口接受显式 UIA 选择：

```powershell
.\tools\Soui.ps1 build --with-uia
.\tools\Build.ps1 -WithUia
node .\tools\internal\standard-build.cjs --with-uia
```

实际运行时仍须提供所属入口原有的工具链与监督参数。SDK-only 使用 `-SdkOnly` / `--sdk-only`。省略 UIA 开关会明确关闭该 CMake 选项；不会把旧输出中的库当成本次选择。

完整源码及生成应用的库路径为 `out/<architecture>/sdk/accessibility/uia/<configuration>/soui_uia.lib`，SDK-only 为 `out/sdk-<architecture>/accessibility/uia/<configuration>/soui_uia.lib`。读取器核对当前实际文件；LLVM 进一步核对实际归档输出。该库为可选项，业务仍需主动创建 provider、接入窗口消息、发布公共状态并处理动作。

独立提案运行的 258 项测试覆盖参数转发、默认值、SDK-only 路径、选择与产物不一致、缺库、文件漂移、实际归档证明缺失或被替换、重复归档记录，以及原有命令/Host64 读取边界。它们不构成真实安装或应用可访问性结论。

后续独立 CMake 安装探针已完成两架构 ON/OFF、安装树搬移和 imported `Soui::Uia` 实际消费，各 11 项原生用例自然通过；首个 x64 测试消费者遗漏自身 JSON 依赖导致的失败单独保留。它未替代新增公共 Build/Soui 开关的实际构建。整合后继续保留公共构建原始记录并重验最终候选。业务投影、真实读屏器和目标系统矩阵仍分别验收。
