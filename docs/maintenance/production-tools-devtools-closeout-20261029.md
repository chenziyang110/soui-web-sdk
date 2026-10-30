# 打包、构建与 DevTools 必要项收尾（2026-10-29）

基线为已发布 v0.6.0。此文件记录后续源码候选，不修改该 tag、附件或既有验收记录。
当前状态：代码与独立静态审查完成；必要短验已完成，输入未变的通过项未重跑。原始记录见[证据说明](../../verification/production-validation/production-tools-devtools-2026-10-29/README.md)及[机器摘要](../../verification/production-validation/production-tools-devtools-2026-10-29/Summary.actual.json)。此处的完成仅指本轮集中收尾范围，完整生产资格仍待验。

## 修改范围

- ZIP：公开源码归档与压缩档位的 ZIP 参数增加 `-mcu=on`，控制文件名字节及 ZIP bit 11；`-scsUTF-8` 仅控制列表输入。应用、Bundle 与测量入口通过共同档位复用；LZMA2 档位不变。
- MSBuild：普通 MSVC 与 LLVM 配置/构建期间显式设置进程局部 `MSBUILDDISABLENODEREUSE=1`，MSVC build 传 `/nr:false`，finally 恢复原 presence/value。两入口输出 `native-invocation-<Configuration>.json`，只记录实际 CMake argv 和节点复用变量，不输出全环境。
- 构建尾部：`build-closeout.json` 保存原始报告绑定与最后一次 root 退出后的成员观察，包括 PID/出生时间/映像及读取错误。它不是清理瞬间的完整成员证明，`membersAtCleanupVerified=false`，不授予新的通过状态，不改变原截止和 forced-cleanup 失败含义。
- SDK 发布：保留原 rename 的 Error/code/errno/syscall、最多六次尝试和总计450ms等待；最终错误附 `publication`，记录清理前 stage/output 状态。若 stage 清理也失败，首次错误保持，另附 `cleanupFailure`，CLI 仍返回非零并输出一条 `SDK diagnostics:` JSON。
- DevTools：新增 grant 替换、原生文档/generation 改变、关闭 Host、未派发 WM_TIMER 时到期拒绝及撤销后重新授权用例。静态未发现需修改的产品授权或暂停关闭语义；MiniBlink 专用 close wake 和真实资源 fence 保持。

## 可复用证据与新增短验

已发布载荷的八次默认运行、四个诊断消费者五个自然阶段、TSX断点/已观察单步、构建、SDK安装消费者和归档/远端资产身份继续保留。只对本轮改变的工具或新增契约执行有限回归；不因为提交号变化重跑全套。

本轮实际短验：

1. 工具共64个独立逻辑用例已获得通过结果：Unicode ZIP、source SDK 首错与清理失败诊断、MSBuild环境/argv及受影响构建契约。首轮55通过、9失败；修复PS5读回夹具缺少Compression程序集和新工作树缺少out目录后，仅重跑失败9项并通过。原失败保留，不称首轮全绿。
2. ZIP 使用微型中文载荷经过真实producer，检查local/central文件名、bit11和PS5跨代码页逐文件内容；默认/显式源码根使用隔离stage夹具，不据此声称完整源码绑定或已发版包重验通过。MSBuild用例执行真实PowerShell AST片段的12种组合，不冒充完整产品构建。
3. DevTools facade 在未变的原SDK安装库上完成x86/x64 Release实编，各10个受控adapter场景通过，四个编译/运行Job自然归零。最初私有running()调用编译失败保留，改用公共session状态后重编。受控adapter不构成后端GUI验收。
4. 后台QEMU中MiniBlink x86 Release真实main.tsx:12断点暂停后，直接点击业务窗口原生X，无人工Resume。PID1760/出生时间134377149663488156在原120000ms/2000ms预算内于86414ms自然收尾，root=0、Job active=0、无强杀；应用日志保留E_DEVTOOLS_PENDING与E_CURSOR_CLOSE_PENDING后记录host-close-complete。第一次120046ms超时/强清记录保留，不改为通过。
5. GUI成功例来自同工作树的另一个续接聊天；用户手动停止该任务后，Root复核截图/操作序列，导出原始记录并用既有公开validator重新校验，复用该次通过结果，没有再运行产品。最后仅关闭本任务控制台，释放VM输入租约和本任务浏览器页。

`Build-ValidationTools.ps1` 是 prepared fingerprint 输入，修改后不得把旧 prepared 元数据改挂到新脚本。复用已发版原封载荷做新的 GUI case 时，仍使用其原配套源码、工具、SDK和Runtime身份，不声明运行了本候选新构建入口。

本轮没有产品C++、renderer、kernel或预编译依赖变更；两架构原安装SDK相关57文件的前后身份保持。GUI结论仅覆盖一个精确MiniBlink x86 Release消费者及当前Windows/Runtime环境，不覆盖完整D06、WebView2暂停关闭、x64或Debug。自然Job关闭和host-close-complete支持既有退休契约，不推断未观测的heap/GDI或内部计数器资格。

## 保留限制

VCTIP尚无受支持且实证有效的自然退出修复。禁止添加已否证或未知编译开关、延长原尾部预算或把强清改为自然成功。MSBuild节点策略与VCTIP分别报告。

EPERM锁持有者仍未知；新增诊断只报告可观察事实，不归因Defender或MAX_PATH。清理前状态不是锁持有者取证。清理中途失败可能已经删除部分stage，只保留剩余内容与原错误，不承诺完整失败快照。PS5长临时stage原失败无确定根因，独立保留。

D07真实30分钟、D13千次、D14八小时及完整环境/性能矩阵沿[长测交接](../testing/v0.6.0-long-run-handoff.md)后置。三项资格继续false，不手工修改acceptance或伪造通过。
