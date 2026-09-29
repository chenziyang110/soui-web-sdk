# 内核维护与发布准入核对

日期：2026-09-23。状态：初步来源与配置核对，生产准入未通过。关联：[完整方案](../roadmap/0.3-sdk-validation.md)、[安全报告流程](../../SECURITY.md)。本文不提供漏洞利用步骤，也不把源码版本识别等同于完整二进制安全评估。

2026-09-27 更新：[旧 libxml 数值格式化 C4789 的有限诊断](../../verification/production-validation/kernel-number-format-diagnostic-2026-09-27/README.md)在 x86 提取函数上完成四种常规区域设置、833,668 次边界和随机输入，实际 ASan 编译/运行退出 0、未报告越界。它不是完整依赖或内核插桩，也不覆盖自定义区域设置；警告仍待审查，整体安全状态不变。

## 已核对的来源

| 对象 | 本地证据 | 已知事实与边界 |
|---|---|---|
| Miniblink 导入 | `third_party/sources/miniblink/SOURCE-MANIFEST.json` | 来源为 `Alberl-Lee/miniblink49-VS2022`，基线提交 `e0a58c04a5ae2a003285fcf26556014f7ceb7037`；包含工作树修改，不是未经修改的上游发布 |
| 导入后的补丁 | `third_party/patches/miniblink/` | 生命周期、动态绑定 ABI、GPU 与 HTTPS 身份校验保留六份独立补丁；preview.4 固定 DLL 与本次重编译候选散列一致。这些修补不代表完成漏洞维护 |
| JavaScript 引擎 | `third_party/sources/miniblink/v8_5_7/include/v8-version.h` | 源码宏为 5.7.246.0；不能仅用“Chrome 49 兼容”描述整个内核的确切组成 |
| TLS 源码 | `third_party/sources/miniblink/node/openssl/openssl/include/openssl/opensslv.h` | 版本标记为 OpenSSL 1.0.2h；现有依赖构建入口选择此 OpenSSL 工程。仍需结合链接配置与产物确认完整组成 |
| HTTP 源码 | `third_party/sources/miniblink/third_party/libcurl/include/curl/curlver.h` | 版本标记为 libcurl 7.61.1；网络配置及错误路径需要独立真实测试 |
| 精确二进制授权 | `third_party/manifests/license-review.json` | 现有状态是 `unverified`，已绑定 preview.14 重编的 x86/x64 DLL；完整链接组成与再分发要求仍须维护者审阅 |

[两架构旧固定 DLL 的直接 PE 导入记录](../../verification/production-validation/kernel-imports-2026-09-25/README.md)对应旧 x86 SHA-256 `3b9071413f704958fb5708b8bb065785a6c3ed03b7fd0fb04f5952201e5fe4f7`、旧 x64 SHA-256 `0801aa929336168c851df4a3f245f5984a88bd01af585aea752404fa07774816`，分别记录 17、19 个直接导入 DLL 名称。[当前重编身份](../../third_party/manifests/kernel-build-preview14.json)已变为 x86 `2ed793ccccb0642df9916128e36aece733efd34ad6acb4e50caa0622a4c25ef3`、x64 `fe4160a70587d027504c409b8fd2bc5c87d78392811bff0c969f69c41a44d951`；旧导入记录不能直接证明当前二进制的导入、静态链接或延迟/运行时加载。

[旧固定 DLL 的链接参数记录](../../verification/production-validation/kernel-link-inputs-2026-09-25/README.md)从旧构建的双架构 MSBuild 日志回放提取各 36 个库参数。旧 x86 显式传入 `openssl.lib`，旧 x64 没有显式传入；这不是 OpenSSL 在 x64 中缺席的证明，也不能将该参数列表无条件继承给当前重编 DLL。当前[双架构构建记录](../../third_party/manifests/kernel-build-preview14.json)证明了新产物身份，仍缺与其精确绑定的完整链接参数、默认库指令和实际链接成员清单。

[源码声明材料定位](../../verification/production-validation/kernel-notice-inventory-2026-09-25/README.md)把旧 DLL 的显式库参数与源码树中可见的候选声明文件及 SHA-256 对齐，并标出若干库源码根目录缺少顶层许可证/声明文件的事实。它可作为当前候选的核对线索，但未绑定新 DLL 的完整链接成员，也未穷尽嵌套组件、运行载荷和再分发条款；二进制授权仍为 `unverified`。

进一步核对发现导入源码内已有 `libxml/src/Copyright`、`libpng/png.h` 的许可正文及 `node/LICENSE-nodejs` 的 c-ares 段落。导入的 libcurl、libjpeg、OTS 文件则指向源码根目录未提供的 `COPYING`、`README`、`LICENSE`；三者的[版本匹配上游原文已补录](../../verification/production-validation/kernel-notice-recovery-2026-09-25/README.md)。嵌套组件材料、实际链接成员与运行包声明仍待完成，不能根据头文件注释自行代写。

