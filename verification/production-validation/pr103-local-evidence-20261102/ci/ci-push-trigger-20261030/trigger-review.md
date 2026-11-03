# 历史 CI 验收文本的公开副本

此文件经过删减、脱敏和编码/换行规范化，不是原始字节。仅反映 2026-10-30 的本地静态、提交或拒绝记录；不验证新候选 e43a79，也不构成云端运行通过证据。

- 源逻辑标识：pr103-integration-evidence/ci-push-trigger-20261030/trigger-review.md
- 源原始 SHA256：c325a59ddddec2deae55933d0abe436099a8294abda967e14a353b42d3579c89

# 精确分支 push 触发修订

已修正首次触发缺口：有合并冲突时pull_request不运行，首次workflow_dispatch有默认分支前提。
本版仅以现有PR head分支的精确push作为自动入口，删除PR触发避免双跑，保留dispatch供未来使用但当前不依赖。
官方依据：https://docs.github.com/en/actions/reference/workflows-and-actions/events-that-trigger-workflows
分支/tag过滤依据：https://docs.github.com/en/actions/reference/workflows-and-actions/workflow-syntax#onpushbranchestagsbranches-ignoretags-ignore

```yaml
on:
  push:
    branches:
      - codex/portable-runtime-platform-matrix-20261020
  workflow_dispatch: {}
permissions:
  contents: read
concurrency:
  group: soui-platform-${{ github.workflow }}-${{ github.ref }}
  cancel-in-progress: true
# checkout input:
ref: ${{ github.sha }}

```

原生六runner、contents:read、无secrets/cache/artifact、Windows build-only和公共仓库guard保留。
没有pull_request_target、rebase或main修改，没有提交/push/触发。Actions设置端点400保持停止，没有替代查询。

静态检查：validator语法/策略均0；13负控、14环境注入通过。事件模型只证明配置选择，不是GitHub真实调度。
精确push：模型1轮workflow/6矩阵；main/近似分支/tag/PR/private不选矩阵；默认分支workflow缺失的dispatch模型不可用。

上一版tree：8c052061f27ee62ae9e95b729096f31d430f8ad4
新tree：e4f78362f40d25ac5fe21692326b73f6a59077a3
HEAD：19d450eae04d7ee51286168712d5b60c08631e89
分支：codex/ci-platform-matrix-20261030
unstaged/untracked/unmerged均0；10个累计文件暂存；原发布checkout干净。

.github/workflows/platform-build.yml    | 15 ++++++--------
 docs/ci-platform-matrix.md              | 21 +++++++++++--------
 source-manifest.json                    | 12 +++++------
 tools/ci/validate-platform-workflow.cjs | 36 +++++++++++++++++++++++++++++++--
 4 files changed, 59 insertions(+), 25 deletions(-)

相对上一版只有workflow、validator、对应文档三处源修改和派生source-manifest。
清单create/verify各一次均0，62652项，无增删；SHA256 a3d03f5082a354d9d161a62f95e57f36e2d5206842847d725768bbfeb4dd41a9，releaseReady=false。

ci-trigger-change.patch / base 8c052061f27ee62ae9e95b729096f31d430f8ad4 / 11060 bytes / SHA256 7040768c449c41966e3785e3bbecd79fc3f34b01e50f027835aad1fea2126950 / reverse-apply-check=0
ci-complete.patch / base 19d450eae04d7ee51286168712d5b60c08631e89 / 60237 bytes / SHA256 030d971ef99538f5f8cd52301748e53fa3cf81d781fc86842ecda395efd3fa2f / reverse-apply-check=0

上一版ci-closeout.patch/closeout-review.md已逐SHA确认未变；完整逐文件/blob身份见final-state-and-diff.json。
