# 0.4.0-preview.1 当前验证

日期：2026-10-08。工程代码指纹 `3079521f32e26c94316d351731c20c33b5e356a061d244bd2e8e114297c23602`；固定 SDK 清单 SHA-256 `7bc3dabd484656fbb273dbcbe41f0f472aec9a702c4011033e1e337b0d0ca513`。本目录保存实际执行证据，`releaseReady=false`。

## 本机及独立交付

- 四组 x86/x64 × Debug/Release 的编译、每组 11 项原生契约测试、兼容 Starter/Workbench 默认/React 及现代 Starter 启动证据由根 `verification/acceptance.json` 汇总。当前主机为 Windows 11 10.0.26200、1920×1080、75 Hz。
- `modern-project/`：现代项目在仓库外新建和搬移后分别编译、真实启动；代码指纹和固定 SDK 身份相同。`rejected-downgrade.json` 记录缺少现代组件的旧 SDK 在变更锁文件之前被拒绝。
- 兼容独立项目的六阶段新建/旧版基线/升级/回退/再升级/搬移和外部 CMake 消费者见根 `verification/generated-project.json` 等汇总。
- `clean-checkout/`：独立 Git 副本的 x64 Release 构建、完整测试和四个应用场景。Windows 历史证据长路径须启用仓库本地 `core.longpaths`。首次 Git 检出的三个授权文件因换行转换未匹配固定 SDK；添加保留字节的属性后重检出，完整构建和启动通过。额外新建的未修改副本也通过固定 SDK 源文件核对，见 `fresh-license-checkout.json`。
- `packages/`：六个运行 ZIP 的源码/载荷/归档身份。现代运行包不包含 WebView2 Runtime，不能用其大小代替完整离线部署成本。`sdk-manifest.json` 是固定 SDK 的文件清单。

## 生命周期

| 证据 | 架构和模式 | 结果 |
|---|---|---|
| `modern-x86/` | 重载 10、重建 10 | 正常退出；实际 Runtime 154.0.4258.62 |
| `modern-x64/` | 重载 10、重建 10、另重建 1,000 | 全部正常退出；千轮有 1,000 个独立文档会话 |
| `compatibility-x64/` | 重建 1,000（预热 3），另重载/三窗口/Bridge 各 3 | 全部正常退出；千轮约 17.5 分钟 |

现代消费者自身源码指纹与主工程指纹分开记录；`build.json` 同时绑定安装后的公共 SDK 库、可选组件和 Loader 的实际散列。所有运行都保存请求、原始事件和结果；不将历史实验的成功标记搬作本版验收。

兼容千轮测量阶段 private bytes 为 33,603,584–150,650,880 字节；四段中位数依次为 86,908,928、94,916,608、99,508,224、102,449,152 字节。存在回收波动，分段中位数仍上升。普通句柄在 296–544 之间，GDI 11–33、USER 9–12；这些样本不足以证明资源已收敛。`summary.json` 仅描述样本，原始结果的 `resourceGate=not-evaluated` 保持不变。现代结果也未评估完整进程树资源门槛。

## 目标系统和失败记录

- `win7/`：Win7 SP1 x86（6.1.7601）QEMU 来宾，真实 Starter 和诊断程序退出 0；串口保存 `application-ready`、`host-close-complete`。当前构建与送入来宾的逐文件载荷匹配；系统镜像、ISO、启动参数和截图一起保留。
- `pe31-initial/`：同代 Setup PE 3.1 x86 中原生诊断可运行并正确识别 PE；固定 Miniblink GPU 编译产物静态依赖 OPENGL32.dll，系统缺少该文件，应用退出 1。运行时选软件呈现不能消除静态 DLL 导入。这是应用兼容失败，不因诊断成功而变成通过。
- `pe31-software/`：使用同一内核源码、已验证的底层静态库和既有构建入口，GPU 参数留空，隔离重建软件候选。导入表无 OPENGL32.dll；本机启动通过，Setup PE 3.1 x86 中退出 0、就绪/关闭标记完整，普通界面显示中文和 C++ 问候。PE 缺少串口导出组件，结果由保留的来宾截图观察取得，不能声称串口自动验证通过。候选没有替换固定 SDK；构建仍有上游告警（包括 libxml C4789），源码维护和授权门槛不变。
- 同一 x86 软件候选在 Win7 SP1 来宾也退出 0，`pe31-software/win7-serial.txt` 保存诊断/应用退出码及完整就绪/关闭标记。候选运行 ZIP 的解压后逐文件结果见 `packages/pe-software-x86-candidate.json`。
- `pe-public-x86/`、`pe-public-x64/`：本机独立公共 SDK 消费者使用明确指定的软件候选，两架构重载、重建、三窗口、Bridge 各 3 轮通过。构建记录将 `kernel.kind=explicit-candidate` 和实际 DLL 散列一起保存，防止误记成固定内核测试。
- `pe10-software/`：x64 无 GPU 候选在本机和 Setup PE 10 x64（10.0.17134，安装镜像版本 17134.228）完成就绪、关闭并退出 0。保存精确 ISO 身份、内核构建输入/输出、依赖导入表、来宾截图与运行参数。结果由截图观察取得，串口导出不可用；初始安装环境控制台出现内存资源提示，保留截图所见事实，应用仍实际完成短测。此候选也没有替换固定 SDK。
- PE 10 首次普通窗口截图取到尚未绘制的白屏，原图 `application.png` 保留；重新启动镜像并运行同一载荷，等待可见窗口绘制后取得 `recheck-application.png`，中文页面及 C++ 问候正常显示。这个观察不提供首帧时间或流畅度测量。
- `failures/modern-startup-trailing-space.json`：现代模板初次测试未进入启动测试分支；Windows PowerShell 5 传参尾空格触发了原始字符串比较错误。代码已改为 Windows 参数解析，最终四组与独立项目启动均重新验证。

没有执行本版 8 小时混合负载、120/144 Hz 实机呈现、全 Win7—Win11/PE 镜像矩阵、GPU 设备异常与资源回收、完整输入法/多屏/辅助功能。以上结果不构成这些门槛通过，也不构成 Electron 等价性能结论。
