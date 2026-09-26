# preview.13 完整源码与 SDK 包的许可证身份差异

**真实源码解压预检失败，尚未进入原生构建。** 完整源码包的内部清单及 ZIP 校验通过，不代表它与同版已安装 SDK 的输入一致；本次明确保留这一失败。

原 `independent-build-attempt2-full-source.zip` SHA `7ddafb54fdf2bb41f0aa3faeaad72d74b6de018d6e7435c307b803866dcc64b5` 含 33,103 文件（清单 33,102 项）。实际解压到无 Git 新目录后，全源码清单校验通过，但编译输入断言退出 1。同版正式 SDK `verify-source` 也退出 1，指向 `third_party/licenses/element-plus-2.11.1-LICENSE.txt`。

714 个编译输入中，仅该许可证不同：工作区/419 文件 SDK 包为 1,098 字节 CRLF，SHA `7d6560ca47099f18ae7dc4d2fcdb3e0dcc207430a442525177f8af6dc229d1cc`；Git blob/旧源码包为 1,077 字节 LF，SHA `0790118bb4d66681db1d63181f72ef68e632d632f6db0373ef87cf328561af27`。解压输入 SHA 为 `22c6f0bfa6bb29c8aca7c5723e7c6bf4fa1a24dd0877c67c2c8d2fd1a4c472a2`，不能冒充原 `f2ca9f2ca470c5886cda1456b31b4260795667eb4df65d4c679d8acd80f2a788`。旧 ZIP 和失败解压目录保留，不覆盖，也不把这次失败记成编译成功。

Git 原先仅对三个已枚举许可证设置 `-text`，遗漏 Element Plus。SDK 对整个许可证目录使用原字节身份，故修正 Git 规则覆盖整个目录，保留现有 SDK 的 CRLF 字节；不修改许可正文、SDK 实现或已冻结的 419 文件 SDK 包。新增回归使用仓库真实属性及 `core.autocrlf=true`，验证已有和未来许可证在 Git checkout/无 Git 导出后字节及输入身份不变：修正前 1 项失败；修正后交付身份测试 8/8，通过的同版 SDK `verify-source` 退出 0。

回归文件改变验证输入，当前仍 714 文件，新 SHA `a1007f2188102dfa4de4e79f73a4e0b2c2b8b99cf3e892a3d1e1e3794fd51e68`。先前业务四配置构建保留原 `f2ca...` 输入；新源码包必须在新目录重新解压、校验同版 SDK 并编译，不能直接改旧失败目录或放宽散列断言。完整底层重编、GUI 与生产门槛未通过。

另外保留前一轮打包环境失败：Node 启动的 Windows PowerShell 5.1 继承 Core 模块路径时找不到 `Get-FileHash`，压缩及 CRC 检查已完成但整个命令退出 1；相同启动方式的环境诊断也退出 1。仅在重试子进程设置兼容的模块目录后诊断与打包退出 0，宿主配置未改，旧包不覆盖。这一环境修正不能修复上述许可证身份差异。

47 个原始文件见 `raw-manifest.json`，包含 ZIP 校验、模块环境、完整源码预检失败、714 文件差异、回归先失败后通过及正式 SDK 比对。原始大文件保留在清单所指外部目录。`sourceDeliveryVerified=false`、`benchmarkValid=false`、`releaseReady=false`。
