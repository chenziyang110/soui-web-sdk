# 同页面运行包体积测量

本工具把 x64 Release 的两个最小宿主和同一份 `dist-legacy` 页面分别装入独立目录，生成完整文件散列清单、解压大小和同算法 ZIP 大小。SOUI 包包含宿主 EXE、Miniblink、SOUI、应用本地 VC/UCRT DLL 和现有声明文件；Electron 包从锁定的 `electron` 发行目录构造 `resources/app`，保留其发行文件和许可材料。两端页面目录的逐文件散列必须相同。它测的是当前最小宿主运行包，不是正式安装程序或六组业务 Demo。

先按 [双端宿主说明](../README.md)构建共享页面，并执行：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/Build.ps1 -Architecture x64 -Configuration Release -SdkOnly -Install
cmake -S benchmarks/hosts/soui -B out/benchmark-soui-x64-19041 -G 'Visual Studio 17 2022' -A x64 '-DCMAKE_SYSTEM_VERSION=10.0.19041.0' "-DSouiSdk_DIR=$((Resolve-Path out/install/x64/lib/cmake/SouiSdk).Path)"
cmake --build out/benchmark-soui-x64-19041 --config Release
npm --prefix benchmarks/hosts/electron ci
node benchmarks/hosts/electron/node_modules/electron/install.js
node benchmarks/size/measure.cjs ([IO.Path]::GetFullPath('../benchmark-size-run'))
```

最后一个目录必须是尚不存在的仓库外路径；测量程序不删除旧结果，也不修改 SDK 源码。需要 `7z.exe` 在 PATH 中。`report.json` 保存源码/构建身份、Electron 精确版本、共享页面与所有包文件 SHA-256、两份 ZIP 的散列、大小和比值。程序会从打包后的两个宿主各启动一次 S01 写入探针，验证退出码、结果结构及 Electron 的页面散列和安全配置；探针日志和结果放在 `smoke/`，SOUI 探针使用包外副本，不改变被测 ZIP。探针没有真实输入和帧采集。没有完整进程树资源、安装签名和授权闭环时，体积比值不能单独证明采用价值或生产准入。
