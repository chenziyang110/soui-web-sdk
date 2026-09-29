# 当前内核链接读取记录补充（2026-09-28）

本目录补充[当前运行包声明观察](../license-runtime-notices-2026-09-28/README.md)的**机械构建输入证据**。采集绑定主树 `5df8e3f0a5ef5a45098cb08466d444158c7fef5e`、x86 `node.dll` SHA-256 `2ed793ccccb0642df9916128e36aece733efd34ad6acb4e50caa0622a4c25ef3`、x64 `miniblink.dll` SHA-256 `fe4160a70587d027504c409b8fd2bc5c87d78392811bff0c969f69c41a44d951`。采集时逐一比对正式重编记录、当前预编译 DLL、链接记录的时间与散列；[原始清单](inventory.json) SHA-256 为 `802cabd09ee32e9c2a859e154e541d2f07b69e0cc3aa8682aa272158a72629b0`，[采集后校验记录](verification.json)标记通过。两架构的原始 MSVC UTF-16LE `link.command.1.tlog` 和 `link.read.1.tlog` 均在 `raw/`，保留原字节。

| 架构 | 显式命令库名 | 链接读取库名 | OpenSSL 观察 |
| --- | ---: | ---: | --- |
| x86 | 36 | 44 | 命令显式列出，读取记录也包含 `openssl.lib` |
| x64 | 35 | 44 | 命令未显式列出，读取记录包含 `openssl.lib` |

x64 的 `link.read` 将 `openssl.lib` 定位到该架构的 `out/dependencies/x64/libraries/lib/`；这比“x64 命令未显式列出”更进一步。两架构的 44 个库名集合相同；x86 比显式参数多 8 个读取库名，x64 多 9 个，其中 x64 的新增项包含 `openssl.lib`。运行 `node verification/production-validation/license-link-read-traces-2026-09-28/Verify-Archived.cjs` 可从本目录原始记录重新核对冻结清单 SHA-256、原始记录字节、读取路径和此前的显式参数提取。首次校验夹具错误地假定两架构的产物均名为 `node.dll`，原始失败保留在 `check-attempt1/`；修正后 `check-attempt2/` 通过，增加清单固定散列与来源检查后的最终复核原件保存在 `check-attempt3/`。

**证据边界**：本目录的 `link.read` 只证明链接器打开了库文件，本身不能证明提取了哪些归档成员或确定适用的再分发条款。后续[匹配 DLL/PDB 的 x86 成员记录](../license-pdb-members-2026-09-29/README.md)列出 501 个来自 `openssl.lib` 的编译单元；x64 尚无同等级证据。采集发生在另一台 QEMU 客体运行期间，没有启动 SOUI 构建、桌面应用、Win7 客体或性能测量；该记录不支持隔离性能结论。完整运行载荷声明和维护者授权仍需审阅。[Issue #39](https://github.com/chenziyang110/soui-web-sdk/issues/39)继续开放，`third_party/manifests/license-review.json` 为 `unverified`，`releaseReady=false`。

2026-09-29 查阅 [OpenSSL 官方版本许可说明](https://openssl-library.org/source/license/)：1.0.2 及其字母补丁版本属于 OpenSSL 与原 SSLeay 双许可范围，不适用 3.0 起的 Apache-2.0 许可。官方[双许可文本](https://openssl-library.org/source/license/license-openssl-ssleay.txt)列有二进制再分发时的版权、条件和免责声明保留要求。这是版本与许可文本的来源核对；实际 x86 成员证据见上方后续记录，不能替代对具体分发物的授权判断；上述未关闭项保持不变。
