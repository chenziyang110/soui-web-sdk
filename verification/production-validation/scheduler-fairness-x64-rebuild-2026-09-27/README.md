# x64 完整依赖重编和原动画验证

候选源码提交 `be1a70872682cb25acbcbfc96fb7fe46bb322667`，两架构共用冻结输入 SHA `012bfc5c482f59dc14452a76cc13e7536662950e218ada4e888ad475a8d7d652`，28,163 文件。x64 `Build-Dependencies -Component All` 从 2026-09-26 15:46:17Z 到 16:36:44Z，原始退出 0、无外层超时，构建前后输入一致。

六份 DLL 的散列与 x64 PE 身份通过。新内核 35,865,600 字节，SHA `fe4160a70587d027504c409b8fd2bc5c87d78392811bff0c969f69c41a44d951`。18 份构建日志和 33 份实际编译/链接命令没有外部 vcpkg 路径或 C4819/C4828；其他警告完整保留，风险审查未完成。

新冻结 SHA `e15be278480ac667f0ad518fd8699063b1a41a894745bd5714186e25f0b9be9b` 的 70 文件消费者只替换 `node.dll`。EXE、HTML/CSS 动画、JS、原 60 秒就绪期限和 30 秒短测时长均保持不变；消费者仍为 preview.11 配已修监督器。第一次命令被新出现的外部 QEMU 门禁拒绝，退出 2，没有启动 GUI；原样保留。

外部 QEMU 退出后，第二次命令串行运行，原生 76 条连续事件、34,337 ms、退出 0。bootstrap/ready 各一次，两类公共任务、结果 42、取消、退订和关闭完成；全过程门禁及完整前后载荷通过复核，没有调试器。这确认修复覆盖原 x64 动画加载复现，不代表当前 SDK 整包或真实长时续期通过。

`raw/driver/dependency-review.json` 是动画运行前的构建复核，保留当时 `animationPending=true`。最终动画结果见 `raw/animated/review.json`；`raw-manifest.json` 逐项绑定原件、大小与散列。新 SOUI DLL 的应用验证、四配置、Win7、V02、长稳、真实续期与生产门槛继续待验收。
