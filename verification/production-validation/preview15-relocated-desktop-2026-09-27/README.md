# preview.15 无 Git 解压源码的独立桌面短测

本记录补齐[完整源码四配置构建](../preview15-full-source-build-2026-09-27/README.md)之后的新应用启动验证。测试使用源码 ZIP 独立解压、无 `.git` 目录后重新编译的 EXE，不复用原仓库应用成功记录。

输入为 `0.3.0-preview.15`，717 文件构建源码 SHA `b0d7cc7d52de3493084e0dec6813f92f9a90a1bbce2f75ad5a62532a6dd87d07`。原 ZIP SHA `4c0255ca2a215e819bbda339d7432fc37398c3cef98b7d234f0411fcd4a4dd92`，完整源码清单 SHA `f1f32413ee6461a0affd9b2accb2ae978e21a3666039a0a85b43ccc9e5da74d9`。原 ZIP 没有因后续证据增加而重新打包。

| Release 架构 | 独立构建 ID | 原生结果 |
|---|---|---|
| x86 | `ecb09cdb-d13c-405c-87ec-92277f09c092` | Starter、Workbench 默认、Workbench React 各退出 0 |
| x64 | `f6a5320a-1e7c-48bb-88b5-aa810661e191` | Starter、Workbench 默认、Workbench React 各退出 0 |

六项均有 `application-ready` 和 `host-close-complete`。完整运行载荷、资源和 legal 文件绑定对应构建；开始、运行期间和结束门禁均未观察到其他桌面负载。x86 执行时间为 UTC 2026-09-26 18:05:08–18:05:22，x64 为 18:05:22–18:05:33，串行执行。

最初 `x86-apps-attempt1` 因外部 QEMU 运行返回 2，没有启动应用。原拒绝记录继续保留；本次另用 `x86-apps-attempt2` 和 `x64-apps-attempt1`，两个实际子命令均退出 0。`raw/review.json` 记录逐项载荷散列，`raw-manifest.json` 索引 31 份原始文件。

这些结果证明指定开发机上、独立解压并重新编译的六个应用入口可短时就绪和完整关闭。仍使用随包固定依赖，不证明干净机器、解压目录中全部底层重新编译、Win7、长稳、真实续期或性能对标；`sourceDeliveryVerified=false`、`realRenewalVerified=false`、`releaseReady=false`。
