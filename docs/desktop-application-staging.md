# 统一桌面应用目录 staging（未发布）

此入口把一个已构建的应用二进制和一份前端资源放入可搬移目录，不是 SDK 开发
ZIP、安装器、签名发布或运行兼容性认证。无需重新构建 SDK，也不运行输入程序。
输入应为可信、复制期间不可变的应用产物；不要把含秘密的源码根当作前端目录。

## 直接使用

```sh
node tools/stage-desktop-app.cjs \
  --binary path/to/soui_desktop_example --assets examples/unified-desktop/web \
  --output path/to/new-output --name soui-example \
  --platform linux --arch x64 --version 0.1.0
```

Windows 使用 `--platform windows` 和 PE 程序；macOS 使用 `--platform macos`
和 thin Mach-O 程序，并传入 `--identifier org.example.app`。版本是应用自身版本，
不是 SDK VERSION；工具不修改 SDK 版本或许可。二进制头的平台和架构必须与参数
相符，未知格式、未支持组合及 universal/fat 输入明确拒绝，不靠文件扩展名推断。
ELF 只接受 Linux/System V ABI 的可执行入口，动态 PIE 要求 PT_INTERP，静态 PIE
保守拒绝；Mach-O 必须声明 macOS 平台，最低系统版本从载荷读取且不低于12.0。
PE 只接受 Windows GUI/console 子系统，不接收 DLL、驱动或 EFI 映像。

输出目录必须不存在。工具拒绝 symlink、特殊文件、非法可搬移名称、大小写冲突
及输入/输出的危险嵌套。前端必须含根 index.html；复制限制为每文件256 MiB、
资产总计1 GiB、至多10000个目录/文件和64层目录。HTTP服务自身仍有8 MiB
单文件上限。失败留下不含最终 manifest 的不完整目录，不自动删除输入或输出。
它不是防恶意并发修改的文件系统沙箱。输出文件散列用于
核对复制产物，不替代代码签名或供应链真实性证明。

- Windows：程序与 `web` 同级，需目标机已有兼容 WebView2 Runtime 和 VC runtime。
- Linux：程序与 `web` 同级，提供相对自身位置的启动脚本；不修改全局环境或通过
  LD_LIBRARY_PATH 掩盖缺失库，仍需兼容发行版 ABI、GTK/WebKitGTK 及其 helper。
- macOS：`<name>.app/Contents/MacOS/<name>`、`Contents/Resources/web` 和 Info.plist。
  系统 WebKit 不打包；最低系统要求、签名、公证和 quarantine 行为须目标机验收。

附带依赖说明是部署要求，不是动态依赖扫描或“目标机依赖已满足”证明；工具不会
执行 ldd、自动下载安装运行库、增加权限、改变系统安全设置或给应用签名。

## CMake 接入

`find_package(SouiDesktop CONFIG REQUIRED)` 后，或仓库内启用 Desktop 后：

```cmake
soui_stage_application(TARGET my_app NAME my-app
  ASSETS "${CMAKE_CURRENT_SOURCE_DIR}/web"
  OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/app-$<CONFIG>"
  VERSION "1.0.0" IDENTIFIER "org.example.myapp")
```

显式构建 `my_app_stage`。它先构建应用，调用已安装 SDK 附带的工具，使用框架
已核验的目标平台/架构，不加入默认 ALL；只有使用此函数才要求 Node。再次调用
同一已有输出会失败，请选择新目录，不自动删除或覆盖交付物。示例设置
`SOUI_DESKTOP_STAGE_EXAMPLE=ON` 后提供 `soui_desktop_example_stage`。

## 不依赖启动目录的资源定位

公共 `application_resource_directory()` 返回 `{status, value}`，使用实际运行
程序位置查找默认 `web`；macOS bundle 使用 Resources/web。统一示例未传入
显式资源根时调用此函数，不使用 argv[0]、PATH 或 cwd 作为失败回退。
资源缺失或无法定位时明确报错；应用仍可显式配置 `options.resource_root`。
此 helper 只定义目录约定，不自动启用 HTTP 资源服务或更改 Bridge 权限。
示例可直接传 `--resource-server` 使用默认资源目录和 HTTP 模式，无需绝对路径。

## 本增量验收范围

按代码优先节奏，仅做 Linux 构建、实际目录 staging、资源定位以及必要非法
输入检查。Windows/macOS 产物布局代码不等于目标机运行通过。完整的搬移后
GUI 启动、快捷方式/Finder 启动、依赖部署、各架构、签名和安装器验收延后。
详见 [ADR-0126](adr/0126-relocatable-application-staging.md)。

## 不依赖 Node 的运行依赖诊断

每份应用目录新增 doctor 入口并写入 manifest。Linux 使用 `./launch.sh --doctor`
或 `/bin/sh doctor.sh`，macOS 使用 `/bin/sh doctor.sh`，Windows 使用
`powershell -NoProfile -File doctor.ps1`。Windows 执行策略阻止脚本时不要自动
放宽策略，应由部署者按已有组织策略使用诊断工具。脚本不执行应用或 ldd，
不下载安装软件、不提升权限、不记录环境变量值和绝对用户路径。

Linux launcher 正常启动也先执行诊断；明确平台不兼容等
已知阻塞返回1并停止。unknown 不阻止启动，也不被计为依赖满足。退出0只表示
没有识别到已知阻塞，绝不是 GUI、ABI 或系统依赖完整性验收。直接执行应用
二进制仍使用原有启动流程；应用自己的输出不由 doctor 捕获或过滤。

