# preview.3 → preview.4 独立业务项目升级与回退

日期：2026-09-23。候选 `0.3.0-preview.4`，生产准入仍为 `false`。这是仓库外新建的 `ProductTrial`，并非 Demo 工程。业务方自行增加 `app/business_mark.*`、改动 `app/main.cpp`、`web/src/main.tsx` 和 CMake 入口；五个业务文件的原始散列及副本见 [`result.json`](result.json) 与 `business-source/`。

| 阶段 | SDK | 实际行为 |
|---|---|---|
| [`old-initial/`](old-initial/) | preview.3 | 锁校验、x64 Release 编译、真实启动和关闭，业务标记可见 |
| [`new-upgraded/`](new-upgraded/) | preview.4 | 安装后重新编译、启动、关闭，业务文件散列不变 |
| [`old-rolled-back/`](old-rolled-back/) | preview.3 | 回退后重新编译、启动、关闭，业务文件散列不变 |
| [`new-restored/`](new-restored/) | preview.4 | 再升级后重新编译、启动、关闭，业务文件散列不变 |

四个不同 build-id、对应的源码指纹、SDK 锁、完整运行载荷与 smoke 结果均在各阶段原始 JSON 中；[`verification.json`](verification.json)由 `tests/acceptance/verify-upgrade.cjs`逐项交叉核对，41 份原始文件保存散列。运行脚本原样归档为 [`procedure-as-run.ps1`](procedure-as-run.ps1)，执行位置是 SDK 根目录下的 `out/`；它不是从本归档目录直接运行的入口。

最初一次验证尝试在首段编译通过后调用了新项目里不存在的 `tools/Test.ps1`，因此未产生有效启动结论。`attempt1-invalid-runner/`保留当次编译和锁校验日志；修正验证脚本为直接运行生成项目的真实应用，并通过随项目交付的构建证据工具核对退出码、日志和载荷身份后，使用全新目录完整重跑四段。这是测试驱动入口错误，不将首段失败记为 SDK 缺陷，也不覆盖它。

本证据只覆盖 Windows 本机 x64 Release 的业务小样升级链，不替代 x86、Debug、安装签名、故障中断恢复、目标机或长期业务运行验收。
