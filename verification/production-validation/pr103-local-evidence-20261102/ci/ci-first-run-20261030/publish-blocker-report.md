# 历史 CI 验收文本的公开副本

此文件经过删减、脱敏和编码/换行规范化，不是原始字节。仅反映 2026-10-30 的本地静态、提交或拒绝记录；不验证新候选 e43a79，也不构成云端运行通过证据。

- 源逻辑标识：pr103-integration-evidence/ci-first-run-20261030/publish-blocker-report.md
- 源原始 SHA256：6d9b4b47b8369b858b5cebc7852edcedb8c03aee2ac6264ea92d002303680ca6

# 首轮 CI 发布阻塞

本地提交成功，普通 fast-forward push 被 GitHub 服务端明确拒绝。已只读确认目标分支未变化，没有重试、替代写入或 dispatch。

- 本地 commit：32b412b1c8c2bac1f10b9fa2e770170653fc9e41
- 本地 tree：e4f78362f40d25ac5fe21692326b73f6a59077a3
- 本地分支：codex/ci-platform-matrix-20261030；工作树干净。
- 远端目标：chenziyang110/soui-web-sdk，refs/heads/codex/portable-runtime-platform-matrix-20261020。
- 远端 head 仍为19d450eae04d7ee51286168712d5b60c08631e89；远端 tree 98cb4681032285309841293d65f0041344aa5f14。
- PR103仍Draft/open/unmerged，正文前后相同。

精确已执行动作：git push --porcelain https://github.com/chenziyang110/soui-web-sdk.git 32b412b1c8c2bac1f10b9fa2e770170653fc9e41:refs/heads/codex/portable-runtime-platform-matrix-20261020
2026-10-30 09:43:41–09:43:47 UTC，exit1，remote rejected：

> refusing to allow an OAuth App to create or update workflow `.github/workflows/platform-build.yml` without `workflow` scope

这是GitHub对当前HTTPS OAuth凭据的workflow写权限拒绝，不是本地静态检查失败，也不是Actions开关状态证据。返回未给出OAuth App名称，不能据helper名称猜测具体应用。

commit于09:42:55–09:42:56 UTC退出0，沿用冻结HEAD现有<COMMIT_IDENTITY> author/committer，仅进程环境，没有修改全局身份、hook或签名配置，没有新凭据/交互授权。
Git ls-remote读回退出0，并由GitHub PR及Git Commit API独立确认远端仍旧head/tree。推送结果已确定：本地提交完成，远端ref写入被拒。工作流尚未发布，因此首轮CI未能启动，没有可验证run链接；没有把0checks当作成功。

〔操作指引已从公开副本省略；历史结果不变。〕本轮没有发起授权、改仓库/账号安全设置、换接口推送、改workflow路径或以其他分支规避。
GitHub官方scope说明：https://docs.github.com/en/apps/oauth-apps/building-oauth-apps/scopes-for-oauth-apps
〔操作指引已从公开副本省略；历史结果不变。〕https://docs.github.com/en/apps/oauth-apps/using-oauth-apps/reviewing-your-authorized-oauth-apps

被停止的Actions permissions/workflows列表端点未访问；未运行本地工程程序、旧4551目标、GUI或CTest；未触发付费资源或上传日志/ZIP；没有合并PR或release。

原始日志和机器可读证据：commit-result.json、push-result.json、push.stdout.log、push.stderr.log、remote-readback.json、github-readback.json。