2026-09-28 对当前 preview.15 精确 DLL 另作[链接参数和运行包声明观察](../../verification/production-validation/license-runtime-notices-2026-09-28/README.md)：x86 真实链接命令显式列出 `openssl.lib`；四份 Release Starter/Workbench 的 `resources/legal/` 各有五个文件，均未包含导入源码中 OpenSSL 1.0.2h 的 OpenSSL/原 SSLeay 许可文本。原始脚本与独立校验退出 0、当前 DLL/文件散列已保留。此结果不证明静态归档成员最终进入 DLL，也不构成法律结论；x64 无显式参数同样不能证明缺席。实际链接解析、适用条款、随包声明及维护者审批继续按 [Issue #39](https://github.com/chenziyang110/soui-web-sdk/issues/39) 验收，`license-review.json` 保持 `unverified`。

2026-09-29 的[匹配 x86 DLL/PDB 成员检查](../../verification/production-validation/license-pdb-members-2026-09-29/README.md)把当前精确 x86 `node.dll` 与 PDB 的 GUID/Age 对齐，并列出 501 个来自 `openssl.lib` 的编译单元。这补强了 x86 成员级证据；精确 x64 `miniblink.dll` 没有 CodeView，采集时无匹配 PDB，不能沿用 x86 结论。四份运行包仍缺对应声明，适用再分发条件和维护审批未完成，依赖授权保持 `unverified`。

OpenSSL 官方将 1.0.2 列为已结束常规支持的分支，另有扩展支持渠道。当前项目没有扩展支持或完整安全补丁覆盖的证据，因此不能把“能自行编译”解释为“有持续安全维护”。[OpenSSL 发布策略](https://openssl-library.org/policies/releasestrat/)（访问日期：2026-09-22）

## 阻止生产放行的缺口

1. **维护覆盖不完整**：尚无逐组件的安全公告跟踪、适用性分析、补丁回移与回归记录。只扫描 npm 依赖不会覆盖浏览器、V8、TLS、图片及字体解析组件。
2. **联网边界仍未完整验收**：preview.4 固定 DLL 已修正证书链与主机名校验默认配置，[本地 HTTPS 十二项测试](../../verification/production-validation/preview4-fixed-2026-09-23/README.md)覆盖显式信任后的成功、未受信链/错误主机/重定向拒绝，以及回调不能关闭两种校验。preview.3 旧固定包在相同类别的负向测试中失败，已由新版本替换；不能把新包测试追溯成旧包安全。系统或随包信任根、代理、远程服务、动态子资源与维护更新仍无完整生产证据。受控本地页面仍可能接触网络数据。
3. **内容与权限边界不等同于隔离**：当前 Bridge 的 session/origin/token 校验不构成操作系统进程沙箱。文件名、剪贴板文本、图片、字体、网络结果等数据入口仍须分别评估。
4. **签名和发布责任未闭环**：未指定实际证书、发布审批者、公告受理人、补丁承诺及回退演练责任人。不以 AI 生成的姓名或默认负责人填满这些字段。
5. **授权核对未闭环**：已有部分源码许可文本与文件散列；精确运行包的链接组成、随包声明及适用再分发要求仍需由维护者完成审阅。首方 SDK/模板/Demo 尚未由所有者选定对外复用许可；即使日后公开仓库，也不能把公开可见等同于允许第三方使用或再分发。

证书链和主机名是分别检查的条件，测试必须同时覆盖；不能以建立了 HTTPS 连接代替验证对端身份。[libcurl 证书校验](https://curl.se/libcurl/c/CURLOPT_SSL_VERIFYPEER.html)、[主机名校验](https://curl.se/libcurl/c/CURLOPT_SSL_VERIFYHOST.html)

## 后续交付及负责人记录

| 交付项 | 必需记录 | 当前状态 |
|---|---|---|
| 内核组成清单 | 实际编译配置、组件精确版本/补丁、链接输入与 DLL 散列 | 部分来源已识别，完整清单待完成 |
| 漏洞维护台账 | 公告来源、适用判断、修复版本、回归及未解决影响 | 待建立；不能记为无漏洞 |
| 网络负向验收 | 本地测试服务、测试证书、输入/预期、页面与原生结果、真实退出 | preview.4 固定 DLL 的回环链/主机名/重定向测试已通过；实际信任根、代理与动态子资源仍待验收 |
| 依赖更新演练 | 新来源 → 编译 → 独立消费者/模板/Demo → 压力 → 回退 | 生命周期补丁已进行两架构源码重建；完整更新和回退未完成 |
| 发布责任 | 维护者确认的人员/渠道、响应约定、签名和撤回流程 | 待维护者确认；技术验证仍可继续 |
| 采用决定 | 与更换内核方案的体积、功能、隔离及持续成本比较 | 待有对照数据后形成，不预先推荐生产采用 |

若某个范围的维护和边界无法验收，就保留该范围的未通过结论。生命周期、长稳或 Electron 体积优势不能抵消此门槛。Electron 官方同样要求持续更新底层框架、核对依赖并隔离不可信内容；对照程序须保留其正常安全设置。[Electron 安全指南](https://www.electronjs.org/docs/latest/tutorial/security)
