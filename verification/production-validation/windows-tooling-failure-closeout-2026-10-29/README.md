# Windows 工具遗留问题：必要短验

本轮基于PR127合并后的main `b9d53f9e182bfd335c9bd6fb23690b40718999ef`。代码集中完成并经独立静审后才执行短验；没有运行VM、产品窗口、生成的微型程序或长测。机器事实见[Summary.actual.json](Summary.actual.json)，167份复制原件的大小、SHA256及来源见[Files.index.json](Files.index.json)。索引不含本说明、摘要与最终复核，避免循环绑定。原件中的绝对路径、PID和时间仅是当次环境身份。

| 范围 | 结果与限制 |
|---|---|
| 新增及受影响工具契约 | 66个独立Node逻辑用例获得通过结果。首轮58通过/8失败；修复后只补12项：9个相关archive用例、1个含9场景的MSVC模型、2个SDK恢复用例。MSBuild策略另含12组合。不是一次全绿的66项运行 |
| 实际PS5与7-Zip | 中文、空格及超过260字符路径内容正确；真实非法路径/链接ZIP在成员检查后拒绝。单调期限覆盖路径遍历、128KiB块散列、原生命令和发布检查；零退出的空/部分/错字节不能发布 |
| 实际EPERM与恢复 | PS5持有子文件句柄时，Node目录rename实际返回EPERM并保持失败；关闭该真实句柄后，独立恢复自然成功，同manifest SHA256。新候选公开pack至`C:/sc07/dist/sdk/0.6.0`也成功，1501文件/112873502载荷字节经完整闭集及内容验证 |
| 当前MSVC生命周期真实短验 | x64 PID60820/FILETIME134377193591860325，50590ms；x86身份见摘要，54085ms。各自同一原120000ms root/2000ms drain内完成configure与build，两阶段VCTIP各有PID/出生时间/映像及原始owned-member证据。root0、supervisor0、active0、无强杀 |

首轮8项失败完整保留。实际产品工具缺陷是CLI选项词法校验不允许`--manifest-sha256`中的数字，已修正；其余为测试夹具的嵌套mock泄漏与PowerShell script变量/iterator别名。mock泄漏导致两个原“实际ZIP拒绝通过”并未实际调用原生边界，它们不能作为有效原生证据，已用更强断言重新验证。原错误记录不改写，未受影响的通过项不重跑。

真实编译第一次在已有VCTIP ready event处拒绝，root1/supervisor1、active0、无强杀，尚未编译。按用户此前的持续授权，另外核验无活动编译、原父进程退出、PID出生时间/映像一致且CPU不变后，清理4个旧辅助进程；清理记录与新编译样本分开，不能被计为自然退出。之后一个新的forward attempt两架构自然通过。原八个发布期compiler失败继续保留，其中一例有多个残留成员，不把所有旧失败都归为VCTIP。

真实短验复用原封`C:/sr06` runner及原reader；原prepared `genericSupervisionPassed=null`保持，新Job的自然闭合由本轮raw/context/ready/finished独立证明。不宣称新`Build-ValidationTools`已prepared，也没有执行完整Build入口、完整SDK/内核重编或生成程序。`native/harness/README.md`是作者执行前交接原件；实际执行结果以本轮accepted目录为准。

canonical-pack是本候选工作树的本地验证checkpoint，未上传、安装或替换任何已发版SDK，旧封存`C:/sr06`原目标也未改。manifest保留该次精确输入身份；随后仅更新验收说明等文档不据此改写该package manifest。EPERM历史锁持有者与PS5原空stage的底层原因仍未知；本轮证明新的有界失败恢复与校验准备路径可用，不伪称历史归因完成。

`releaseReady=false`、`productionQualificationComplete=false`、`fullDevtoolsGoalComplete=false`。固定MSVC/VCTIP二进制之外及所有长编译负载形态另验；D07/D13/D14、完整性能/环境/安全/许可/签名资格继续按[长测交接](../../../docs/testing/v0.6.0-long-run-handoff.md)后置。原已通过的DevTools、GUI、发布归档与未变依赖输入未重复测。
