# preview.15 完整源码 ZIP 的独立解压和四配置构建

本轮从提交 `6702e94397f1339c466d86a6f7affdf1f3947703` 的完整源码清单打包，真实 ZIP 创建、压缩测试与 SHA 校验均退出 0。随后解压到新的无 Git 目录，未复用原仓库的构建输出。

| 冻结对象 | 身份 |
|---|---|
| ZIP | `soui-sdk-0.3.0-preview.15-full-source.zip`，248,078,344 字节 |
| ZIP SHA-256 | `4c0255ca2a215e819bbda339d7432fc37398c3cef98b7d234f0411fcd4a4dd92` |
| 完整源码清单 | 34,612 文件，SHA `f1f32413ee6461a0affd9b2accb2ae978e21a3666039a0a85b43ccc9e5da74d9` |
| 同版 SDK Core | 419 文件，manifest SHA `44839f856df8d9542231e7bcd38af4ca9a37663aa8d4e6fb755868faacb076da` |
| 实际应用构建输入 | 717 文件，SHA `b0d7cc7d52de3493084e0dec6813f92f9a90a1bbce2f75ad5a62532a6dd87d07` |

四组 x86/x64 × Debug/Release 构建的原始退出码均为 0。八份 Starter/Workbench 运行载荷和每份 60 个 PE 的架构逐项复核通过；实际 CL/link/Lib 命令没有 vcpkg 路径。子构建禁用环境中的 vcpkg 自动集成和 MSBuild 节点复用，未修改宿主全局设置。重编 EXE 的散列与原仓库产物可以不同，分别绑定各自构建 ID 和完整载荷。

x64 Release 的完整 `Test.ps1 -UnitOnly` 入口及其余三组 CTest 原始退出码均为 0，合计 48 项 CTest。完整源码清单与同版 SDK 包匹配在构建和测试前后均真实复核，原始日志和退出记录保留。

开始解压版 x86 桌面 smoke 前，门禁发现外部 `qemu-system-x86_64.exe`，命令退出 2；没有启动应用，没有继续 x64 桌面或 Win7 客体。该拒绝样本原样保留，不能用原仓库 14 项短测替代这些新编 EXE 的 GUI 验收。

239 份原始记录见 `raw-manifest.json`，汇总见 `raw/review.json`。ZIP、完整解压树和运行二进制保存在报告记录的外部验证目录；此证据目录保留清单、脚本、日志和真实命令结果，不重复提交二进制。

本轮证明该 ZIP 在当前开发宿主可独立解压并编译 SDK、Starter、Workbench。构建使用源码包交付的固定底层依赖；没有在这个解压目录重新执行全部底层源码编译，也未覆盖干净机器、独立业务应用、目标系统 GUI、长稳和性能。`sourceDeliveryVerified=false`、`releaseReady=false` 保持不变。
