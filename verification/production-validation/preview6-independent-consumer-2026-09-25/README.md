# preview.6 独立安装消费者预检

2026-09-25，源码指纹 `2ac1f9aded43c894b8d798fad2fffae233049eb79e29b4d8d447c68c056fe557`，版本 `0.3.0-preview.6`。本记录验证公开 SDK 可被与模板、Demo 无关的应用接入；`releaseReady=false`。具体 build-id、安装快照、运行载荷和控制台应用散列见 [observation.json](observation.json)。

| 架构 | 公开头文件与静态库消费者 | 独立桌面验收应用准备 | 安装内容 |
|---|---|---|---|
| x64 | `tests/package-consumer` 使用已安装 SDK 的 CMake 包编译，运行退出 0 | `tools/Test-Acceptance.ps1 -Architecture x64 -Configuration Release -PrepareOnly` 退出 0；独立 C++ 消费者和 Web 页面编译、类型检查、75 文件运行载荷完成 | 32 文件，含第三方声明和四份许可 |
| x86 | 同上，运行退出 0 | 对应 x86 `-PrepareOnly` 退出 0，75 文件运行载荷完成 | 32 文件，含相同声明和四份许可 |

两个架构的安装库散列均与各自 SDK-only 构建输出及 [build.json](x64/build.json) / [build.json](x86/build.json) 的逐文件记录相同。声明文件的 SHA-256 均为 `21c9dd51116f3e8b4c189ac220ecc41735b3ca0bed30dc1efd876b86f08e8288`。归档保留配置、编译、安装、Web 构建和类型检查日志；空的类型检查日志表示命令成功且未输出诊断。构建身份按架构分别记录，不能把此前完整应用构建的静态库字节散列当成这次 SDK-only 构建散列。

`-PrepareOnly` **没有运行桌面场景**。此记录仅证明安装的公共 C++/TypeScript 接口可供独立工程构建及部署、控制台服务注册可运行；不证明 WebView、真实 Bridge 业务、桌面交互、Win7 SP1 x86 或长期稳定性通过。宿主八小时测试和外部 QEMU 仍运行期间未启动另一场桌面或客体测试。
