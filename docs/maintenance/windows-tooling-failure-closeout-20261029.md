# VCTIP、SDK EPERM 与 PS5 准备链收尾

本轮代码、独立静态审查及必要短验完成，基线为PR127合并后的main。不改变已发布v0.6.0 tag、附件或验收资格。设计见[ADR-0137](../adr/0137-bounded-windows-tool-preparation.md)，实际结果与原失败见[证据说明](../../verification/production-validation/windows-tooling-failure-closeout-2026-10-29/README.md)。ADR保留设计时的候选状态，执行结果以独立记录为准。

VCTIP自身支持idle timeout；固定匹配映像的默认900秒生命周期与原30秒drain不匹配。两个正式MSVC入口在configure/build各自持有共享mutex，拒绝已有服务，固定VS实例、compiler/server/config身份，启动自有20秒idle服务，并等待自然退出。服务替换、非零、超时和原编译失败均不能变成成功。原root/drain不延长，LLVM不启动该服务。新增工具发现和生命周期脚本进入prepared指纹，四项漂移回归已过；旧prepared元数据不改挂新脚本。

SDK默认清理和六次rename/450ms保持。显式保留模式让失败现场存在，恢复要求原UUID sibling stage、调用者给定manifest SHA256、完整payload/文件闭集及不存在的输出；这是独立操作，不能把原非零追认为成功。真实PS5子文件句柄造成EPERM，释放后同字节恢复通过；当前工作树canonical pack也实际通过。历史两次原stage已销毁，所以无法识别原持有者，不归因Defender或MAX_PATH。保留模式用完后仅清理自己核对过的stage，不自动反复积累或结束持有者。

原PS5空stage发生在外部VM数据安装脚本，未独立记录解压stream/退出码。现在提供checked ZIP准备入口：成员安全预检、短UUID stage、同一单调期限内的流式身份和内容校验，完整闭集成立才发布。原命令失败及清理错误分别保留；无关联的WebView2 NuGet恢复不修改。实际系统PS5+官方7-Zip的中文长路径与拒绝例已通过，旧5846文件包不重做，原底层原因仍未知。

统一短验首轮66用例58通过/8失败，修复CLI数字选项缺陷及两种夹具问题后，12项定向补过；其中包括替换两个受mock泄漏影响的无效原生通过。原记录全部保留，既有DevTools/GUI等通过项未重复。两架构微型真实CMake阶段在原120000/2000ms内分别50590/54085ms自然收尾，Job归零、无强杀；完整Build入口和所有工作负载形态未在本轮实跑。

长测及完整生产资格继续后置。当前实证只关闭上述准备机制的必要短验范围，不把全部旧失败或未观测资源域改为通过。
