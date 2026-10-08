# x86 内核 OpenSSL 成员级链接证据（2026-09-29）

本目录保存对当前 x86 `node.dll` 和匹配 PDB 的只读检查。DLL SHA-256 为 `2ed793ccccb0642df9916128e36aece733efd34ad6acb4e50caa0622a4c25ef3`，PDB SHA-256 为 `f3e7f26fedd9deddde44ba0a5adeef11f8e3ef637b16cfa715b9fea004a042e5`。`capture.json` 保存路径、字节数、原始命令退出码和采集时外部 QEMU 状态；三组 `stdout`、`stderr` 是原始工具输出，`analysis.json` 列出各文件的字节数与 SHA-256。大型 PDB 本身留在本机构建目录，不进入仓库。

`llvm-readobj --coff-debug-directory` 输出的 DLL CodeView GUID `3F19AE1B-D4FA-463B-80B8-7D89FE53437F`、Age 1 与 `llvm-pdbutil dump --summary` 的 PDB 身份一致。`llvm-pdbutil dump --modules` 在该 PDB 的 5145 个编译单元记录中列出 **501 个 `Obj` 来源为 x86 `openssl.lib`**，包括 `x86cpuid.obj`、`stack.obj`。独立按原始 `Obj:` 行计数也得到 501。可运行 `node verification/production-validation/license-pdb-members-2026-09-29/Analyze-X86-Pdb.cjs`，从保存的原始输出重新检查身份、成员数和逐字节清单；`Verify-Archive.cjs` 比对原始文件与 Git index 的字节。这些命令只读，不需要运行 SDK 或打开图形界面。

这为**该 SHA 的 x86 DLL**提供了 OpenSSL 归档成员进入链接结果的证据，比[链接器打开库的记录](../license-link-read-traces-2026-09-28/README.md)更强；它没有证明每段代码在运行时可达，也没有提供 x64 同等级成员证据。源码版本头标记 OpenSSL 1.0.2h，源码树内保留的 `third_party/sources/miniblink/node/openssl/openssl/LICENSE` SHA-256 为 `d0047be11f613ca5afebb508c19242fdecd55b740d70ea031c00c11784f37ee7`；[OpenSSL 官方版本许可说明](https://openssl-library.org/source/license/)将 1.0.2 及字母补丁归入 OpenSSL/SSLeay 双许可，[官方许可文本](https://openssl-library.org/source/license/license-openssl-ssleay.txt)列有二进制再分发声明条件。四份已核对的运行包 `resources/legal/` 尚未发现相应文本，依赖授权审阅仍为 `unverified`，Issue #39 保持开放，`releaseReady=false`。该记录不构成法律结论，也不构成隔离性能证据。

对精确 SHA-256 `fe4160a70587d027504c409b8fd2bc5c87d78392811bff0c969f69c41a44d951` 的 x64 `miniblink.dll` 另作只读检查：`llvm-readobj` 原始输出只有 POGO 调试目录，没有 CodeView；同一构建的链接命令虽指定 `/PDB` 输出路径，却没有 `/DEBUG` 开关，采集时该输出路径没有 PDB。原始输出、退出码、链接记录 SHA 与观察结果保存在 `x64-*` 文件；`Analyze-X64-Debug.cjs` 可只读复核。**没有 x64 成员级结论**；后续需要与该 DLL 精确绑定的链接成员映射或在新构建中保留相应诊断，不能用 x86 PDB 或 x64 `link.read` 推断。

采集期间存在他人的 QEMU；本次只读取 PE/PDB 元数据，没有启动编译、桌面、客体或性能负载。原始工具输出与外部原件逐字节比对后归档；本目录不覆盖较早的链接读取或许可观察原样本。
