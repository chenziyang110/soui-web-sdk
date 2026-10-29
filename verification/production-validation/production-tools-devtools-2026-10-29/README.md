# 生产工具与 DevTools 必要短验（2026-10-29）

这是已发布v0.6.0之后的源码候选短验记录，基线为`8b7b57341d78d101ebc317de6cef8ac19b4a7531`。不替换Release资产，不重新授予完整生产资格。机器事实见[Summary.actual.json](Summary.actual.json)，复制原始记录的字节/SHA256/来源见[Files.index.json](Files.index.json)，最终只读复核见[Final-review.json](Final-review.json)。索引只覆盖124份复制的原始记录，不包含本说明、摘要、最终复核及索引自身，避免摘要与复核相互循环绑定。

| 必要范围 | 实际结果 | 证据与限制 |
|---|---|---|
| ZIP、SDK发布错误、构建策略和受影响契约 | 64个独立逻辑用例获得通过结果 | `tools/initial/`首轮55通过、9失败；`tools/targeted-repairs/`只补失败9项并通过，未重跑原55项。不是一次全绿的64项运行 |
| DevTools受控adapter授权契约 | x86/x64 Release各实编/运行10场景，四个Job自然归零 | `native/accepted/`。原SDK安装库及相关57文件每架构身份未变；不是实际Engine/Inspector GUI测试 |
| MiniBlink x86 Release保持真实断点暂停后原生关闭 | 原120000ms/2000ms预算内，86414ms完成；root=0、active=0、无强杀 | `vm/accepted/`；真实main.tsx:12暂停及原生业务X关闭，无人工Resume。PID1760，出生时间134377149663488156 |
| 独立静态审查 | 代码修复及测试边界已复核 | `reviews/`；原始早期review的旧测试pin保留，新公共session断言以`native/accepted/independent-static-review.json`为准 |

首轮失败全部保留。工具9项失败来自PS5读取夹具缺少`System.IO.Compression`程序集、全新工作树未创建`out`目录；修复仅限夹具。原生首轮私有`running()`访问编译失败见`native/initial-failed/`，两处断言改用已有公共session状态后实编通过。GUI首次`vm/initial-failed/`在120046ms超时后强清，无root-exit事件；它仍为失败，不能被第二次通过覆盖。

第二次GUI由同工作树的另一个续接聊天操作。用户手动停止该任务后，Root复核原截图和点击序列，导出该次原始数据，经既有严格serial extractor和公开`validateStage`重新校验，直接复用通过记录，未再运行产品。应用日志的`E_DEVTOOLS_PENDING`、`E_CURSOR_CLOSE_PENDING`保留，随后为`host-close-complete`。原EXE、SDK锁、kernel、离线前端和runner身份见摘要与客体`Inputs.actual.json`，没有把旧载荷挂到新工具fingerprint。VM输入租约已释放，本任务浏览器页已关闭。

ZIP验证使用真实producer处理微型Unicode载荷，检查local/central UTF-8标志与PS5跨代码页内容闭集；默认/显式源码根使用隔离stage夹具，不等于完整源码绑定资格。MSBuild回归执行真实PowerShell AST片段的12种组合，不是完整产品构建。原生产C++、renderer、kernel和prebuilt未修改，先前对应输入未变的通过项不重跑。

原始记录中的绝对路径、PID、时间和编译缓存属于当次环境身份，不是源码配置。只保留诊断需要的环境值，不提交VM租约、凭据或构建二进制。

`releaseReady=false`、`productionQualificationComplete=false`、`fullDevtoolsGoalComplete=false`。本次GUI只覆盖一个精确MiniBlink x86 Release消费者和环境，不覆盖完整D06、WebView2暂停关闭、x64或Debug，不推断heap/GDI或内部计数器资格。VCTIP自然退出、EPERM锁持有者、PS5长stage旧失败根因仍未解；D07真实30分钟、D13千次、D14八小时及完整性能/环境/安全/许可/签名资格继续交给[后续验收](../../../docs/testing/v0.6.0-long-run-handoff.md)。
