# I01 / F01 / W01：已验证的实施范围

当前源码候选仍为 `0.7.0-preview.4`，SDK source SHA `e1d2dd8b7699930f678e21a77772b8fa4e371f5b67a53937d98470ee1659e87c`。独立核对已绑定下述既有原件及当前源码相等范围；I01/W01没有重测，F01只补当前单TU合成回归，没有重编SDK或扩大最终Host的性能资格。

| 项目 | 有证据支持的结论 | 仍未证明 |
|---|---|---|
| I01 | 同源码受控burst的通知调用工作减少，以及保序/交错/恢复合同 | OS实际唤醒次数、当前应用CPU/FPS或输入尾延迟收益 |
| F01 | 配置真实性修复可保留；当前150ms合成回归x64单TU构建及原fixture一次执行通过 | setter实际调用次数、真实慢provider、x86新增用例与宏观收益 |
| W01 | 有界metadata复用、原短路语义，以及缓存命中时少一次worker路由解析 | 全请求解析减半、授权解析消失或完整应用内存收益 |

## I01：通知调用数有实际对照

既有v143/x64受控对照每轮50,000组、每组64包。四份原始结果各接纳并完成3,200,000项，均有序、checksum相同。原逐pending策略调用 `engine_admission::notify_input` 3,200,000次，receipt策略为50,000次，即每组64→1次、调用减少98.4375%。另一个真实Engine合同接纳32包，只通知一次，随后没有新producer也能有序排空，32次service完成、mailbox为空。

当前mailbox、Engine、interaction protocol及trace四源逐字等于该受控实验记录，publication/wake合同也与旧双架构通过记录一致。这是已有同源码的有限工作量证据，不是本轮重新运行当前SDK4；原auto-reset事件本来就可能合并OS唤醒，因此不能把函数调用数直接写成系统唤醒数或GUI收益。

## F01：状态观察与当前合成回归分开

匹配SDK4及默认56d内核的既有功能原件记录requested/effective/attempted/applied均60、backend240、apply_result0、negotiated=true。feedback_supported和pulse_synchronized均false，保持原值，不能推断所有外部consumer的Host pulse已同步。实际setter调用计数未记录。

当前Host的query/apply及失败读回位于completed_at之前，100/200/400ms退避、同key去重、generation撤销和旧provider未确认语义均保留。旧测试源中helper/provider与当前相等，但新增150ms用例所在negotiation_cases已改变，旧通过记录不能证明新增用例已执行。限定查找未绑定现成收据后，仅补当前标准v14314.44/Windows SDK19041、x64 Release的原 `sdk.native.host-frame-policy` fixture，不重跑整套矩阵。

首轮新包装器将两个CMake `-D` 定义拼成一个参数，配置失败；原fixture未编译或执行。保存的PowerShell异常及其观察值-1不是可信CMake原生退出码。构建包装器自然退出1、34.199秒、Job0，原件保留。修正仅分开两个参数并使用新尝试目录，测试源码/断言和15秒原测试期限不变。

第二轮构建自然退出0、63.050秒，仅 `fixture.obj`；实际原fixture执行一次，71ms、自然退出0、Job0，两阶段编译助手均自然退出0。当前 `negotiation_cases.h` SHA `3e551baee22727a46828f4d6f81b7ad7b04a649c89b949ad1dfbac83cdc06b20`，EXE52,224 B、SHA `2e5451bfbfd40306e3fa3049ea203498d8c763e893a379cda44eed8cdc317f53`。原main无条件调用该用例组，使用require/throw而非被Release移除的assert；生成生产片段的字节和输入身份已绑定。实际收据 `F01-current-case-x64-r2/result.json` SHA `1441bf8df2ec7fba3af618b7f16e3800d61700d3f47889e165f5358fa397339c`，路径相对于验收包。

这是私有状态机与生成生产片段的合成合同，不执行真实慢provider或完整Host，不测setter调用次数、帧率或输入呈现收益；新增用例的x86执行仍未知。机器索引为 `f01-current-case-x64-r2.json`，原首轮失败不改写为一次全程成功。

独立实际审查 `F01-current-case-x64-r2/independent-actual-review-r1.json`，12,721 B、SHA `b9aabbb40895c7d26f4535779392b2061b6b5ad505d14967ecbf41e1f6c36923`，核对原始Owned起止、当前用例的main调用、单TU、生成片段及实际EXE；审查者未新运行测试或修改原件。它作为新增补充保留，不改写早先I01/F01/W01原审查中当时未绑定的状态。

## W01：减少的是已命中缓存的路由工作

ingress利用已有DOM提取有界method/session；worker只有缓存不可用时才走原路由parse，原Bridge的handle_json、授权、schema/session/generation、deadline与revoke仍执行。字段内容上限为method128＋session256字节，不是完整驻留内存；超限或分配失败回退原解析，不增加接纳拒绝。

旧双架构差分和x64分配失败补测有实际记录，原工程启用UNDEBUG。当前metadata头与fixture匹配旧来源；旧组合SDK测试使用LLVM22/MSVC142/19041，不能称本轮preview.4/v143重新测试。CPU片段源码在命中时少一次路由解析，原输出没有直接parse计数器，不将其改写为“所有请求解析减半”或应用CPU改善。

完整绑定见 `verification/performance/sdk-input-frame-20261105/core-implementation-gates-r1.json`。独立原件相对于验收包为 `core-implementation-gates-independent-review-r1.json`，SHA `a64069ce210414417a62c101e1856ab3d432eba9d204f0488d3738723d354514`；原实现实验位于 `perf-implementation-20261102` 的I01/W01及final-sdk-validation目录，各自身份保持。最终输入、帧率、资源及无回退门仍开放，`performanceVerified=false`、`inputLatencyVerified=false`、`releaseReady=false`、`wholeGoalComplete=false`。
