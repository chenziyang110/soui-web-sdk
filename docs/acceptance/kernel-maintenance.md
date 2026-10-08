# 内核维护与发布准入核对

日期：2026-09-23。状态：初步来源与配置核对，生产准入未通过。关联：[完整方案](../roadmap/0.3-sdk-validation.md)、[安全报告流程](../../SECURITY.md)。本文不提供漏洞利用步骤，也不把源码版本识别等同于完整二进制安全评估。

## 已核对的来源

| 对象 | 本地证据 | 已知事实与边界 |
|---|---|---|
| Miniblink 导入 | `third_party/sources/miniblink/SOURCE-MANIFEST.json` | 来源为 `Alberl-Lee/miniblink49-VS2022`，基线提交 `e0a58c04a5ae2a003285fcf26556014f7ceb7037`；包含工作树修改，不是未经修改的上游发布 |
| 导入后的补丁 | `third_party/patches/miniblink/` | 生命周期、动态绑定 ABI、GPU 与 HTTPS 身份校验保留六份独立补丁；preview.4 固定 DLL 与本次重编译候选散列一致。这些修补不代表完成漏洞维护 |
| JavaScript 引擎 | `third_party/sources/miniblink/v8_5_7/include/v8-version.h` | 源码宏为 5.7.246.0；不能仅用“Chrome 49 兼容”描述整个内核的确切组成 |
| TLS 源码 | `third_party/sources/miniblink/node/openssl/openssl/include/openssl/opensslv.h` | 版本标记为 OpenSSL 1.0.2h；现有依赖构建入口选择此 OpenSSL 工程。仍需结合链接配置与产物确认完整组成 |
| HTTP 源码 | `third_party/sources/miniblink/third_party/libcurl/include/curl/curlver.h` | 版本标记为 libcurl 7.61.1；网络配置及错误路径需要独立真实测试 |
| 精确二进制授权 | `third_party/manifests/license-review.json` | 现有状态是 `unverified`，已重新绑定 preview.4 固定 DLL；完整链接组成与再分发要求仍须维护者审阅 |

OpenSSL 官方将 1.0.2 列为已结束常规支持的分支，另有扩展支持渠道。当前项目没有扩展支持或完整安全补丁覆盖的证据，因此不能把“能自行编译”解释为“有持续安全维护”。[OpenSSL 发布策略](https://openssl-library.org/policies/releasestrat/)（访问日期：2026-09-22）

## 阻止生产放行的缺口

1. **维护覆盖不完整**：尚无逐组件的安全公告跟踪、适用性分析、补丁回移与回归记录。只扫描 npm 依赖不会覆盖浏览器、V8、TLS、图片及字体解析组件。
2. **联网边界仍未完整验收**：preview.4 固定 DLL 已修正证书链与主机名校验默认配置，[本地 HTTPS 十二项测试](../../verification/production-validation/preview4-fixed-2026-09-23/README.md)覆盖显式信任后的成功、未受信链/错误主机/重定向拒绝，以及回调不能关闭两种校验。preview.3 旧固定包在相同类别的负向测试中失败，已由新版本替换；不能把新包测试追溯成旧包安全。系统或随包信任根、代理、远程服务、动态子资源与维护更新仍无完整生产证据。受控本地页面仍可能接触网络数据。
3. **内容与权限边界不等同于隔离**：当前 Bridge 的 session/origin/token 校验不构成操作系统进程沙箱。文件名、剪贴板文本、图片、字体、网络结果等数据入口仍须分别评估。
4. **签名和发布责任未闭环**：未指定实际证书、发布审批者、公告受理人、补丁承诺及回退演练责任人。不以 AI 生成的姓名或默认负责人填满这些字段。
5. **授权核对未闭环**：已有源码许可文本与文件散列；精确运行包的链接组成、随包声明及适用再分发要求仍需由维护者完成审阅。

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
