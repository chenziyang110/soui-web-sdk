# 历史解析器单元夹具

这四个文件只用于源码搬迁后仍可运行的单元回归。它们按原字节复制自 2026-09-23 / 2026-09-24 的有限历史记录，不是当前 SDK 的性能、资源、操作系统兼容性、长时运行或发布资格证据。

`mixed-analysis.test.cjs` 仍通过原读取器验证 `result.json` 对 `events.ndjson` 的 SHA256 和完整动作记录；历史结果的资源与长时门槛仍为 `not-evaluated`，`releaseReady` 为 `false`。`page-pair.test.cjs` 仍拒绝原始视口不等与过短的 S01 数据，随后只在内存中构造边界输入，保留 120000 ms 运动时长、视口、动作顺序和场景检查。测试没有改写这些原始文件。

数据来源位于原仓库 `verification/production-validation/`，仅复制以下四项；相邻的大型验证档案、二进制和运行收据均不依赖也不复制。

| 相对原始档案路径 | 字节 | SHA256 |
| --- | ---: | --- |
| preview5-mixed-soak-preflight-2026-09-24/x86-60s/result.json | 4392 | 1ad71d476cd02eaadc2859ba26ee3789b8739d353680b1897fab0087176c2ba1 |
| preview5-mixed-soak-preflight-2026-09-24/x86-60s/events.ndjson | 12465 | ec18cbbd55cd1931b2229f1cccaa941b25b46a3e41bff1d3e05b55f68c333b1d |
| benchmark-host-smoke-2026-09-23/soui-x64-run5/soui-70144-427189546-1.json | 1073 | 95af2b06e734cb7b38db0d2ab5731a29eb3d0bc6f48b94b70cf403d49befb3f0 |
| benchmark-host-smoke-2026-09-23/electron-run1/6a947807-9291-42d5-8657-e714cdb97dba-S01-12204884-adaf-4860-bdfb-ab2186116de9.json | 1079 | 574e884dc0327bd005eff11918f1912b1acea2cc6130107d659d027da2ac7cf6 |

.gitattributes 只对上述四个路径禁用文本转换，保留事件日志哈希和原始 JSON 字节。数据完整人工读取并检查字段，内容为资源计数、动作、PID/UUID、哈希与场景说明，未发现实际凭据或私钥。
