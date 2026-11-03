# PR #103 历史验收文本归档（本地准备）

本目录保存24份脱敏文本副本：CI 20份、Windows 4份；另有本索引和[逐文件清单](MANIFEST.json)。原件未修改，逐份原始哈希与副本哈希、脱敏及删减范围分开记录。路径是来源的逻辑标识，不是可访问的原件链接。

恢复基线为e43a79c6b5f6a3dce509fbf4c834952b64bf4985 / tree 9a5e9c13e7afc888bc982879aadbc79c5cadf929。归档不验证该候选，最终归档提交身份由实际Git记录给出。releaseReady=false；这些材料尚未保存到远端。

CI资料保留2026-10-30各阶段的原时间与范围：初稿静态准备、收尾、触发修订、本地提交及GitHub缺workflow scope的明确拒绝。初稿中“尚未提交”等表述仅代表该历史阶段，不是当前状态；没有据此获得云端通过结果。

Windows资料绑定旧tree 98cb4681032285309841293d65f0041344aa5f14，仅Release build/install/compile/link/consumer/template/package。没有EXE/CTest/GUI运行；旧primitives 4551和consumer原因未知保留，Win32不代表32位硬件原生运行。

## 副本索引

| 副本 | 来源阶段/组 |
|---|---|
| [ci/ci-first-run-20261030/commit-result.json](ci/ci-first-run-20261030/commit-result.json) | CI历史静态/发布记录 |
| [ci/ci-first-run-20261030/github-readback.json](ci/ci-first-run-20261030/github-readback.json) | CI历史静态/发布记录 |
| [ci/ci-first-run-20261030/publish-blocker-report.md](ci/ci-first-run-20261030/publish-blocker-report.md) | CI历史静态/发布记录 |
| [ci/ci-first-run-20261030/push-result.json](ci/ci-first-run-20261030/push-result.json) | CI历史静态/发布记录 |
| [ci/ci-first-run-20261030/push.stderr.log](ci/ci-first-run-20261030/push.stderr.log) | CI历史静态/发布记录 |
| [ci/ci-first-run-20261030/push.stdout.log](ci/ci-first-run-20261030/push.stdout.log) | CI历史静态/发布记录 |
| [ci/ci-first-run-20261030/remote-readback.json](ci/ci-first-run-20261030/remote-readback.json) | CI历史静态/发布记录 |
| [ci/ci-platform-closeout-20261030/closeout-review.md](ci/ci-platform-closeout-20261030/closeout-review.md) | CI历史静态/发布记录 |
| [ci/ci-platform-closeout-20261030/final-policy.stdout.json](ci/ci-platform-closeout-20261030/final-policy.stdout.json) | CI历史静态/发布记录 |
| [ci/ci-platform-closeout-20261030/final-state-and-diff.json](ci/ci-platform-closeout-20261030/final-state-and-diff.json) | CI历史静态/发布记录 |
| [ci/ci-platform-closeout-20261030/manifest-delta.json](ci/ci-platform-closeout-20261030/manifest-delta.json) | CI历史静态/发布记录 |
| [ci/ci-platform-preparation-20261030/final-static/static-commands.json](ci/ci-platform-preparation-20261030/final-static/static-commands.json) | CI历史静态/发布记录 |
| [ci/ci-platform-preparation-20261030/final-static/static-policy.stdout.json](ci/ci-platform-preparation-20261030/final-static/static-policy.stdout.json) | CI历史静态/发布记录 |
| [ci/ci-platform-preparation-20261030/github-preflight.json](ci/ci-platform-preparation-20261030/github-preflight.json) | CI历史静态/发布记录 |
| [ci/ci-platform-preparation-20261030/local-state-and-diff.json](ci/ci-platform-preparation-20261030/local-state-and-diff.json) | CI历史静态/发布记录 |
| [ci/ci-platform-preparation-20261030/preparation-report.md](ci/ci-platform-preparation-20261030/preparation-report.md) | CI历史静态/发布记录 |
| [ci/ci-push-trigger-20261030/final-state-and-diff.json](ci/ci-push-trigger-20261030/final-state-and-diff.json) | CI历史静态/发布记录 |
| [ci/ci-push-trigger-20261030/manifest-delta.json](ci/ci-push-trigger-20261030/manifest-delta.json) | CI历史静态/发布记录 |
| [ci/ci-push-trigger-20261030/trigger-policy.stdout.json](ci/ci-push-trigger-20261030/trigger-policy.stdout.json) | CI历史静态/发布记录 |
| [ci/ci-push-trigger-20261030/trigger-review.md](ci/ci-push-trigger-20261030/trigger-review.md) | CI历史静态/发布记录 |
| [windows/build-only-report.md](windows/build-only-report.md) | Windows历史build-only |
| [windows/desktop-build-only-report.md](windows/desktop-build-only-report.md) | Windows历史build-only |
| [windows/multi-architecture-report.md](windows/multi-architecture-report.md) | Windows历史build-only |
| [windows/multi-architecture-summary.json](windows/multi-architecture-summary.json) | Windows历史build-only |

## 处理与排除

副本统一UTF-8无BOM与LF。机器路径、个人身份及非必要账户/执行记录已去除或用稳定占位符替代；JSON字段投影、段落/源码附录删除等均逐份披露。技术错误、退出码、版本、相对源码路径及证据SHA不能用脱敏来改写。某些副本因此是明确删减视图，不是原字节拷贝或可直接执行脚本。

选定清单另有9项排除：2个二进制ZIP、4份源码patch及3份工作流源码/视图快照。后两类已由Git保留源码且含执行指令，本次不重复作为验收文本；详情及原始身份见清单。大历史manifest和其他未选定材料本轮未读取、未归档。未归档截图、其他项目、缓存或凭据。

原11月2日云整合目录与原始日志，以及bd42b51943c6d550cc9551f83f1eebfe5e0fcaff、6d64c01fd5ecaa188bddbd2e273833a4bcdd2ce1仍缺失，没有重造。复制旧资料不能恢复这些对象或产生新运行通过。

本次仅本地文件整理、秘密/身份模式检查、内容复核、Git字节核对和源码清单验证；不运行项目程序、不推送或上传。既有workflow权限拒绝仍有效。审阅范围限这里的24份文本及元文件，不扩大为全部历史128份材料或二进制包的全面审计。
