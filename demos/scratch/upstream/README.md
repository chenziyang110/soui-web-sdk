# 可追溯 Scratch 源码性能候选

本目录维护源级改动，不改 `.scratch-cache`、现有 bundle 或运行期方法。当前候选将 targets reducer 的两次过滤及逐角色临时对象合并为一次遍历；保留每次新对象、顺序、stage、editingTarget 和嵌套可变 VM 数据语义。Runtime 可选计数器只记录真实 `_step` 与成功返回的 draw；默认 null，采样工具显式启用。

上游固定到 vendor-lock 指定的 release commit：`b4b934aaf6641a539325a971249234f4a50a2f03`。原始源版本字段为 15.2.0，发布的 standalone 包为 15.3.0，构建记录分别保留两者；不把自行编译输出冒充原始 npm 包。

专职构建人员在仓库外准备干净 checkout（命令从 SDK 根执行）：

```powershell
git clone https://github.com/scratchfoundation/scratch-editor.git ../scratch-performance-source
git -C ../scratch-performance-source checkout --detach b4b934aaf6641a539325a971249234f4a50a2f03
node demos/scratch/upstream/build.mjs --source-root ../scratch-performance-source --output ../scratch-performance-candidate
```

脚本检查干净提交、固定 package-lock 和两个修改文件原始 SHA256，应用源码改动，再以原 lock 执行 `npm ci` 和 workspace 构建。失败非零退出；不覆盖旧输出，不删除用户目录。`--npm-cli` 可指定当前 Node 配套 `npm-cli.js`，避免 Windows shell 拼接。构建会修改此专属 checkout，不要对含其他任务改动的目录运行。

结果包含 dist、全部跟踪源码与配置、原 lock、LICENSE 和 source-build.json。记录绑定 driver、provenance module、完整源码和 dist 清单及两个补丁文件；缺少源码清单的旧记录必须由专人重新构建，不能直接采用。运行时资源不混入源码维护文件；选择 SourceVendor 的本地包将完整构建输出复制到 `source-materials/scratch-source-build/`，随包保留对应源码、锁、LICENSE 和构建身份。

通过 Web 构建入口的 `--source-vendor` 参数采用该候选，入口必须核对 provenance 和 bundle 散列。原固定 npm 包保留为对照；两端性能比较使用同一选择，不允许仅优化 SOUI 页却以旧 Electron 页作宿主差距结论。

```powershell
powershell.exe -File demos/scratch/Build.ps1 -Architecture x64 -SourceVendor ../scratch-performance-candidate
powershell.exe -File demos/scratch/Package.ps1 -Architecture x64 -SourceVendor ../scratch-performance-candidate
```

SourceVendor 不能与 SkipWeb 混用，避免用旧输出冒充源码候选。Build/Package 默认仍选固定对照包；经过体积、正确性及收益验收后再由集成者采用候选，不在未编译时切换发布默认值。

Package 继续显式传递 SourceVendor 并复用 Web 构建的 provenance 核验。它要求部署的 runtime bundle、HTML 身份与 source-build 记录一致，复制后再次核验源码闭集；不能把源码版 runtime 当作 stock 打包，也不能只交付旧 npm 原包充当补丁版对应源码。包说明及 `package-manifest.json` 分开记录实际源码版本 15.2.0 和对照 artifact 15.3.0。

当前未执行 npm 安装、上游编译或性能测试。结构性分配减少不等于已证明 FPS 提升。skin 共享、角色虚拟化、VM/Worker 改造仍需 profile 和独立兼容设计，不默认启用。最终产物还必须通过共同体积门禁。
