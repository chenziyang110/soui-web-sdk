# ADR 0033：标准构建调用者与独立 compiler-tail 接受

## 状态

已接受；实现与本机验证进行中。仅适用于当前 preview 版本，不授予发布或目标系统验收。

## 背景

`Build.ps1` 已在构建开始使旧成功记录失效，完成时绑定源码和实际载荷。通用 owned Job 对编译根进程退出后遗留的子进程仍返回 false/124。ADR 0031 的 reader 和政策可以证明一份精确固定的 MSVC 尾进程被当前 held Job 清理，但需要标准调用者在启动前固定命令、源码、工具和 pin，结束后重新核对实际 bytes。手工填写 reviewedInvocation 或采用旧成功报告不足以授权新构建。

## 决定

新增 `tools/internal/standard-build.cjs`，只构造当前工作区 `tools/Build.ps1` 的标准 PowerShell argv；架构、配置显式且唯一，开关使用严格 CLI 白名单。不存在任意命令、脚本、额外 argv 或 Test/application 调用入口。

每次调用创建新的 UUID 证据目录。执行前先独占取得整个工作区的租约，所有架构和 SDK/application 共享该租约，因为 Web 构建目录、可选绑定输出和前端依赖缓存也由同一工作区共享。已有租约明确拒绝，不根据 PID 猜测失效，不自动删除历史租约；正常持有者只释放自己的租约。旧 build 报告的实际原件、散列和 buildId 在启动前另存，成功必须使用新 buildId。

启动前固定当前 preview VERSION、标准 argv/cwd、绝对 PowerShell/runner 路径及 SHA、Build 和调用链原始字节散列，以及既有规范化 delivery source。无窗口的 owned 只读工具探针取得实际 Get-Command 和 Visual Studio 默认工具集；探针要求通用自然闭合。Build 启动前再次核对源码和所有工具原始 bytes，保存本轮独占 context。只向子环境传入关闭 MSBuild 节点复用的设置，不改变全局配置或 telemetry。

外部工具允许明确解析已有 NVM junction 等启动路径，同时保存 requestedPath、resolvedPath 和实际目标文件 bytes/SHA。读目标前后再次核对解析结果和已打开文件的身份；构建启动前与结束后均重新解析并核对固定记录，即使新目标 bytes 相同，目标路径变化也拒绝。仓库源码、证据、所选 prepared runner 与 tail pin 继续使用拒绝链接的路径规则。该边界允许合法的工具管理器，不声称阻止同权限主体在检查间隔替换链接或改写整套文件。

结束后从实际 Buffer 重新读取完整 native stream/context，核对唯一 start、root-exit、terminal、ready PID 与规范 uint64 FILETIME、argv/cwd/工具散列，拒绝截断、重复字段、身份和 context 漂移。当前源码、脚本、调用链与工具都须仍匹配启动前记录。`build-evidence.verifyBuild` 必须验证本轮 build.json 和实际 payload/library；新 buildId 与 subject 创建时间绑定，CMake 编译器、Visual Studio 实例、v143 与实际 vcxproj SDK 19041 必须符合预先观察。

默认接受要求 root exit 0 且通用 Job 自然 completed。只有显式 compiler-tail opt-in 才传入完整 image/SHA/nonce 组，并调用既有 `acceptCompilerTail`，后者再次从实际 raw 重算证据。只接受政策已审过的 MSVC 14.44.35207 Hostx64/x86 或 Hostx64/x64 精确文件。当前政策不包含 `WithGuiBindings` 参数，因此该组合在启动前拒绝；普通默认构建可使用该开关。

结果分别记录 compilationCompleted、genericSupervisionPassed、closureConfirmed、compilerTailAcceptance 和 accepted；始终 releaseReady=false。窄政策接受可使调用者退出 0，但原始 generic false、supervisor 124、终止与清理记录保持原样。STOP、timeout、未知或不可读成员、spawn/report 错误、fallback、任意 test/application 或测量组不继承该政策。

## 备选方案

- 把 vctip 按名称忽略或修改通用 passed：拒绝，无法绑定实际对象，且会改变运行时退出门槛。
- 构建根进程 exit 0 即接受：拒绝，遗漏当前载荷、源码、工具、旧报告与 Job 闭合。
- 复用手工 reviewedInvocation 或旧 raw：拒绝，本轮必须在启动前固定并生成新证据。

## 兼容性与迁移

Build.ps1 和通用 supervisor 协议不改变；现有人工调试入口仍可使用。受监督的标准构建使用新的 Node 调用者。当前政策仅适用于现有 preview 版本规范，RC/stable 的身份和发布渠道另行设计，不能从这里扩大。最低 CMake 3.24、Windows SDK 19041、v143 与目标系统支持声明不提高。

标准 Test/Collect 在测量准备器合入后，以本轮 selectedArchitecture 的已验证工具配置 Tail native/client/history 组；缺工具、旧源或缺身份不得以 mandatory skip 冒充通过。测量组保持自己的自然闭合要求。

## 验证

纯边界回归使用明确标注的 synthetic Buffer/临时 SDK 载荷，覆盖严格 CLI、互斥选项、租约、旧报告、source/tool/context/产物漂移、重复或截断 raw、默认尾拒绝与 STOP/timeout 拒绝；这些不证明实际 MSVC 构建。真实 SDK 构建由主代理协调，在冻结输入上执行两架构并保留所有失败尝试。无 GUI/VM/正常 SendInput 的工具测试不能替代完整目标系统、长期稳定性或发布验收。

同一用户可重写整套证据或客户端；SHA、nonce、磁盘对象和一致性核验不认证该主体，也不认证执行内存或加载 DLL。租约约束合作调用者，不声称防止同权限程序直接改变缓存或目录。
