# 性能候选的发布体积门禁

工具只依赖 Node 内置模块，不进入应用运行包。正式大小结论必须来自冻结的实际构建；源码检查不能证明零增长。

准备仓库外 JSON 配置，路径相对配置文件：

```json
{
  "schema": "soui.performance-size-config/v1",
  "profile": {
    "backend": "miniblink",
    "architecture": "x64",
    "configuration": "Release",
    "toolchain": "MSVC143",
    "featureSet": "frozen-feature-inventory-sha256",
    "resourceSet": "frozen-resource-inventory-sha256",
    "packaging": "same-archiver-version-and-options",
    "requiredRuntimeDependencies": []
  },
  "buildIdentity": "actual-source-and-loaded-binary-manifest",
  "runtimeRoot": "candidate",
  "archive": "candidate.zip",
  "archiveRoot": "",
  "kernelPaths": ["miniblink.dll", "soui-sdk.dll"]
}
```

`kernelPaths` 按真实交付填写，支持单文件或目录前缀。每项都必须命中文件；不能用空范围跳过内核预算。`archiveRoot` 是 ZIP 中包目录前缀，ZIP 直接包含运行文件时为空。工具实际读取并解压校验每个 ZIP 文件的散列与运行目录完全一致；旧 ZIP 不能搭配新运行目录通过。支持单卷、未加密、普通 ZIP 的 stored/deflate，ZIP64/未知编码拒绝并要求另行审查读取器。两次配置的 profile、kernelPaths 必须完全相同，buildIdentity 分别绑定真实构建。featureSet/resourceSet 是已审查完整能力与资源清单身份，不能仅填一个随意标签。

```powershell
node tools/performance-size-gate.mjs snapshot ../baseline-config.json ../baseline-size.json
node tools/performance-size-gate.mjs snapshot ../candidate-config.json ../candidate-size.json
node tools/performance-size-gate.mjs compare ../baseline-size.json ../candidate-size.json ../size-diff.json
```

输出不存在时才写入，防止覆盖原始证据。snapshot 放在运行目录外；采集时冻结打包目录，不与其他写入/部署并行。工具拒绝链接、范围逃逸、重复路径、无效散列和伪造总数，同时要求压缩包、完整运行目录、SDK/内核三项均不增大。新增 DLL/OCX/SYS 或移除原运行文件会阻止自动通过，需先明确原因和等价能力，不能自行缩减基线清单绕过门禁。

此工具不证明功能、性能、签名或授权。零新 DLL 也不证明没有新增系统前置依赖：requiredRuntimeDependencies 和交付审查共同负责。库和资源在原文件内被删掉也需要能力回归与来源审查。

回归源码：`node --test tests/sdk/performance-size-gate.test.mjs`。本轮按用户安排只提供源码，统一候选后由专人运行。
