# ADR-0126：可搬移应用目录与可执行文件相对资源

状态：开发增量；未签名，未作为正式发布或安装器。

## 背景与决定

SDK 开发 ZIP 不是最终应用。增加独立 Node staging 工具，把已构建的程序和
可信前端资源复制到新目录，生成平台/架构校验、文件散列和依赖说明；不在此
入口重新构建 SDK、不运行输入程序、不自动下载或捆绑系统 WebView。
Windows/Linux 使用程序与 web 同级目录，Linux 提供可搬移启动脚本；macOS
使用标准 .app 的 Contents/MacOS、Contents/Resources/web 和 Info.plist。

公共 `application_resource_directory()` 返回真实运行程序位置对应的默认
web 目录，不依赖 cwd、PATH 或 argv[0]；macOS bundle 使用 Resources/web。
失败返回明确错误，不回退当前工作目录。现有显式 options.resource_root
不变；统一示例只在未传入显式目录时调用新 helper。

输入二进制头核验平台和架构，拒绝未支持的组合与 fat/universal 输入；拒绝
覆盖已有输出、symlink、特殊文件、非法可搬移名称及不安全路径嵌套。输入
必须为应用拥有、复制期间保持不可变的可信目录，检查不等于抵抗恶意并发替换
的文件系统沙箱。应用版本由调用者显式声明，不修改 SDK VERSION/许可。

## 备选与边界

直接依赖 cwd 会破坏 Finder/快捷方式启动；argv[0] 可能仅为命令名，因此使用
操作系统实际程序路径。暂不引入通用安装器、自动依赖拷贝、签名、公证、自动
更新或系统配置改动。Linux 的发行版 ABI、GTK/WebKitGTK/helper，Windows
WebView2/VC runtime，macOS 最低系统与签名仍由部署者完成目标机验收。

## 验证与迁移

旧显式资源路径保持兼容。仅执行必要 Linux 构建、路径/目录 staging 冒烟及
拒绝关键非法输入的检查；跨平台编译、搬移后的真实 GUI 启动、签名与依赖部署
测试留待统一验收，生成目录不代表可在所有机器直接运行。
