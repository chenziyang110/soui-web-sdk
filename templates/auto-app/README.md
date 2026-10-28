# SouiAutoStarter

同一程序在启动前选择一次后端：Windows 10/11 上优先使用实际探测到的 WebView2
Runtime；明确不可用或系统只适用兼容路径时，使用包内 MiniBlink。非法配置、权限错误、
损坏资源、初始化失败和超时按错误退出，页面开始运行后不会切换内核或重放业务动作。
设置 `--require-modern` 后，Runtime 缺失会失败，不降级。

模板通过可选 `Soui::AutoHost` 组件接入；已有 compatibility 和 modern 模板行为不变。
完整源码仓库运行 `powershell.exe -File tools/Build.ps1 -WithAutoHost -Architecture x64`。
生成项目使用 `powershell.exe -File tools/New-Project.ps1 -Name MyApp -OutputRoot ..\MyApp -Profile auto`；
进入生成目录后运行 `powershell.exe -File tools/Build.ps1 -Architecture x64`。x86 使用相同入口。
业务程序不需要 Node.js，不会自动联网下载浏览器。

`web/src/` 是共享的 React 18 + TypeScript 页面和业务。`project.json` 声明现代与兼容两个目标，
构建入口分别产出 `web/dist/modern` 与 `web/dist/compatibility`，部署到
`resources/web-modern` 与 `resources/web-compat`。兼容构建包含 Chrome 49 转译、polyfill 和
经典脚本入口；现代构建只包含现代资源。直接执行 `npm --prefix web run build` 只构建默认目标；
完整项目构建会生成两份。兼容页面不依赖 Proxy、CSS Grid 或其他未经降级的现代功能。

问候动作唯一来源是 `contracts/actions.json`。生成项目中修改后执行
`node tools/sdk/actiongen.cjs --source contracts/actions.json --root .`；附加 `--check`
只检查生成内容。C++ 使用 `register_Greet`，页面使用 `callAction` 校验输入和返回值。
静态描述不授予权限，动作发现与调用仍由原生 Bridge 授权。

启动和关闭在窗口 UI STA 完成。窗口保留 Host 和借用的 Bridge，直到 `shutdown()` 成功；
收到 `E_BUSY` 时继续消息循环并重试。WebView2 使用独占的用户目录，等待浏览器自然退出；
所有宿主销毁后才请求 MiniBlink 进程级最终关闭。关闭超时按失败处理，不记录成功退出。
普通启动复用 Local AppData 的应用浏览器目录，独占模式拒绝并发占用；smoke 创建独立目录。
需要同时运行多个实例的应用，应自行分配受控目录和实例策略。

`--smoke-test` 隐藏窗口但真实创建后端、加载页面并完成原生动作往返。页面确认中文问候返回值，
检查非法输入被拒绝，然后报告就绪；原生端同时确认实际处理过问候动作。日志记录实际后端、
选择原因、Runtime 探测结果、页面就绪和完整自然关闭。它不代表持续帧率、输入延迟或生产稳定性验收。

可选诊断参数：

- `--smoke-log=绝对路径`：独占创建新的日志文件，已存在则失败；父目录必须存在。
- `--user-data=绝对路径`：创建新的用户数据目录，已存在则失败；父目录必须存在。
- `--webview2-runtime=绝对路径`：在明确指定、已存在的目录实际探测 Fixed Runtime，路径原样传给 SDK。
  空目录可用来验证真实“未发现 Runtime”的降级路径，不修改系统 Runtime。
- `--require-modern`：明确要求现代后端，不允许降级。

隔离空目录的验证与整机未安装 Runtime、Windows 7 或 PE 实机验证不是同一结论。
打包必须保留两份页面及运行时清单；不可只复制现代页面后宣称已具备离线降级能力。
