# 两份内核依赖配套文本的原版补录

2026-09-25 只补录导入源码明确指向、但本仓库原来缺失的两份上游文本；没有改动固定的 x86/x64 `node.dll`。补录文件留在 `third_party/licenses/`，不是已经核对完成的运行包声明或法律审查。

| 组件 | 原版来源 | 本仓库文件 | 原始 SHA-256 |
|---|---|---|---|
| libcurl 7.61.1 | [curl 官方仓库 `curl-7_61_1/COPYING`](https://github.com/curl/curl/blob/curl-7_61_1/COPYING) | `third_party/licenses/libcurl-7.61.1-COPYING.txt` | `5F3849EC38DDB927E79F514BF948890C41B8D1407286A49609B8FB1585931095` |
| Independent JPEG Group 9a | [IJG 官方 `jpegsrc.v9a.tar.gz`](https://www.ijg.org/files/jpegsrc.v9a.tar.gz) 内 `jpeg-9a/README` | `third_party/licenses/ijg-jpeg-9a-README.txt` | `C791DA525733040E622ED257C7B096B8F5191B332604EAD0C6BC2B54CBD8E0D1` |

IJG 压缩包的 SHA-256 为 `C133068C1ADEBED7C36B95445FCA78CF74849CD3FED0D05C5D211C8325B22740`。其中 `jversion.h` 和 `jpeglib.h` 与导入源码对应文件经 Git 文本规范化比较无差异，`jversion.h` 标记 `9a 19-Jan-2014`。curl 官方标签的 `curlver.h` 与导入文件相比仅有发布时生成的版本/时间字段差异；`curl.h` 另有导入源码把 `CURL_MAX_WRITE_SIZE` 从 16,384 改为 32,768 的本地改动。导入 `curlver.h` 标记 7.61.1。这些检查支持**来源版本匹配**，并不证明所有对象文件都与上游相同。

仍须核对固定 DLL 的实际链接成员和完整运行载荷，补齐 OTS 等未定位文本、嵌套组件声明，决定两份文本如何随各运行包交付，并由维护者审阅实际适用的再分发要求。`third_party/manifests/license-review.json` 继续是 `unverified`，`releaseReady=false`。
