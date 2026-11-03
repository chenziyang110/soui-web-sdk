# PR #103 六平台 CI 限定验收（2026-11-03）

本页记录 [PR #103](https://github.com/chenziyang110/soui-web-sdk/pull/103) 的固定源码及已实际结束的 CI，不作为其他提交或完整生产资格的通过证明。

- commit：`b446379ec6059ec32057fa544a5556720f6439fe`
- tree：`58fee7c27a16d7495a0b6fa0e45f8ee54d77f969`
- 父提交：`d8589e90622b1010e3ebf122a0f51a8940ba1799`
- 源码版本：`0.6.0`，PR 中的修改仍未发布；PR 在本次结果核对时为 Draft、open、未合并。
- [运行 41579734678](https://github.com/chenziyang110/soui-web-sdk/actions/runs/41579734678)：`push` 事件，第 1 次 attempt，整体及六个精确 head 的 check 均为 `completed/success`。

本页新增后形成的提交须核对其自身 CI，不能直接继承上述固定 head 的结果。

## 六个平台的实际结果

| runner / 目标 | job | 实际执行范围 | 结果 |
|---|---|---|---|
| windows-2022 / x64 | [125731500267](https://github.com/chenziyang110/soui-web-sdk/actions/runs/41579734678/job/125731500267) | 13 个构建、安装、链接和文件交付阶段；无 CTest 或项目 EXE 启动 | success |
| windows-11-arm / ARM64 | [125731500090](https://github.com/chenziyang110/soui-web-sdk/actions/runs/41579734678/job/125731500090) | 同上，目标为 ARM64 | success |
| ubuntu-24.04 / x64 | [125731500297](https://github.com/chenziyang110/soui-web-sdk/actions/runs/41579734678/job/125731500297) | 16 个执行阶段，包含两项精确无界面 Runtime 测试 | success |
| ubuntu-24.04-arm / ARM64 | [125731500322](https://github.com/chenziyang110/soui-web-sdk/actions/runs/41579734678/job/125731500322) | 同上，目标为 ARM64 | success |
| macos-15-intel / x64 | [125731500367](https://github.com/chenziyang110/soui-web-sdk/actions/runs/41579734678/job/125731500367) | 同上，单一 `x86_64` 目标 | success |
| macos-15 / ARM64 | [125731500343](https://github.com/chenziyang110/soui-web-sdk/actions/runs/41579734678/job/125731500343) | 同上，单一 `arm64` 目标 | success |

执行记录中，上述阶段的退出码均为 `0`，`signal` 和 `errorCode` 均为 `null`。Unix job 另外各有两条 CTest 选择元数据记录，不计为额外测试执行。

六个 job 均完成 Runtime/Desktop SDK 构建与安装、两个安装版消费者的编译链接、安装前缀生成的正常模板编译链接，以及 stage、verify、ZIP archive、解压和再次 verify。`runtime_metrics.h` 与 `view_observation_abi.h` 均实际安装。`ci_installed_headers` 以一个聚合翻译单元编译 22 个安装公共头；这不是 22 个逐头独立自包含测试。安装配置和模板包来源均核对到本轮独立安装前缀；本次未提供逐头依赖转储或链接器文件读取跟踪。

四个 Linux/macOS job 各实际运行并通过：

- `sdk.portable.primitives`：只选择该测试，1/1 Passed，CTest 退出码 `0`。
- `ci.runtime-installed`：只选择该测试，1/1 Passed，CTest 退出码 `0`。

每项选择记录仅含对应 Runtime 可执行文件，没有附加参数或 fixture。Windows job 不注册或运行这些测试；Desktop 消费者及正常应用均未启动。

文件校验通过架构、notices 和完整性检查；Unix 同时校验可执行位。Windows 两包各有 16 个展开文件匹配，四个 Unix 包各有 15 个文件匹配。Windows 日志没有独立 PE Machine 数值转储。ZIP 在 job 内生成并校验，未作为 Actions artifact 上传。

## 首轮失败及修复边界

父提交 `d8589e90622b1010e3ebf122a0f51a8940ba1799` 的[首轮运行 41575151728](https://github.com/chenziyang110/soui-web-sdk/actions/runs/41575151728) 六项均为 failure，原结果保留：两项 Windows 在长路径 checkout 停止；四项 Unix 完成 SDK、安装消费者及各两项 Runtime 测试后，在模板包来源校验停止。详见[首轮记录](pr103-first-ci-20261103.md)。

本轮仅在 checkout 步骤使用进程级 `core.longpaths` 配置，未修改全局或系统策略。模板包校验接受合法 `PATH`/`UNINITIALIZED` 缓存类型，同时保持唯一项及 canonical 安装目录检查。修复后的 Unix 日志实际记录两个包均为 `UNINITIALIZED`，目录均属于本轮安装前缀。Runtime 汇总依据两项真实完成的测试记录计算，不以部分成功覆盖整体失败。

## 未完成项

当前六项不覆盖新 head 的 Windows x86、Linux i686 或 ARM32，也不覆盖 Windows Runtime 执行、任何 Desktop GUI/交互行为、长期稳定性或完整生产验收。macOS 两项是分别构建的单架构结果，不证明 universal 包。旧 Windows x86 构建证据及其他历史结果不转授本页源码；缺失历史资料不重建为成功日志。

四个 Unix job 的 `runtimeNoGuiTestsPassed=true`；Windows 为 `false`。全部 job 仍记录：

```json
{
  "desktopApplicationExecuted": false,
  "releaseReady": false,
  "nativeRuntimeVerified": false,
  "authenticityVerified": false,
  "signed": false,
  "artifactsUploaded": false
}
```

## 摘要来源 SHA-256

以下是整理来源的逻辑文件名及原始 SHA-256，不是附件下载清单。原始报告、完整日志、操作记录、截图及 ZIP 均未随本页附带；执行事实可通过上方公开 CI 链接核对。

| 来源逻辑名 | SHA-256 |
|---|---|
| `closeout.md` | `de4e870e48b9ce10d88fcc1f6aa7b3e9e1f359b0490f807bd4c4f160fd6eb638` |
| `windows-x64-ci-evidence.md` | `55e4c240b17f5547e1844eeeaf9542cab14943bdbe72d5f03375c726bf7450d1` |
| `windows-arm64-ci-evidence.md` | `1cab5d84fcf21812fc88380d85fc36002a7442e3c688373fdfc55390c389f9d1` |
| `linux-x64-job-125731500297-report.md` | `bd2bf7db1ad533f33f9f309085395d8b6553fd57c280bbb0bf6d8aff80a839d6` |
| `linux-arm64-job-125731500322-report.md` | `94b67b5c4593b21d789e8a1aeb01bd3b5eec2a35b2a5ee61dbfd7fd9708f1e89` |
| `macos-x64-ci-evidence.md` | `36f08a61592ca734288011881722a379496d388d2d38100e3ab93eccc8e6afee` |
| `macos-arm64-ci-evidence.md` | `76d4b306370c99aab9abff8fbc5d358b7a991f43a6a085758adda45c6b93242b` |
| `github-final-readback.json` | `1eadf204cd103c5b8a844b101383bf5bcfd426b9f0649d2dfa9479b34d1bd23f` |
