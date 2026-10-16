# 0086: LLVM 观察器的本地工具路径拼写边界

状态：Source-only 候选；独立复核、新 SDK 接入、新生产者与完整 SDK 实际运行均待完成。旧失败保留，accepted=false，发布资格为 false。

原始完整构建的 native Build 观察器自然 0/0/Job0；内层 Build 的 429 个历史成员、151 次查询和 loader 序列也自然关闭。后置 PowerShell reader 在首个 POWERSHELL CREATE 失败：Win32 句柄给出的 imageFile.finalPath 是 `\\?\C:\...\pwsh.exe`，而原 Tool-FileIdentity helper 明确只接受普通盘符路径。失败发生在创建 ninja-commands.txt 前。路径拼写错误不能被改判成旧 SDK 构建成功。

只在 Observe-LlvmBuild 的 ToolIdentity 和 ResourceWrapperIdentity 两个调用边界增加 LocalToolSpelling。它接受普通绝对盘符工具路径并保持原值；对实际 Win32 `\\?\<drive>:\...` 形式只去掉固定四字符前缀。扩展拼写禁止正斜线。UNC、设备命名空间、相对路径、ADS、控制字符、通配符、空/点/父目录组件、尾点/空格和保留设备组件均拒绝。这个函数只转换字符串，不证明文件身份，也不按工具 basename 放行。

转换后的路径继续交给原 Assert-ToolFileIdentity。helper 字节不变：普通路径、普通文件及祖先、双句柄 FileID/最终路径、长度、SHA 和读取前后稳定性检查全部保留；CREATE 的固定 role、SHA、bytes 与 AMD64 machine gate 不变。缓存仍使用原 raw observed key，只有原身份校验成功后才把原始和检查拼写登记为完整 frozen profile 路径 aliases。原 CREATE、导入证据和 raw command 文本不会被覆盖。

ResourceWrapperIdentity 有相同调用边界，因此使用同一字符串转换，保留 wrapper pin、完整依赖封闭与身份校验。helper 的导入位置仍是 PSScriptRoot 下固定 sibling，并须与 frozen profile 及 BridgePin 匹配；不把任意 helper 或 provider 引入本片。

Source 纯回归从真实 reader AST 提取完整新函数，从原 helper AST 提取原始拒绝条件，以原失败的首个 CREATE 证明旧 RED、新模型 GREEN。身份检查用惰性模型代替，不运行 helper、Add-Type、native FileID 或完整 Reader actor；负例覆盖路径命名空间、ADS、控制/异常组件、物理身份拒绝、wrapper 依赖 gate 和原 CREATE 的 SHA/bytes/machine gate。逐字重建旧文件确认三个函数之外全部字节相同。原 Ninja parser regression 仍以真实 AST 纯函数运行。

Role、default graph、编译数据库、RC/res、MT、COFF archive/link、SDK 和 Job gate 不变。后续必须由 Root 独立 Source peer 接受，再产生新 Source/MF/SDK producer 与完整 SDK 实际记录。不得把新函数应用到旧结果并改写 accepted=false，也不得把模型绿色称为 native 文件身份、SDK 完整成功、兼容性或发布资格。
