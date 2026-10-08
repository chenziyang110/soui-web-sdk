# 当前 Miniblink 二进制的许可材料候选映射

本记录对应 `0.3.0-preview.15` 当前 x86 `node.dll`（SHA-256 `2ed793ccccb0642df9916128e36aece733efd34ad6acb4e50caa0622a4c25ef3`）与 x64 `miniblink.dll`（SHA-256 `fe4160a70587d027504c409b8fd2bc5c87d78392811bff0c969f69c41a44d951`）。它把已提取的实际链接命令中的**显式库参数**，与显式 `/LIBPATH` 中的本地候选文件及源码清单里的候选许可文本关联；不证明链接器实际选择了哪份文件、提取了哪些归档成员、嵌套依赖是否穷尽，亦不作法律判断。

[候选映射](current-notice-candidate-map-v1.json)由冻结脚本 `Map-CurrentNoticeCandidates.py` 生成，SHA-256 为 `9a16e2cc7daa5ce34e6346b0079fb91e3a3c4d7446ed417b10f76fe3198e5551`。[独立一致性检查](notice-map-check-attempt1/stdout.txt)原始退出 0，核对链接提取、候选路径、待审清单和完整源码清单的输入散列；13 份唯一候选材料与 Git 索引 blob 逐一匹配。生成与检查的原始退出码、标准输出/错误及脚本前后散列分别保留在 `notice-map-attempt1/` 与 `notice-map-check-attempt1/`，不覆盖旧的链接警告与候选路径记录。

| 架构 | 显式库参数 | 显式路径有本地候选 | 未在显式路径定位 |
|---|---:|---:|---:|
| x86 | 36 | 12 | 24 |
| x64 | 35 | 11 | 24 |

x86 命令显式列出 `openssl.lib`，x64 没有这一显式参数；这不能证明 OpenSSL 只进入 x86。其余本地候选涉及 Skia、V8、libcurl、zlib、OTS、libxml、HarfBuzz、JPEG 和 PNG。另列出 Miniblink 根声明、Brotli 与 Node/c-ares 材料作人工排查线索。未在显式路径定位的库名可能由默认路径、环境、链接指令或系统工具链提供，不能直接判定缺失。

下一步仍需从同一构建的链接器详细输出或等价证据确认**实际解析文件与归档成员**，核对完整运行载荷和嵌套组件声明，再由维护者审阅适用条款与再分发方式。`third_party/manifests/license-review.json` 维持 `unverified`，`releaseReady=false`。
