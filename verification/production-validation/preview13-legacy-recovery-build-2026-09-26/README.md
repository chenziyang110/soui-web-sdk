# preview.13 已知旧现场恢复与独立业务四配置构建

**已知无 journal 现场在独立副本中手工恢复，原件未改；独立账本项目两轮 x86/x64 × Debug/Release 编译通过，GUI 与完整 V06 尚未通过。** 本轮没有修改 SDK、模板或 Demo 源码，也没有增加版本。

## 输入与恢复边界

当前 SDK 仍为 `0.3.0-preview.13`，714 文件验证输入 SHA-256 `f2ca9f2ca470c5886cda1456b31b4260795667eb4df65d4c679d8acd80f2a788`；源码 SDK 包 419 文件。原 [preview.11 中断现场](../preview11-upgrade-interruption-2026-09-26/README.md)保留 858 个文件，复核 SHA-256 `8af01bd24b70f8f3fe767727f0a8abb0f233a5d0b2f49e7d6badcef5ffb9a790`，恢复和八次构建后原件仍一致。

先逐字节复制原件。新 CLI 的 `recover` 与 `verify-project` 对旧 `.sdk-install-*` 无 journal 状态均退出 1，属于预期拒绝。人工恢复脚本预检整个已知现场：锁匹配旧包、业务文件未变、现存 SDK 字节只允许来自旧/新清单、备份必须逐项匹配且不存在额外文件。随后从副本的备份还原 7 个文件，保留全部 419 个旧事务文件到副本外证据目录；没有需要隔离的新版本专有文件。

旧 preview.10 的全部 417 个 SDK 文件经正式 CLI 校验退出 0；再由外部 preview.13 安装器升级，项目校验退出 0。22 个应用与中文账本数据文件保持原字节。此脚本只接受这一确切已知现场，**不代表通用或自动旧格式恢复**。原件、备份与失败日志均保留，不能靠删除未知事务来继续安装。

## 独立编译结果

项目位于仓库外，业务代码和账本数据沿用原小样。两轮共同编译输入 SHA-256 `a961f978b4a1444f6874f82ed1acb206349ddbcd454f1b8edaf13fa2c1a9c359`。每轮按 x86 Release、x64 Release、x86 Debug、x64 Debug 串行构建；每次正式构建退出 0，输入/锁/22 个业务文件前后保持一致，各自保存完整 71 文件运行载荷，60 个 EXE/DLL 的 PE 架构均匹配。

首轮实际编译和链接命令包含宿主全局 vcpkg 的头文件/库搜索路径及通配库输入。首轮保留为真实环境记录，不把它表述为独立于宿主依赖配置的复现。第二轮另建仅含 442 个项目输入文件的副本，没有复制输出或 node_modules；只为构建子进程设置 `VcpkgEnabled=false`、`VcpkgAutoLink=false`、`VcpkgEnableManifest=false`，没有修改宿主配置。四组实际 CL/link 命令均不再含 vcpkg 路径，重编全部成功。

第二轮运行载荷已另存，后续 GUI 必须先核验完整载荷，不能直接运行可能变化的构建输出：

| 架构/配置 | buildId | 载荷 SHA-256 |
|---|---|---|
| x86 Release | e3e44d33-c8b8-43ff-bbaf-45f7d8018afd | `7bdb422f0da444e6565211e6cc556c8b4743adbd990f8fdfe3ef07c561355803` |
| x64 Release | fabaaf5e-0486-40a2-a321-c996f1db4d32 | `90cb1518b8e6dbc64c1c5121d0856dbba6d83dd8a0623d522fa9f554cb14c45a` |
| x86 Debug | 3442e084-02a3-4187-b946-5a98a05bfd7c | `5746ee647a483498a1fdfa0cfe5081365da3a0cc9e808995f72ad45fd7af2fc6` |
| x64 Debug | afbfe997-16c5-4286-ae49-1dae5d626739 | `35d9b1702ae15acdb7a396452337575248192284aced56cc4c5619defc22c3b4` |

## 证据与未关闭项

`raw/isolated/independent-review.json` 再次核对原现场、保留的事务备份、全部命令退出码与日志散列、业务文件、八份运行载荷、PE 架构和实际编译/链接命令。`raw/legacy/` 保留恢复、首轮构建及 vcpkg 输入；`raw/isolated/` 保留新副本准备、四次重编及实际工具链配置快照；`raw/business/` 是 22 个业务文件的惰性 `.snapshot` 副本。177 个原始文件由 `raw-manifest.json` 绑定，原始二进制和完整项目保留在清单所指外部目录，不重复纳入 Git。归档的脚本保留实际路径用于追溯，不作为可搬移产品入口。

SDK Web 的当次 npm audit 退出 0，漏洞总数 0，仅代表该锁文件依赖审计。原生构建有第三方头文件警告，原日志与诊断代码保留，不声明零警告。

其他 QEMU 运行期间仅执行编译与文件核验，没有启动本轮 GUI。当前 Starter、Workbench、独立公共消费者整体尚未为 preview.13 重验；本业务配置未启用授权续期。Windows 7、真实业务升级四阶段运行、受限账户、完整底层重编、干净机器复现、长稳、IME/DPI、多屏、Electron 对照和维护/授权/发布责任仍待验收。本次关闭 vcpkg 自动接入不证明所有环境依赖已封闭。`sourceDeliveryVerified=false`、`benchmarkValid=false`、`releaseReady=false`。
