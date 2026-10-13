# MessagePort / Dedicated Worker 有限行为 fixture

这是独立 installed-SDK 消费者。六个 C++/CMake/Web/Worker 文件按此前真实四臂负载的原始字节搬移；普通 tests/acceptance-app 保持原负载。CMake 单独配置，保留 SouiAcceptance WIN32 目标、/MD /U_DEBUG、公开 find_package(SouiSdk) 与三份资源部署，所以共享 stage/run 无需改 EXE 文件名。不要把本叶添加到正常根 native tests 默认构建：它需要真正公开 Web SDK 构建产物、安装 SDK、实际软件 kernel 和桌面运行。

准备入口（SOURCE 提案，须经共享改动审查和真实入口复验后采用）：

```powershell
./tools/Test-Acceptance.ps1 -Architecture x64 -Configuration Release -Fixture message-port -PrepareOnly -FreshConfigure
./tools/Test-Acceptance.ps1 -Architecture x86 -Configuration Release -Fixture message-port -PrepareOnly -FreshConfigure
```

默认 Fixture=ordinary 不变。message-port 分支只允许 PrepareOnly；它仍执行原锁 npm ci、正常 SDK/Web build、公开 Web typecheck/Vite、SDK install、独立消费者 configure/build 和 stage，不跳过生产流程。普通 build-web 自动跟随新目录的 main.ts，并把 web/public/message-worker.js 原样部署。本 fixture 不替代原软件资源工作负载：原 100 measured +10 warmup / settle5000 / timeout20000 的读取器、容差和普通消费者完全独立。

运行使用 tests/acceptance/message-port-ab-controller.cjs 原已验证的 Root 控制协议：真实 prepare/stage 完成后另生成并审查四臂 inputs 与四个严格 Owned 计划，再由唯一 Root 执行。控制器需要固定 gate ROOT_CURRENT_MESSAGE_PORT_AB_V1、input raw SHA、arm key 和明确启用的真实 inputs。旧 packet 的 inputs/BuildID/source SHA/EXE hash/UUID 不得搬成新输出的证据。此提案不提供未来 EXE hash 或可执行 cfg。operator guard 仍须 DesktopValidation 互斥、清除并恢复 Node/诊断环境、提前验证真实输入/所有祖先、显式新建公共 runs 父目录，并保持原 strict Owned envelope；不能把准备退出0当行为通过。

四臂使用每架构同一实际 EXE、Web、CRT 和安装 SDK；唯一 runtime 差异是 node.dll。新 candidate suite 必须调用持久的 tests/acceptance/message-port-stage-candidate.cjs；它向共享 runner.prepare 明确传入第五参数 message-port，再按原 stage 验证创建，不在已有 build 身份中替换 DLL。A/B 控制器先原 runner 验证 3 reload cycles、0 warmup、settle500、timeout20000，9 lifecycle steps /7 samples、实际 root PID/string FILETIME 和整个 Job 自然闭合，再用 message-port-report-reader 验证每臂4文档/32 browser cases/40 sidecar rows。原 native processTimeout214500 / Owned394500 / drain2000 与 outer strict 0/0 保持。普通软件资源套件不能使用这个 observer Web 来冒充原负载。

八项覆盖双向 FIFO/多消息异步、排空后 refill、页面线程内 transfer、close 后下一 turn 的有限抑制、回调重入、Dedicated Worker FIFO、self.close 与 terminate 请求后的120ms有限消息静默及页面定时器前进。消息端口不能传到 Worker（原 vendor main-thread assertion）。有实际 pulse 才请求 Worker 停止；最终 terminate 只清理 fixture 残留。native action acceptance.message-port.report 使用原 typed validator/真实 session scope 授权，caseId/status 枚举带已修复 maxLength24/11；sidecar owner 在所有 window/Host/Bridge 关闭后析构，页面收到真实当前 session ACK 后才记录身份，最后报告 ACK 完成后才走原 ready。

历史私有 fixture 的 x64/x86 baseline/candidate 四臂已真实通过，共16文档/128 cases/160 sidecar rows；这不能证明本次新目录/共享入口已测试。Worker 原生 join、debugger通用 wait/kill、polyfill scheduler 实际分支、完整 kernel 构建、资源增长、OS 和正式发布资格仍未验证。新入口采用前需在正常源交付中重新 PrepareOnly、stage 和四臂 strict Owned 复验，并检查默认 ordinary prepare 仍选择原普通源码。
