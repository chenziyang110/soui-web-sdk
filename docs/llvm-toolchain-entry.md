# 显式 LLVM 正常入口候选

本目录是尚未采纳、尚未运行的源码候选。默认 `MSVC143` 的 VS17/v143/14.44/19041 配置和输出布局保持原合同。显式 `LLVM22MSVC142` 使用 LLVM 22.1.3、MSVC 14.29.30133 的头和 CRT、SDK 19041、CMake 3.29 以上及 Ninja Multi-Config。

`Build.ps1`、`Doctor.ps1` 和标准调用器接受六个实际路径：`LlvmRoot`、`MsvcRoot`、`WindowsSdkRoot`、`NinjaExe`、`WindowsCmdExe`、`PowerShellExe`。物理文件身份的两个脚本只能使用产品内固定 sibling；不接受调用者替换脚本。缓存必须匹配源码根、架构、生成器和完整 v2 输入身份及 12 个定义，`FreshConfigure` 也不允许跨工具链复用。子进程使用标准 Windows `ComSpec`，保存、清理并恢复原 provider 环境。

这个导数在历史 Slice1 的 SourceV5 工具链基础上，明确复制已独审 SourceV7 的工具链文件。Finder 和两固定身份脚本仍为原字节。SourceV7 的真实六步和正常入口的实际结果尚待验证；源码审查不能代替运行。

直接 `Build.ps1` 写出的请求和原始 CMake 配置只说明输入及配置状态。正常 SDK/维护模板的 `standard-build.cjs` 调用器先保存原始 ready、PID/字符串 FILETIME、argv、完整 Job 的自然退出记录，再在独立受监督的观察阶段读取真实 Ninja 命令、编译数据库、VALID 依赖记录和实际对象。每个选中编译对象必须有本轮真实命令，或具有同输入工具链、同 source/header/command/OBJ 的历史自然编译证明。档案成员必须逐字对应实际对象；应用载荷的 EXE 必须对应实际链接记录。资源编译没有 Ninja 头读取证明，历史复用要求全部冻结输入不变，并明确保存这一限制。

LLVM 标准调用器只接受完整自然 `0/0`、固定 2000ms drain 和无强制清理，不接受 MSVC 的 compiler-tail 例外。首次观察不能靠已有的无来源对象和 Ninja no-op 取得通过。重新验证必须保留原标准调用器的证据目录；这些记录是本机完整性证据，不是抵御有写权限者的认证机制。观察记录明确不声称所有文件读取、完整工具链资格、内核资格、Win7 或发布资格。

普通 smoke、现代 smoke 和应用部署读取器从当前 build 选择并校验工具链；LLVM 额外要求当前构建 ID 对应的标准调用器实际证明。显式选择相反工具链会拒绝，只有请求文件不会通过。现代模板原 smoke 的根进程/日志语义仍须另做完整应用 Job 验证。

正常调用器示例（在完整源码目录，先真实准备同架构验证工具）：

```powershell
.\tools\Soui.ps1 build --native-toolchain LLVM22MSVC142 --llvm-root <absolute-llvm-root> --msvc-root <absolute-msvc-14.29-root> --windows-sdk-root <absolute-sdk-root> --ninja-exe <absolute-ninja.exe> --windows-cmd-exe <absolute-cmd.exe> --powershell-exe <absolute-powershell.exe> --architecture x64 --configuration Release
```

`Test.ps1`、九个测量工具、vendor 注册和独立 installed Reference 由另一隔离切片实现，必须与本切片真实组合验证。生成项目的 LLVM 构建仍通过独立 installed-SDK 消费者控制器；本标准调用器明确拒绝把它当作源码 SDK/维护模板编译。

新回归候选包括原 caller 的 LLVM 参数/退出证明边界、新 reader 的 profile/实际证明拒绝边界，以及直接提取真实 Ninja 纯函数的 22 个词法、成员和配置范围案例。没有执行任何 Node、Finder、身份脚本、编译器、Native、测试、VM 或 Git；Debug/Release、两架构、可选模块、安装消费者和源码搬移仍须实际验证。

完整源码目录的依赖回归构建使用 `Soui.ps1 build --with-vendor-tests`，或标准调用器的同名选项；它显式传入 `Build.ps1 -WithVendorTests`，与 MSVC 和 LLVM 两个编译配置组合。省略此选项时保持原构建范围。这个开关仅用于完整源码目录，不能与 `--sdk-only` 组合，也不能用于生成的应用目录；在工具准备或启动构建前拒绝这些不完整范围。依赖测试的注册数量不代表实际测试通过，仍须核验该构建的真实 CTest 结果。
