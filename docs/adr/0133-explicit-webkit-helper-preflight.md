# ADR-0133：显式 WebKitGTK helper 部署预检

## 背景

Linux 官方发行 WebKitGTK 的 helper 路径在运行库构建时固定。当前隔离依赖可链接，
但系统前缀缺少 WebKitNetworkProcess 时 WebKit 会中止进程。官方 .pc 不提供该路径，
标准位置候选不能当作事实；已有 doctor 因此只报告候选/unknown。

## 决定

新增可选构建项 SOUI_DESKTOP_WEBKIT_HELPER_DIR，部署方明确填入所用运行库的真实
PKGLIBEXECDIR，必须为目标系统绝对路径。默认空，不猜路径、不阻断未知部署，也
不声称默认配置能防止 WebKit abort。该值编入 Linux SDK 静态库，随安装 SDK 传播，
不是应用可覆盖的 helper 搜索路径，不设置环境变量或改变 WebKit 加载行为。

配置时 Linux 在 GTK/WebKit 初始化前只读检查 WebKitNetworkProcess 与
WebKitWebProcess 为普通可执行文件；确定不可用时返回 E_WEBKIT_RUNTIME，错误不
泄露路径。通过只代表文件候选存在，不验证其架构、依赖、沙箱或启动成功。
符号链接可指向正常安装的普通文件；不执行 helper，不调整权限。无并发文件替换
防护承诺。实际运行库更换前缀后应重新构建 SDK，不复用错误的路径契约。

## 兼容与验证

不改变 Windows/macOS 或默认 Linux 行为；不复用不可靠的标准路径猜测。最小
文件检查回归与当前官方库显式配置的失败初始化验证；真实 GUI 仍被缺失系统
helper 阻塞。没有关闭沙箱、修改第三方二进制或重试安装。
