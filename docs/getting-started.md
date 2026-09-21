# 接入与业务开发

1. 从交付根目录运行 `tools/Doctor.ps1`，按检查结果安装缺少的开发组件。
2. 使用 `tools/New-Project.ps1 -Name 产品工程名 -OutputRoot 新目录`。
3. 在生成目录运行 `tools/Build.ps1 -Architecture x64`。
4. 启动 `out/x64/bin/Release/工程名/工程名.exe`。

工程名用 2–64 位英文字母、数字或下划线，首位为字母。已有目录会被拒绝，避免覆盖业务代码。

## 最小业务示例

模板中的 `app/main.cpp` 注册 `app.greet`。页面的 `web/src/native.ts` 定义对应 TypeScript 方法类型，`web/src/main.tsx` 发起真实调用。修改这三处和 `contracts/app.contract.json` 可以形成自己的第一个接口。

`contracts/app.contract.json` 是最小示例的可读合同，目前不会自动生成示例代码；更改合同后应同步修改 C++ 校验、TypeScript 类型和测试。`tools/contractgen` 提供工作台合同格式的生成器，合同格式与最小示例不同，使用前参照 Demo 的 `contracts` 输入。

方法名和权限名明确注册在 C++。页面不能自行扩大权限；默认只授权该适配器注册的能力。处理外部输入时保留类型、长度、路径和权限校验。耗时操作放入任务服务，不阻塞 UI 或引擎线程。

## 页面开发

进入 `web` 后修改 React 页面，运行 `npm run typecheck` 和 `npm run build`。模板构建输出经过 Chrome 49 兼容转换；不要删掉经典脚本入口生成步骤。随后在项目根运行 `tools/Build.ps1 -SkipWeb`，重新部署 Web 资源。

生成的源码目录可以搬移、单独建立 Git 仓库。构建路径均从当前工程计算；不需要环境变量 `SOUIPATH` 或原 SDK 仓库。

## 分发应用

分发整个应用的 `bin/配置/工程名` 输出目录，包括 DLL 和 `resources`。只复制 EXE 会缺失运行依赖。开发用 `sdk`、Node、npm 及编译器不需要随应用部署。

模板保留标准系统标题栏和 DPI 清单。高级自定义标题栏、文件扫描与媒体功能见独立的 `demos/workbench`。
