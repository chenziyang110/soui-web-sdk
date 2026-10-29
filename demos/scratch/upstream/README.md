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

## Renderer 源配置与可重现配方

`--renderer-profile` 仅接受 `stock`（默认）和 `local-hull-cache-v1`。这里的 stock 指原有**源码构建的 renderer**；它仍保留既有 targets reducer 优化和中立 Runtime 计数器这两个补丁，不等于未修改的 npm15.3 artifact。

```powershell
node demos/scratch/upstream/build.mjs --source-root CLEAN_PINNED_CHECKOUT --output NEW_STOCK_OUTPUT --renderer-profile stock
node demos/scratch/upstream/build.mjs --source-root ANOTHER_CLEAN_PINNED_CHECKOUT --output NEW_CACHE_OUTPUT --renderer-profile local-hull-cache-v1
```

两次均需固定 upstream/lock 的独立干净 checkout；不接受任意 patch 文件、replacement 根目录或未知 profile。缓存配置只增加已审 r2 的两个替换文件 RenderWebGL.js、Silhouette.js 和新增 LocalHullCache.js。模块登记它们唯一允许的路径、原始 LF SHA（新增文件为 null）与结果 LF SHA，并在写入前校验原始文件/新文件不存在，构建后再次校验精确五补丁闭集。

复制完整对应源码时显式加入新增 LocalHullCache.js；不能只运行 git ls-files 然后遗漏未跟踪的新源。stock 的对应源路径闭集为3411项，缓存配置3412项，路径集合绑定固定 release/profile 的 SHA，校验拒绝少文件、多文件或把缓存修改隐藏在 stock 清单里。

新输出额外包含 recipe/。它镜像最小 SDK 目录布局，随附 build/provenance/profile module、targets patch、本文、vendor-lock 和 SDK LICENSE.md；缓存配置另含3份固定 renderer replacement 文件。构建前冻结这些真实配方字节、构建后确认未变，再复制进输出并登记完整 recipeFiles。接收方既核 source/dist 清单，也核 shipped recipe 的固定路径闭集及与当前可信配方逐字一致；不可自行改 record 的 driver/module hash 来收养旧产物。

完整配方可以从其自身路径重跑，仍要求新的干净上游 checkout 和新的输出位置：

```powershell
node SOURCE_BUILD/recipe/demos/scratch/upstream/build.mjs --source-root NEW_CLEAN_CHECKOUT --output ANOTHER_NEW_OUTPUT --renderer-profile local-hull-cache-v1
```

source-build.json 显式登记 rendererProfile、2或5个精确patch、sourceMemberPathsSha256、recipeFiles，继续绑定 driver、provenance、完整对应源、lock 与 dist。新 driver/provenance 的身份已变，既有记录必须原样保留；控制和候选都要全新构建后才可声称通过新接收器。

## 同资源 cache off/on 与旧路径开销控制

缓存 profile 的构建**默认仍关闭缓存**。只在受控实验中，对同一个已冻结的新 bundle 显式调用 `vm.renderer.setLocalHullCacheEnabled(false|true)`。实验须记录实际 renderer 是否存在该 API、当前缓存是否启用、条目/点数以及页面/bundle/workload 身份；不存在 API或状态不符应失败，不能默默记成 cache on。

最小三组为：R0=新 driver 的 stock 源 profile，R1=cache profile 的同一新 bundle/off，R2=与R1逐字同资源/on。R1-R2衡量启用缓存的增量，R0-R1检查新 off 路径的 revision token/额外判断成本，R0-R2检查净变化。每组保持逻辑60Hz请求、真实VMsteps/运动积木量、目标数、素材、Runtime、GPU、计量接线相同；不要降低逻辑量来抬高 rAF。

这一版新增 profile 尚未执行 npm ci、上游构建、真实 SVG/MIP 浏览器测试或 FPS 对照。已有原算法 oracle 与局部 Node 差分/微基准只支持算法候选，不代表浏览器收益。真实启用时机和产品默认由集成者在 profile/兼容验证后决定。用户允许少量透明体积增长；应报告 bundle、runtime/ZIP 与缓存内存的实际增量，不以牺牲正确性抵扣字节。跨角色 skin 共享、GUI delta、虚拟化和 VM/Worker 改造仍为独立阶段。
