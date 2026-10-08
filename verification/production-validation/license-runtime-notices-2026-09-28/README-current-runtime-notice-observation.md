# 当前运行包声明文件核对（未放行）

`current-runtime-notice-observation-v1.json` 是对当前 preview.15 四份 Release 应用运行目录的机械核对，不是授权意见。生成器为 `Audit-CurrentRuntimeNotices.py`，输入为当前 `third_party/manifests/license-review.json`、`current-link-command-extraction-v3.json`、导入的 OpenSSL `LICENSE` 与 x86/x64 Starter/Workbench 实际 `node.dll` 和 `resources/legal/` 文件。生成器要求四份 DLL 与待审查散列一致，报告 SHA-256 为 `f2f4738b82f889228734da31bbc84836f201a2ef5c6cb198bdec5ea6d1b2f79e`。

独立的 `Verify-CurrentRuntimeNotices.py` 在 `notice-observation-check-attempt1/` 保存标准输出、标准错误和原始退出码；实测退出 0，复核四份应用、当前 DLL、链接清单、许可源码和五份随包文件。生成器 SHA-256 为 `71acadc57b21b0b13818b79c340af472287c24ea03cab40a44796d581cdbf97f`；校验器 SHA-256 为 `a7ff1038af480b4d1958bb38cc19fd433875942f0402f58df0f50fe16f1849c4`。本次文件读写和核对只记录材料是否存在，不判断许可义务。

x86 当前链接命令显式列出 `openssl.lib`。导入源码 `third_party/sources/miniblink/node/openssl/openssl/LICENSE`（SHA-256 `d0047be11f613ca5afebb508c19242fdecd55b740d70ea031c00c11784f37ee7`）包含 OpenSSL 与原 SSLeay 两套条款；[OpenSSL 1.0.2h 官方版本文件](https://github.com/openssl/openssl/blob/OpenSSL_1_0_2h/LICENSE)也明确列出这两套文本。当前每份应用的 `resources/legal/` 只有五个文件，没有逐字节相同的导入许可证，也没有 `Original SSLeay License` 文本。上游 [Miniblink 导入来源的许可证](https://github.com/Alberl-Lee/miniblink49-VS2022/blob/master/LICENSE)另为 Apache 2.0，不能拿它替代嵌套 OpenSSL 文本。

这项发现只说明当前随应用部署的声明集合有可核查的缺口。显式链接参数不能证明哪些 OpenSSL 归档成员最终进入 DLL；x64 未显式列出 `openssl.lib` 也不能证明它不含 OpenSSL。完整链接成员、运行包中其他声明渠道、具体再分发义务与首方许可证均未完成审阅。保持 `license-review.json` 的 `unverified` 和 `releaseReady=false`；不要更换同版本已冻结 SDK Core 来掩盖缺口。
