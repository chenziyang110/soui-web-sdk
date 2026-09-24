# V02 Workbench 数据交互页编译预检

状态：**源码、类型与两种 Web 构建已通过；真实桌面尚未执行**。2026-09-25 在 `demos/workbench/web` 对 Git 索引树 `8d9679bd81b99958b964a685c423ce4d441a6ee2` 运行 `npx tsc --noEmit -p tsconfig.json`、`npm run build:legacy` 和 `npm run build`，三项退出码均为 0。原始日志留在 `D:\SOUI-SDK-QEMU-validation\v02-workbench-preflight-20260925`：

| 文件 | SHA-256 |
|---|---|
| `typescript.log`（无诊断输出） | `E3B0C44298FC1C149AFBF4C8996FB92427AE41E4649B934CA495991B7852B855` |
| `legacy-build.log` | `7AC3778DACF3711D775CA7588CDF4C264AD7E54CAEC5DF4C2518C1D0DF75125F` |
| `modern-build.log` | `4E51F3125B94103465F667917D7BE438E2D8BEF1A089BB12F05907047A003B08` |

Legacy 输出生成 `react.html`，SHA-256 `7D104AB3F3482DD111CA71EACFF3D4D51FEB6EB497B5E9D8051AF35F1942BC55`；包含新 V02 页面标记的 `assets/main-legacy-CHDlOddn.js` SHA-256 为 `0913B3A9C8E9D8C9DDCFEFD80578529518C5533D4152D51FDD6537BF58BE8B01`。页面从 Workbench 首页及页面菜单进入，用对照页同一确定性数据生成器加载 10,000 行并虚拟滚动；中文输入记录 composition 事件，窗口 resize 与 RAF 诊断可见，公共 SDK 任务入口可在同页启动。

这些构建发生在宿主八小时长测期间，因此没有启动另一个 GUI，也不能从构建成功推断真实 WebView 行为、中文候选框、输入响应、实际呈现、后台效率或 Win7 SP1 x86 通过。下一步须在空闲桌面和目标客体实际滚动、输入中文、持续缩放、运行及取消任务，并保留带源码/载荷身份的原始结果。`releaseReady=false`。
