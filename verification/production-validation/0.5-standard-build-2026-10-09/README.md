# 0.5 标准构建调用者

日期：2026-10-09。开发版本 `0.5.0-preview.1`。本切片不关闭正式发布、应用、GUI、目标系统或长期运行门槛。

标准调用者固定本工作区的 Build.ps1 参数、当前源码、工具、命令与旧报告，再核对真实 Job 原文、编译配置和实际载荷。默认要求自然闭合；显式 opt-in 才能请求已审精确 MSVC 对象的窄政策。整个工作区共用合作租约，外部 NVM junction 固定 requested/resolved 路径和 bytes。源码、证据、prepared runner 和 pin 仍拒绝链接；一致性不认证可改写整套记录的同权限主体。

## 归档边界

公开 index.json 映射 1,979 份逐字 payload，共160,113,361字节；保留本项目源码、自产工具/fixture、原始日志、context、输入和原索引。两份大于64 MiB的文件是未执行的 TEST_ONLY拒绝夹具，不是 SDK运行体积。代码及程序样式文件均为 inert snapshot。

首次完整归档的1,972份原件、161,319,509字节和索引SHA `5018368731012ac42eb8976bf54ab7f778f0dd5ace12b18f7eb2b448c2bfcb1c` 完整保存在新的私有owned out。公开归档中的四份真实外部构建工具PE改为来源、字节数、散列及私有保留位置；不分发这些构建工具。original-full-index.json.inert.snapshot 是该私有完整集合的历史清单，不表示其中每个二进制均在公开包内。其他本项目原件逐字保留。首次README及公开整理脚本也保存原字节。

| 原组 | 原索引文件数 | 证明范围 |
|---|---:|---|
| original-boundaries |147| 原始纯边界及拒绝，没有实际 SDK编译 |
| actual-tools |1531| 双架构实际prepared工具和八个标准组；编译root0、通用false/124仍保留，未请求尾政策 |
| tool-resolution |174| NVM与workspace租约修正；caller36/36、双架构tail-protocol各157/157自然完成 |
| sdk-default-x64 |34| 当前SDK-only实际编译/安装root0；清理候选vctip后归零，generic=false/124、accepted=false |
| sdk-optin-natural-x64 |35| 同一源码的显式pin增量构建自然完成；generic=true/0、accepted=true、尾例外false |
| sdk-default-x86 |34| 当前SDK-only实际编译/安装，默认自然完成；generic=true/0、accepted=true |

旧两次私有归档诊断和链接fixture限制保留原文与更正。Root首次归档前误用排序snapshot比较有序compiler fingerprint；逐项和原顺序重算确认16项未变，旧脚本保存。该错误发生在复制前，不改变实际编译/测试原件。

## 当前组合

主线Query/测量/GUI/Tail入口整合来自 `201859502f42ba392be86c7b4b5c30a7f85de48a`。实际SDK source SHA `f637ee3d58789530ed44f49d643dc7fc2c270ade211b5a04d376096a2f3be2bf`；raw source、九份当前切片文件和16项工具编译输入另在索引固定。工具fingerprint仍为 `4e3f19da88c4e3a0dabc7819de096c6e7cc039bc0fbc17c1a9d3388470e45d89`，旧全局源码冻结保留历史身份。

三次buildId不同，x64后续保存的旧报告与首次原字节一致。后续x64是增量构建，其库由首轮真实编译产生：35,754,744字节/SHA `d18da1f63a6293ca23150f1a65f70e2479a2d597f74729023aed615a5366c628`。x86库24,454,576字节/SHA `ab5422a7a158757adb1aa7ab8cc6ea3ab94cad39c9c1b8d4892e3e222df17470`。库保留在owned output，公开归档仅保留观察/散列，没有发布二进制。

本轮没有实际使用尾进程例外，不能追认首次默认失败政策通过。提交前当前sdk/web锁定依赖图 npm audit为0漏洞，owned Job自然闭合，七份原件在dependency-audit保存。没有完整应用/现代后端/GUI/全部Test或Collect/最终无Git重建/Win7或PE/8小时结果。所有记录releaseReady=false，canonical acceptance未修改。
