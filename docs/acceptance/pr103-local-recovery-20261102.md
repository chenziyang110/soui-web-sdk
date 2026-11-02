# PR #103 本地恢复与资产范围

2026-11-02 从本机实际存在的源码与验收文件恢复候选。仅本地整合、静态检查和源码清单验证；没有推送、云端运行或发布。本页是公开适用的事实摘要，不是丢失云日志的补写。

## 源码身份

| 用途 | 精确身份 |
|---|---|
| 恢复起点：现存本地 CI 提交 | `32b412b1c8c2bac1f10b9fa2e770170653fc9e41` |
| 起点 tree | `e4f78362f40d25ac5fe21692326b73f6a59077a3` |
| 唯一整合的固定 main | `ce9757e33e52ac680802691ff7e3a042eca86d52` |
| 固定 main tree | `1a9677e676e713c84d98759d1ddd54356f5d557b` |
| 共同祖先 | `0cdc0a9ace0cdce664ec4a9b54e5f8f71b97ba48` |
| 旧 Windows build-only 来源 tree | `98cb4681032285309841293d65f0041344aa5f14`（发布到 PR 的 `19d450eae04d7ee51286168712d5b60c08631e89`） |

本轮合并只在 `docs/known-issues.md` 和 `source-manifest.json` 产生文本冲突。前者保留 main 更新的 V10/V11 历史边界及 PR 的跨平台限制；后者从解决后的完整索引重新生成，不选取任一父版本清单。main 自带的历史原件与双方源码保留，不能因纳入新索引便改称本轮执行结果。

VERSION 仍为 `0.6.0`；更早 PR 身份为 `0.6.0-preview.3`。本地恢复内容不等于已经发布的 0.6.0 资产，后续发布仍须协调新内容版本。当前候选最终 commit/tree 由完成后的 Git 记录和本地收尾报告给出，不能使用下列缺失的旧 SHA 代替。

## 本轮检查和未执行项

- 四个 CI/清单脚本的 `node --check`、CI 静态 validator、Windows ARM64 `--plan` 均退出 0；validator 核对六矩阵、13 个负控和 14 个环境注入。`--plan` 只构造命令，不执行计划。
- 清单使用仓库现有 `source-archive.cjs create` / `verify` 流程。它们会启动 Git 读取索引和 blob、处理文件及哈希；不启动 CMake、CTest 或项目程序。文件数、SHA256 和最终退出码留存本次收尾记录。
- 没有重新配置、编译或运行工程目标；没有运行旧 4551 程序、consumer、模板、doctor、launch 或 GUI。旧 primitives 的 4551 和旧 consumer 原因未知保留。
- 六平台云 CI 尚无由本次工作取得的运行结果。Windows 仍仅 build/install/package；Linux/macOS 的两项无界面 Runtime 设计范围见 [CI 说明](../ci-platform-matrix.md)。静态通过不等于这些步骤实际通过。
- `releaseReady=false`；未将旧成功自动转授新候选，也未关闭 Linux/macOS、ARM64、真实桌面和生产资格缺口。

## 已存在的历史资产盘点

仅盘点本任务的文件，没有收集其他项目、缓存、凭据或内部记录；没有把原始目录整批加入仓库。下列哈希本轮重新读取实际文件核对，未执行包内程序。

| 本任务资料 | 文件数或字节数 | SHA256 / 范围 |
|---|---:|---|
| 10 月 30 日四个 CI evidence 目录 | 128 文件 / 34,311,857 字节 | 静态准备、清单、触发修订、本地提交及远端拒绝；27 项声明文件大小及哈希一致 |
| Runtime build-only 报告 | 10,837 | `bb52dffefc9e379648293e84241468425f46533f685977bbbe4ecf7edb288f8b` |
| Desktop build-only 报告 | 10,885 | `c080951f0ca6acb9182843713038e813a139d64f3333a8b6c629bc1abcbff4f6` |
| 两架构汇总报告 | 11,823 | `7501b00bd91f91412421a838288e7d8bbd54bc7a7402aed2d3b128e7a384cb00` |
| `InstalledDesktopCheck-0.1.0-windows-x86.zip` | 530,377 | `7a0405f884b89849aec8e65928d1dcbca6db2a1c0d537c5d270fcd08fcd45ee0` |
| `InstalledDesktopCheck-0.1.0-windows-x64.zip` | 605,641 | `ee11525199017d5d91946e0eb96150b4a8dd3b5d2df80830057c4a2f4a739062` |

Windows 两包的 sidecar、交付清单、解压完整性日志与包身份一致；每包 20 条目、16 文件。它们仅绑定旧 tree 98cb4681 的 Release build/install/consumer/template/package 证据：没有 EXE/CTest/GUI 运行，Win32 也不代表 32 位硬件原生运行。两包是开发应用 ZIP，不是 SDK 安装包或正式发布；`nativeRuntimeVerified`、`signed`、`dependenciesBundled` 均为 false。

可公开候选为经过范围审阅的补丁、包及其 sidecar/完整性材料；本轮没有复制或上传它们。原报告、命令和 GitHub 读回含机器路径或个人身份，公开副本须先脱敏并另算哈希。原件保留；包内已知路径标记未命中也不等于全面隐私审计。更详细的逐文件盘点单独保存在本地恢复 evidence 目录，使用本任务相对路径。

## 缺失与发布阻塞

- 原 11 月 2 日云端整合目录当前不可访问；原始日志没有恢复，不能编造或由旧报告替代。
- `bd42b51943c6d550cc9551f83f1eebfe5e0fcaff`、`6d64c01fd5ecaa188bddbd2e273833a4bcdd2ce1` 未在当前可访问 Git 对象库找到；父任务的 GitHub 核对也未找到。本轮新提交将使用实际产生的新 SHA。
- 2026-10-30 对现存 CI 提交的普通 push 被 GitHub 明确拒绝：OAuth App 无 `workflow` scope。没有权限变化证据，本轮不重试、不更换凭据或接口、不修改安全设置；电脑恢复可用不解除该阻塞。
- Actions 设置端点此前被连接器 allowlist 拒绝，不能据此认定 Actions 已禁用；本轮不重试这些端点。未发起 dispatch、付费 runner、artifact/cache 上传或 release。

后续先完成公开材料脱敏与候选验收，再由有相应权限的负责人通过正规流程处理远端推送；不得借删除工作流、改路径或改身份绕过已知拒绝。
