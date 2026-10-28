# ADR 0130：兼容前端的有限 inline source-map 容量

状态：proposed；2026-10-23 源码修复，真实浏览器断点与交付配对待验证。

## 背景

当前兼容诊断 fixture 的 `index-legacy-BKAuK8HG.js` 内嵌 map 有 77,525 个映射段、24 个来源；`polyfills-legacy-Debvrl7F.js` 有 60,974 个映射段、286 个来源。它们分别超过现有 65,536 段和 256 来源上限。文件与原 cw2 冻结 handoff 的字节/SHA 一致，directive 位于完整 wrapper 后，不能把问题归为包装丢失 map。

该前端的 `SourceMap.load` 使用私有、同步、纯数据的 `Stage0InlineMaps`，不是网络 map loader。canonical 修复入口是 `sdk/resources/devtools/miniblink/overrides/front_end/sdk/SourceMap.js`；`tools/internal/devtools-frontend.cjs compose` 对此文件按 `source.json` 的固定 pin 直接合成。历史实验的 generator、已合成前端和 p4 包均不作为编辑入口。

## 决定

仅调整三个互相关联的有限上限：

| 约束 | 原值 | 新值 | 理由 |
| --- | ---: | ---: | --- |
| 每 map 映射段 | 65,536 | 131,072 | 容纳真实 77,525 段 legacy 入口，仍逐段验证 |
| 每 map 来源 | 256 | 512 | 容纳真实 286 来源 polyfill map |
| 每 target 绑定账目条数 | 256 | 512 | 否则 map 解码成功后仍在第 257 个 UI 绑定失败；三个实际 legacy fixture map 合计 311 来源 |

新值为紧邻需求的二进制容量档位。它们不构成独立的内存授权：URL 和解码数据仍各最多 1 MiB，每 target 的活动、缓存、退休模型和绑定账目仍共同受原 16 MiB logical charge 约束。缓存及活动退休记录仍最多 8 个，绑定每条仍计 256 bytes、借用深度仍最多 8。可用容量还取决于各 map 的实际 charge，不能推断任意 512 来源、131,072 段组合均会接受。

保留全部 UTF-8/base64/平面 JSON、路径 label 和 canonical 唯一性、来源内容、VLQ 位数/数值/字段、生成与原坐标、names/ignoreList、epoch/身份/借用/退休检查及无网络 fallback。总预算不能在失败后扩大或被测试绕过。本次不改公共 API、原生授权、生命周期、版本或 manifest。

## 验证与边界

新增 Node 内置测试直接加载 canonical JavaScript；周围 URL、workspace、UI、事件与协议对象为明确的测试 double。测试覆盖新段数和来源数的精确上限/加一拒绝、未修改的 CompilerScriptMapping 发布算法累计 512 个绑定与第 513 个拒绝、reset 释放、原字节和总 charge、路径与 VLQ 负例。

测试另提供显式 `--fixture-root <absolute resources>`，仅从已固定 SHA 的三个实际 legacy JS 提取 inline JSON，绝不执行业务 JS。它在同一 target 发布 286+24+1 个来源，核对原 source content、总 charge 和 reset。常规测试不依赖工作区 out 路径，也不复制旧依赖、构建或运行历史项目。

采用一次红/绿窄验证并保留原失败与输入身份。该结果证明数据解析/发布算法及有限账目行为，不证明实际 V8 scriptParsed 携带 map、浏览器设置、面板输入、可见 UI 或 TS/TSX 断点通过。当前 p4 包不可变；后续须更新 canonical 源声明 pin、合成新前端并完成新技术配对，才能生成新的候选。原 timeout 和全部未验 gate 保留。
