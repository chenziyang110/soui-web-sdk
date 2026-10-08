# 续期夹具 v2：两架构已编译，真实 GUI 验证待执行

当前执行状态：x86-attempt1 / x64-attempt1 每架构 13 个步骤均有原始退出码 0；两次外层 Build 也已有 Capture-Command 原字节 stdout/stderr 与数值退出码 0。两架构无 GUI native self-check 均为 records=8、fixtureRevision=2、phaseCases=45；分析器 106 项合成用例全部通过。这些结果证明编译及对应无 GUI 检查，**v2 尚未执行真实 GUI、smoke 或实际时长续期验证**。

新的 compiled freeze SHA-256 为 `80d19d4e2008a505bbb8a9293849f3756d4bf07ad7838ad38aa5bb054ac0ee3a`。当前汇总与原始命令记录见 `execution-status.json`（归档时生成）、`command-logs/`、`attempts/*/logs/`；一次依赖审计保留在 `audit-build-20260926/`。原 v1 的真实失败不受这些构建成功结果影响。

本轮依赖审计实际退出 1：npm advisories 服务在 TLS 建连前断开连接，因此漏洞统计不可用，不能宣称“0 漏洞”。没有重复尝试；两架构 package/lock 原字节相同，两个完整工具链审计前后未变。

首轮捕获器对只读描述符 fsync 导致 EPERM，未持久化 child 退出码；该失败及旧脚本保留在 `command-logs/prepare-verify-attempt1/` 与 `capture-wrapper-history/`，不能算通过。修正为 r+ 后的独立 attempt2 才有完整退出记录。宿主 vcpkg 引入未冻结链接输入的限制仍存在，不宣称封闭可复现或干净机通过。

## 源码准备历史记录

以下内容保留最初准备阶段的说明；其中“未编译/未执行”和待执行命令只描述当时状态。冻结 PROTOCOL、preparation-status 和历史原件未被覆写。当前构建结果以上述执行状态及原始记录为准。

本目录只完成文件准备与静态实现。**未编译、未运行 native self-check、未执行合成分析器自检、未启动 GUI/VM、未访问网络，尚无新的 freeze.json。** 当前 SDK、旧夹具、旧冻结和仓库证据均未修改。没有续期、smoke 或生产通过结论。

## 修改范围

- `consumer/session_observation.h`：仅应用使用的有界 JSON 元数据投影与生命周期状态。处理转义、Unicode、重复键、结构/长度上限；token 值只跳过校验，不进入观察对象。
- `consumer/main.cpp`：在唯一 bootstrap 调用前记录开始，在 SDK 成功返回后、原响应交回页面前绑定 sessionId/origin；首个 describe 获取授权 scope 并核对已绑定身份。至多一次 bootstrap 调用前、尚无任何业务/授权绑定的 opaque 生命周期退役可单列记录。绑定后包括 describe/ready 前的异常关闭仍失败；正常关闭业务会话恰好一次。
- `consumer/session_observation_tests.h`：纯解析/阶段状态用例，由后续 `--self-check` 在 COM/Runtime/Host/窗口创建之前执行。本轮仅写入，没有执行。
- `Verify-Run.cjs` / `SelfTest-Verify-Run.cjs`：采用明确 v2 NDJSON，核对原生 bootstrap 和 shutdown 事件、身份、阶段、顺序及最终计数；保留所有原续期/任务/订阅/清理约束。合成样本仅用于验证分析器，不是真实长测。
- `Prepare.cjs`：后续构建只检查本目录已复制的固定 SDK 源码输入，不读取开发工作树；将 v2/父冻结身份带入新的构建记录。`Build.ps1`、`Run.ps1`、Web 页面沿用 v1 固定代码。

## 不变输入与历史依据

`Clone-V1.cjs` 已执行一次纯文件复制和散列核对；`clone-provenance.json` 记录修改前基线、原路径及散列。原 SDK 输入共 937 文件、273,676,732 字节，SHA-256 `29548090b14d3e8da3ef5524bdd413911db039a22b5d4d1732ddc39f651b6574`；源码 711 文件 SHA-256 `a0c1d7900d39555508288d5459e65d3852e543cf7ec3879126bd6af59d2c2550`。

`provenance/parent-freeze.json` 绑定父冻结 `7e45bc6158db50362e088ba8a97a08e42e7e63e398dde62df1dbe6ce6721620d`；`provenance/failed-x86-smoke/` 保留退出 1、ready/任务均 0 的失败报告、监督器和诊断。这个失败与初始 Host 生命周期退役路径一致，但由于旧日志没记录收到的 session ID，不能声称其精确运行原因已证实。

没有复制旧 attempts、node_modules 或编译后的夹具。输入中的公开安装 SDK 静态库、固定运行依赖属于原 937 清单，保持原字节。两个 toolchain seed 文件也保持原样。`provenance/parent-drivers`、`parent-protocol` 提供修改前参照。

## 下一步（本轮没有执行）

先做独立静态评审，重点检查 JSON 投影、并发下的 bootstrap/关闭边界及分析器负例。再按顺序执行：

```powershell
Set-Location C:\SOUI-SDK-production-validation\preview11-real-renewal-preparation-v2-20260926
node .\Prepare.cjs verify
node .\SelfTest-Verify-Run.cjs
powershell.exe -NoProfile -File .\Build.ps1 -Architecture x86 -AttemptName x86-attempt1
powershell.exe -NoProfile -File .\Build.ps1 -Architecture x64 -AttemptName x64-attempt1
node .\Prepare.cjs freeze x86-attempt1 x64-attempt1
```

后续 Build 会安装锁定的工具链依赖、做严格 TS 与 native 编译、调用无 GUI self-check；必须保留每步原始失败。两架构成功后才创建新的 compiled freeze。真实 smoke/35 分钟/8 小时运行仍须由主流程确认桌面与外部 QEMU 门禁，串行执行并使用本次冻结分析器；本 README 不授权跳过这些步骤。

## 待审风险与验收用例

- 原 v1 真实链接受宿主 vcpkg 全局集成影响，额外 LIBPATH/库未纳入冻结。v2 沿用工具链时此风险未解决，不得宣称封闭可复现或干净机通过；后续构建仍须保留实际链接命令。
- 新 C++ 观察代码未经编译；MSVC 兼容性、异常路径和计数输出仍待验证。SDK 公共 JSON 返回可提供 sessionId/origin，但授权 scope 只能到首个 contextual request 才取得；实现没有将二者混淆。
- 必须覆盖：无初始退役/一次初始退役正常关闭；第二次退役；bootstrap 调用中关闭；绑定后 describe 前、ready 前、ready 后关闭；错误 session、重 bootstrap、重复最终关闭；转义 method/重复 key/无效成功响应。不能用“ready 前全部忽略”替代这些检查。
- 阶段观察只改变夹具判定，不更改时钟、期限、原生响应、bootstrap 次数、任务或订阅策略。真实 IME/滚动/缩放与 Workbench V02 仍不由这个夹具代替。

所有运行/续期/资源/性能/生产通过标志仍为 false。