Linux 库缓存只用于寻找 GTK/WebKit 候选，不解析目标的完整依赖图；常见系统
目录下找到 helper 也不证明引擎使用同一位置。未找到时可能是缺失、定制布局
或检查能力不足，应核对目标发行版的 GTK/WebKitGTK 完整运行包和系统 helper。
不要仅复制共享库或改安全开关来冒充安装成功。显示环境未设置也可能使用 Wayland 默认连接，故只报告 unknown；变量已设置
也不证明可以连接显示服务，最终仍以原生初始化结果为准。

Windows 注册表仅发现 Evergreen Runtime 候选，不能覆盖固定版本 runtime 或
证明位宽匹配；VC runtime 和应用额外 DLL 要求仍需目标机核验。原生 WebView2
后端使用官方运行时发现 API，对明确缺失给出专用错误和部署建议；动态库导致
程序本身无法加载时应使用外部 doctor/系统错误，而不能依赖 SDK 自诊断。

macOS 根据实际系统版本和产物最低版本比较，系统框架可能存放在 dyld cache，
不能因某个磁盘文件不存在就误报缺失；签名、公证、quarantine 和第三方 dylib
不由此脚本验收。诊断只输出固定状态行和下一步建议，默认不写持久日志；需要
反馈时可显式将 doctor 输出重定向到自行选择的文件。

## 交付目录完整性校验

```sh
node <SDK前缀>/share/SouiDesktop/tools/verify-desktop-app.cjs \
  --input <已staging目录> --platform linux --arch x64
```

仓库内也可运行 `node tools/verify-desktop-app.cjs`。预期 platform/arch 可省略，
但省略不代表跳过二进制与 manifest 一致性校验。工具只读，成功退出0，校验失败
非零；stdout 为结构化 JSON，不执行程序、doctor、动态加载器或自动修复文件。
不会写入报告或修改原目录，需要保存时由调用者显式重定向到包外。

校验清单布局、所有文件大小/SHA-256、必要文件、实际二进制平台/架构与最低系统
元数据；旧 /1 清单未声明 diagnostics 时不要求 doctor，若声明则必须完整一致。
拒绝重复/非法路径、大小写碰撞、symlink、特殊文件、额外未列文件和
多余目录，以及缺失或改变的内容。清单自身不包含自身摘要。POSIX主机可检查
Linux/macOS程序和脚本的 owner 执行位；不验证 ACL、noexec 挂载或实际执行。
在不支持这些权限语义的主机上不能作相同承诺。
输入目录必须静止且可信，检查不提供对恶意并发修改的完整文件系统沙箱。

自附 SHA-256 manifest 不是数字签名！文件和清单同时被替换仍可能通过。JSON
结果明确不提供真实性认证或原生运行验收，不证明来自某位开发者、没有恶意
内容、依赖齐全或 GUI 可运行。此工具不能代替签名、公证或可信分发通道。

CMake 的 `my_app_stage` 创建完成后自动校验；单独的 `my_app_verify_stage` 只
核对已有目录，不构建程序、不再次 staging，也不清理失败产物。选择的配置和
APP_STAGE_DIRECTORY必须与原产物一致。旧create-only覆盖保护继续有效。

## 生成可分发归档（不发布）

```sh
node <SDK前缀>/share/SouiDesktop/tools/archive-desktop-app.cjs \
  --input <已staging目录> --output <新的交付目录> --platform linux --arch x64
```

仓库内入口为 `tools/archive-desktop-app.cjs`。归档前必须通过已有 verifier；
工具不会执行应用或 doctor。交付目录包含一个 ZIP、对应 `.sha256` 和最后写入
的 `delivery-manifest.json`。输出父目录须存在、目标必须不存在，不能位于输入
内或包含输入；不覆盖、不自动删除失败产物，不自动上传、发布或签名。

ZIP 使用 UTF-8、store模式（不压缩），不依赖额外 zip/tar 程序或第三方 Node
包。每份归档有一个应用名/版本/平台/架构组成的根目录，保留 .app 层次和 Unix
执行权限（不传播 setuid/setgid/sticky）。ZIP32容量或条目数超限直接失败，不
静默截断。Windows主机不能可靠保存Linux/macOS执行位，因此明确拒绝该跨主机
归档组合；应在POSIX主机打包这些目标。不同解压器可能忽略Unix权限，目标机
解压后仍应重新校验和运行验收。

写入时重新核对每个文件的大小/SHA-256并计算CRC，完成后回读ZIP结构、条目、
内容散列和权限，再写最终交付清单。输入必须在过程中保持不变，这不是抵抗
恶意并发替换的文件系统沙箱。SHA-256及交付清单不是数字签名；攻击者同时换掉
归档和清单仍可能通过。交付结果明确不提供真实性或原生运行认证。

已有 CMake 调用增加独立 `<target>_archive_stage` 目标，它只读取既有staging
目录，不隐式重建或重新staging。默认交付目录为原staging路径加 `-delivery`，
可通过 `soui_stage_application(... ARCHIVE_OUTPUT <新目录>)` 指定。
完整顺序是先构建 `<target>_stage`，再构建 `<target>_archive_stage`；后者失败
不删除原应用目录。重复生成交付物应选择新输出，不能绕过create-only保护。
