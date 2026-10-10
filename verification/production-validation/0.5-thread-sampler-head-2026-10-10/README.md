# 0.5 线程采样 HEAD 源码有限回归归档（2026-10-10）

本目录封存 a476dff2 的已完成实际记录：x64 与 Win32 各 15 个故障回归用例全部通过，共 30 项。两架构的配置、构建、测试共六个 Owned 阶段均自然退出 0/0，完整 Job 关闭为 0，没有强制清理。归档制作仅复制和核对文件，没有重新运行测试。

实际被编译的是 HEAD 最小修复衍生的 main.cpp：24876 字节 / 15a2f3b6dec9c730ef7153e83668c3c60d5f980dbfb42e8a2e9e73cd3518b3e3。其 HEAD before 为 23191 字节，三跨度补丁为 3670 字节；source/head-before 与 source/head-patch 保存原文。此轮没有继承先前 35921 字节 dirty 主文件的测试结果。实际 CMakeCache 与 CL.read 证明独立测试入口的默认相邻主文件解析生效，未传入显式 main override。

测试 TU 直接包含真实主文件，并只在该 TU 内替换四个 Toolhelp API；先包含 Windows、SDK 与标准库头，再调用生产 process_thread_count()。15 例覆盖当前/其他 PID、完整与空枚举、最小合法 extent、创建/首条/后续枚举失败、短或超长返回结构、关闭失败及合并失败。它们验证原错误保留、失败不能返回部分计数、有效快照恰好关闭一次；不会运行验收程序入口或启动 Host。固定 2025 字节 CMake 和 8772 字节 fixture 均随 source/tested 原样保存。

实际六阶段、JUnit、CMakeCache 和 CL/LINK 命令与读取记录位于 actual；Root 与独立实际收据位于 reviews。controls 是原执行包的冻结 raw/inert 复本，保留原路径及原作者的 pre-execution false 字段，不用于重新执行。source/tested 的 2285 字节 README 是实际构建冻结版本；source/shipping 另保留 1838 字节中文正式 README，其归档时与 live 文档逐字节相同。公开安装 SDK 的真实链接证据保留在 link.read 中，本归档不复制 SDK TU、OBJ、EXE、LIB 或 DLL。

此轮只是线程计数函数的有限故障回归，不推广为真实资源增长、长期运行、目标 OS/PE、完整工具链或正式发布资格。原资源归档仍为 x86 句柄 315 → 318（+3）FAIL、整轮退出 1；这次 30 项测试不能取消该失败。evidence-map.json 保留原 161 文件线程归档和原 53 文件资源归档的精确引用，两目录未改动。

copy-index.json 逐项记录 raw 来源、复制 pin 及前后匹配；manifest.json 按真正 StringComparer.Ordinal 列出完整成员并固定除自身外的全部 SHA256。原 JSON/JSONL/TLOG/JUnit 的字节、编码、换行、绝对路径均保留；局部 69 字节 .gitattributes 禁用 text/eol/ident 转换并采用既有 whitespace 规则。manifest 自身不自 pin。所有复核结论以冻结来源为边界，后续工作区变化不会把旧 pin 重标为当前结果。

归档作者没有修改 live SDK/tests、原证据、Git index 或分支，没有执行 Node、Native、编译、测试或 VM。资源、系统与正式发布资格继续为 false，未来实测结果没有在此预填。
