# PR 合并必要短测（2026-10-28）

本轮按维护者要求顺序处理 Windows SDK、输入调度、渲染、工具和依赖 PR，跨平台/多架构 PR103 排除。未运行长测。源码合并与生产认证分开，`releaseReady=false`、完整 source-delivery/性能/平台资格均未关闭；固定 prebuilt 未替换。

## 实际结果

各组分别报告，不能将重叠案例相加。

| 范围 | 结果 |
|---|---|
| 集中 x64 原生短验 | 93 个唯一 CTest：首次88中86通过，两个夹具修复后仅复验2项，再补5项router/scroll；没有重新运行整套93项。 |
| 同产品工具的数据合同 | Node50、PowerShell recipe57、Owned runner专项7通过；非法参数 preflight 保持严格 Job0/no-force。 |
| AutoHost 与最终栈融合 | 7922源码及六文件修复，x64四指定目标构建与两个 no-view unmount CTest通过。 |
| 必要 Win32 切片 | 同一冻结源码，fresh八目标构建通过，六个明确选择的 CTest通过、无skip；EXE/LIB/OBJ架构均实核为x86。 |
| 中间层必要复验 | PR111/116各六项 timer 夹具短验；PR117本层五目标稳定manifest增量构建与三个mailbox/unmount短例通过。它们不是新的唯一案例数。 |
| 许可、打包与peer集成 | PR101的46项；PR106的201通过、1项文件symlink权限跳过；三个TS包实际构建通过。 |
| Vite/Vue依赖 | acceptance真实legacy构建与临时HTTP server关闭；模板构建和24个devkit合同；Workbench构建、401文件离线检查通过。相应npm audit为0。 |
| Undici工具依赖 | 实际安装7.30.1、锁/SRI与归档一致，一次loopback HTTP往返及关闭通过；未下载或启动Electron。 |

Electron对标工具的npm audit实际退出1：原Electron44.4.4仍有1个high包、2条advisory，Undici无报告项。该审计没有标成通过，Electron后续升级仍开放。

## 修复与原始失败

- 保留MIT随包/安装/运行资源交付和旧peer回退合同，修正当前CMake文档版本。
- JSON读取须匹配原长度，并复核fd与path的身份、时间和大小；三个实际文件竞态先红后绿。
- 历史old-generator恢复原字节，新增两项固定SHA回归，未修改历史raw-manifest。
- Vite精确版本表接纳6.4.5；Vue3.5.44三处兼容站点仍唯一，更新精确指纹，保留原构建失败。
- W00夹具使用真实timer代际；后续deadline层采用其原有idle/timerFired合同。
- 仅修测试中的MSVC显式capture、合法因果context、Release ICF getter及双侧路径规范化，保留产品校验和全部断言。
- 非零根进程保留失败和原退出码，后代获得原drain后再决定强清；未延长预算。
- 中间层source gate只对已逐字节唯一匹配的原fixture body重新绑定whole pin/offset。后续authored更新整组保留，未跳过gate或改成vendor OFF。

原catalog中 `mailbox-context` 的 `originPr=117` 是归属标签错误；该测试实际由PR121的 `0300cbf6` 引入，修复在PR121带回。原记录字节保持不变。

## 身份、复核与原件

[集中结果](native-VerificationSummary.json)保留实际起点aa30、各失败/修后过程与产物；[融合结果](native-IntegrationVerification.json)及[Win32结果](native-x86-final-Verification.json)绑定7922与六个修复文件。[修复catalog](native-fix-catalog.json)给出各原/新文件和补丁pins。[最终源码等价](FinalSourceEquivalence.json)在最终PR123暂存区重新核对：23723项产品blob与已编融合源码差异0，133个compiler-read输入及六修复均逐raw相同，起止digest一致。

新提交没有被写回旧日志冒充重跑。最终main只在上述相同输入闭集内引用已有短验；Electron两个metadata另由其自身短验覆盖。这里归档原机器JSON与选定短验日志，完整编译tlogs、工具和二进制产物保留在维护者本地归档；这些索引不等于完整原始构建包。

## 后续专人

沿[原执行交接](../../../docs/implementation/input-ui-specialist-handoff-20261026.md)按实际新候选冻结源码、匹配重编内核/SDK/Host与Runtime，只补受影响或未验项。当前source/helper/no-view测试不认证真实COM/GL、窗口像素、IME/DPI/跨屏、目标OS、物理延迟、GPU采用、长时间资源趋势或完整发布门槛。不要为补数量重复通过组，不以丢输入、忙等、绕过资源fence或无界队列换取性能数字。
