# 固定 Miniblink 二进制的源码声明材料定位

状态：**机械定位，二进制授权仍为 `unverified`**。本记录对应 x86 `node.dll` SHA-256 `3B9071413F704958FB5708B8BB065785A6C3ED03B7FD0FB04F5952201E5FE4F7` 和 x64 `node.dll` SHA-256 `0801AA929336168C851DF4A3F245F5984A88BD01AF585AEA752404FA07774816`；2026-09-25 从实际随包文件重新计算，与[授权待审清单](../../../third_party/manifests/license-review.json)及[链接命令记录](../kernel-link-inputs-2026-09-25/observation.json)一致。下表 SHA-256 取自根[源码清单](../../../source-manifest.json)的 Git 索引 blob（文本 LF 规范化），与发布源码包的内容身份一致；它只定位源码树中可见的**候选声明文件**，不证明某个静态库成员已进入 DLL，也不判断条款适用性或声明是否完整。

| 链接命令中的库或相关源码 | 候选源码声明文件 | SHA-256 |
|---|---|---|
| Miniblink 项目根 | `third_party/sources/miniblink/LICENSE` | `2958241ded36ff3e9c082a74c8cb94459b45d8406643a0206ff3a9f86d0e70cf` |
| `v8_5_7.lib`、`v8_5_7_1.lib` | `third_party/sources/miniblink/v8_5_7/LICENSE` | `2337a9caeb13854242af8d0022f8e57389e5eae56fa3bf7887f5f38492b57199` |
| `skia.lib`、`skia_gpu.lib` | `third_party/sources/miniblink/third_party/skia/LICENSE` | `4826c9a63352256e3d1eaa81005e60fa618e2251ce2253822855cc27782ced07` |
| `harfbuzz.lib` | `third_party/sources/miniblink/third_party/harfbuzz-ng/COPYING` | `2a886915de4f296cdae5ed67064f86dba01d0c55286d86e8487f2a5caaf40216` |
| `zlib.lib` | `third_party/sources/miniblink/third_party/zlib/LICENSE` | `282644c64aa8c4f0a4a589fd38d2213e41f37b4e0e4f639e01149d09b67117db` |
| x86 命令显式列出的 `openssl.lib` | `third_party/sources/miniblink/node/openssl/openssl/LICENSE` | `0885ecb3437fc1178a62ee58ba3030e48cafab77dc31607e91208476338e0222` |
| `ots.lib` 工程引用的 Brotli 源码 | `third_party/sources/miniblink/third_party/brotli/LICENSE` | `cfc7749b96f63bd31c3c42b5c471bf756814053e847c10f3eb003417bc523d30` |

`libcurl.lib`、`ots.lib`、`libxml.lib`、`libjpeg.lib`、`libpng.lib` 的候选源码根目录下未发现顶层 `LICENSE`、`COPYING` 或 `NOTICE` 文件；`libcurl` 工程还引用 `node/cares`，该根目录也未发现上述名称的顶层文件。这**不等于**源码没有文件头或上级目录的有效声明；需要逐组件核对并决定随包声明。`libxml` 根目录仅看到 `README.chromium`，不能将其直接当作许可证文本。V8、Skia、HarfBuzz 等目录还存在子项目或其他声明文件，上表不是穷尽清单。

尤其不能混淆两个 zlib 输入：上述 `zlib.lib` 工程引用 Miniblink 内部的 `third_party/zlib`，该目录的 `LICENSE` 开头写 `version 1.2.4`；独立随包 `zlib1.dll` 在[依赖清单](../../../third_party/manifests/dependencies.json)中标为 1.2.11。单凭许可证开头或链接参数，仍无法断定最终 DLL 中实际选择的 zlib 对象版本。

下一步必须从精确构建记录确认静态库的实际链接成员和默认库指令，清点完整 x86/x64 运行载荷与嵌套依赖，为每个实际分发组件准备适用的声明与再分发判断，并由维护者明确签署发布责任。在此之前保持 `releaseReady=false`，本记录不构成法律或安全结论。
