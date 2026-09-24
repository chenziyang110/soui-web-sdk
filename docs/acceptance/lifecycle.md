# 公共 SDK 生命周期验证记录

起始日期：2026-09-22；更新：2026-09-23。历史候选为 `0.3.0-preview.3`，文末另列当前 `0.3.0-preview.4` 固定包复测。范围：独立消费者、窗口生命周期与既有应用回归。生产准入保持未通过。

## 冻结三补丁候选的 1,000 轮结果

SDK/工具指纹 `e29dc0d908c7`、x64 内核 `2ed34e54c26f`，3 次预热加 1,000 次测量，每轮静置 20 ms。以下样本不包含后续标准 Web 连接器和业务负载。原始日志、构建身份与全部采样均保留。

| 模式 | 操作结果 | Private Bytes 预热 → 末轮 | 普通句柄 | 线程 | GDI | USER |
|---|---|---:|---:|---:|---:|---:|
| [重载](../../verification/production-validation/96823023-6bef-437f-b623-447093d95089/reload-9eef9083-7cf6-4572-8e37-857eb122477a/result.json) | 正常退出，1,000 轮完成 | 32,710,656 → 104,255,488 | 294 → 289 | 14 → 13 | 60 → 32 | 23 → 23 |
| [销毁重建](../../verification/production-validation/96823023-6bef-437f-b623-447093d95089/recreate-0464659e-322c-4422-acc4-243022394065/result.json) | 正常退出，1,000 轮完成 | 26,800,128 → 67,948,544 | 284 → 302 | 12 → 16 | 55 → 27 | 9 → 14 |
| [三窗口](../../verification/production-validation/96823023-6bef-437f-b623-447093d95089/multiwindow-991b5586-8a1a-4c48-b5ee-568ab9328d41/result.json) | 正常退出，1,000 轮完成 | 51,294,208 → 109,068,288 | 284 → 298 | 15 → 13 | 57 → 29 | 9 → 13 |

重载、重建和三窗口 Private Bytes 峰值分别为 129,646,592、94,580,736 和 150,278,144 字节；最终 Runtime 关闭后分别为 10,043,392、24,465,408 和 47,394,816 字节。此前逐 WebView 累积的 GDI/USER/普通句柄模式已改变，但内存增长、关闭后的线程和句柄仍须解释，不能把两个端点或正常退出当作整体资源门槛通过。`resourceGate` 继续为 `not-evaluated`。本候选 x64 三模式均完成；x86 1,000 轮及新版连接器的长循环仍未执行。

按每 100 个测量周期分段、不删除异常值，重建 Private Bytes 中位数依次为 48.3、52.2、57.4、56.0、60.6、60.6、65.7、65.5、65.6、68.6 MiB，仍有上升趋势；[完整分段数据](../../verification/production-validation/96823023-6bef-437f-b623-447093d95089/recreate-0464659e-322c-4422-acc4-243022394065/resource-windows.json)保留全部统计方法。重载分段中位数在 73.6–103.4 MiB 间波动，不能用重载的回收波动解释重建的增长。

## 验证方式

`tests/acceptance-app` 是普通公共 SDK 消费者：安装 SDK 后通过 `find_package(SouiSdk)` 编译；不链接 Demo 或 SDK 私有实现。每个真实 WebView 完成 bootstrap、窗口身份回显、Bridge probe 和页面 ready；监督程序核对原生步骤、进程退出、源码及整个运行载荷。超时或强制终止始终失败。

三种模式分别验证页面重载、同进程销毁重建、三窗口并存后关闭中间窗口并重新创建。每轮三窗口还要求另外两窗重新完成身份回显。最终检查活跃 Host 阻止 Runtime 关闭、所有 Host 销毁后真实关闭、关闭后拒绝重新初始化及重复关闭幂等。

## 发现、修复与证据

旧版每个窗口各建引擎线程并初始化/关闭全局内核。独立探针复现了第二次创建的访问冲突，以及第二个并存窗口挂载卡住。源码核对发现内核线程及 JS 同名绑定是进程级状态。修复按 [ADR-0003](../adr/0003-process-runtime-ownership.md) 将引擎所有权提升到进程级，并从当前 WebView 解析 Bridge 接收者；窗口仍有独立会话、资源根和 Bridge worker。保留的事件 sink 使用可撤销的生命周期引用。

| 证据 | 参数与结果 | 原始记录 |
|---|---|---|
| preview.2 基线 | x64，各 2 轮；reload 通过；recreate 第二次挂载退出 `0xC0000005`；multiwindow 第二窗口卡住，由 240 秒监督超时终止 | [基线目录](../../verification/production-validation/323e6466-9520-4a5d-a3b4-1ac9afdad688/build.json) |
| 初步边界回归 | x64，各 3 轮，全部通过；该源码尚未加入最终事件 sink 防护 | [边界回归](../../verification/production-validation/4aadf8b4-4713-4ae9-b58f-0cb4485b2883/build.json) |
| 当前重载 | x64，100 轮加 3 次预热，每轮静置 1 秒；操作通过、无崩溃或超时；资源门槛未判定 | [结果](../../verification/production-validation/e828a762-2950-409f-b534-023cc9bcb669/reload-c8079e54-37c7-4f60-8fd4-834bbef72e34/result.json) |
| 重载循环基线 | x64，1,000 轮加 3 次预热，每轮静置 20 ms，操作通过；约 219 秒。该口径服务生命周期循环，不替代上面的 1 秒静置资源观察 | [结果](../../verification/production-validation/6599a4be-03b8-43e3-a2ce-4d83de1fe7c0/reload-fcfdb440-53e3-4058-bde3-a1826bca7ff9/result.json) |
| 重建循环基线 | x64，1,000 轮加 3 次预热，每轮静置 20 ms；操作及正常退出通过，但窗口资源清理失败 | [操作结果](../../verification/production-validation/6599a4be-03b8-43e3-a2ce-4d83de1fe7c0/recreate-a51b255c-2664-4b60-8c5b-b7fc43ddb075/result.json)、[资源判定](../../verification/production-validation/6599a4be-03b8-43e3-a2ce-4d83de1fe7c0/recreate-a51b255c-2664-4b60-8c5b-b7fc43ddb075/resource-observation.json) |
| 三窗口循环基线 | 计划 1,000 轮；因重建资源缺陷已有充分证据，主动请求关闭后以 exit 1 退出，未完成且不算通过 | [结果及中止原因](../../verification/production-validation/6599a4be-03b8-43e3-a2ce-4d83de1fe7c0/multiwindow-28bfbb54-fa23-4fde-9bc7-0b85f43c2df2/operator-note.json) |
| 当前两架构覆盖 | x86，各 3 轮，全部通过，包含最终退出边界及事件 sink 防护代码 | [x86 构建及载荷](../../verification/production-validation/c833702e-6a2a-4965-9049-f6254af28cb8/build.json) |
| 模板和 Demo 回归 | x86/x64 Release，各 3 个实际桌面入口通过；包契约 13 项、交付身份 7 项、Web 客户端 6 项和原生契约通过 | [x64](../../verification/production-validation/regression-3ff5ce4056ae/x64-smoke-Release.json)、[x86](../../verification/production-validation/regression-3ff5ce4056ae/x86-smoke-Release.json) |

当前重载、x86 以及应用回归对应源码指纹 `3ff5ce4056aefa40942a74726c0d9770e2ae2eeda1c7482e8b70ebc26d011f41`。完整版本、SDK/消费者 build-id、载荷散列及逐轮数据见[索引](../../verification/production-validation/index.json)。开发版继续变化后，这些只能证明该指纹对应的行为。

首次探针曾把普通原生调用当成同步字符串解析，导致页面 ready 失败；实际普通调用返回 Promise。修正消费者后才产生表内基线，早期错误夹具不计作 SDK 缺陷。模板的返回类型仍须在标准连接器工作中校正。

## 100 次重载的资源观察

| 采样点 | Private Bytes | 线程 | 普通句柄 | GDI | USER |
|---|---:|---:|---:|---:|---:|
| 预热结束（cycle 3） | 30,887,936 | 14 | 295 | 60 | 23 |
| 最后一轮（cycle 103） | 40,386,560 | 20 | 316 | 32 | 25 |
| 显式 Runtime 关闭后 | 9,494,528 | 15 | 271 | 12 | 6 |

末轮与预热基线相差 9,498,624 字节，约 9.1 MiB；采样中存在回收波动，Private Bytes 峰值为 57,634,816 字节。这个差值低于方案候选数值上限，但线程、句柄残留及逐时趋势仍需解释，不能仅凭两个点把整体资源清理标为通过。结果中的 `resourceGate` 仍为 `not-evaluated`，不主动压缩工作集或重启进程代替回收。

## 构建失败及限制

随后 1,000 次销毁重建测试在 cycle 417 的窗口全部关闭采样中记录 Private Bytes 64,245,760、普通句柄 706、GDI 860、USER 429；cycle 3 对应为 27,136,000、282、60、12。运行中先观察到 539 个 `MbToolTip`；[cycle 943 的独立窗口枚举](../../verification/production-validation/6599a4be-03b8-43e3-a2ce-4d83de1fe7c0/recreate-window-classes.json)记录 944 个该类窗口，当时只有一个业务窗口。源码确认 `WebPageImpl` 创建两种 ToolTip，却只析构普通实例，漏掉 `m_validationMessageTip`。已添加[最小源码补丁](../../third_party/patches/miniblink/README.md)，对应内核重编译及隔离运行仍待完成；当前运行用的固定 DLL 保持原样以完成并保留失败基线。不能把这次已完成操作当作资源门槛通过。

x86 首次完整构建在资源 staging 目录改名为 `resources` 时出现 `EPERM`。部署器恢复旧目录并返回失败；未改源码重试成功，随后真实启动通过。[失败日志](../../verification/production-validation/regression-3ff5ce4056ae/x86-first-build-failed.log) 保留。未确认导致改名失败的具体占用者，不把重试成功描述为部署缺陷已经修复。

此轮不是完整业务压测：空白验证页面不覆盖长任务、事件洪峰、输入/呈现延迟、托盘、对话框或多屏；保留事件 sink 的并发压力回归还需补充。1,000 次各模式、8 小时混合业务、公开桌面模块、Electron 对照、独立升级回退、兼容矩阵及安全/授权门槛持续追踪于[完整执行记录](progress.md)。

当前 x64 内核源码修补已完成 `Build-Dependencies.ps1 -Architecture x64 -Component All`，见[构建记录](../../verification/production-validation/kernel-tooltip-x64/build-All.json)。新 DLL 在隔离消费者中复验后才能更新固定依赖；编译成功不关闭资源缺陷。新的 v2 suite 同时保存实际消费者源码和候选内核输入，旧 v1 报告继续作为历史基线保留。

## 分步验证内核修补

第一个候选仅补齐校验 ToolTip 释放，内核 SHA-256 `32d37e6399bc04e2a8d909c4021f49e3b8f21521b805f9f118f15e111ea0e59f`。在 [v2 独立消费者](../../verification/production-validation/e1661d6d-8f09-45f3-9340-02dd29551669/build.json)中，三种模式各 30 轮加 3 次预热、静置 100 ms 均正常退出。重建模式 USER 增量为 0，但 GDI 与普通句柄各增加 30；三窗口模式 USER 增量为 0，GDI 增加 93、普通句柄增加 119。Private Bytes 仍有增长，不能由短回归推断稳定。

第二处源码问题为每个 WebView 创建的拖拽 HRGN 未释放；第三处为渲染工作线程的唤醒事件未关闭。两者均已形成[独立补丁](../../third_party/patches/miniblink/README.md)。后续报告必须绑定实际 DLL 散列，区分只含第一个补丁、包含前两个、包含全部三个的候选，不用“最新源码”笼统指代所有已执行样本。

前两个补丁组合的 x64 内核 SHA-256 为 `980339823c0d25522c236e1b9f44fa36146a969d3aa8624f6ad63271e134dcfa`。其[重建 30 轮结果](../../verification/production-validation/07fe23af-c09e-4845-8415-12bf8db672f4/recreate-4a9e0c5a-8af8-49a1-a6f9-7a9a76c3553f/result.json)正常退出；预热后 USER 保持 9、GDI 保持 55，普通句柄由 289 增至 319，Private Bytes 由 26,398,720 增至 51,105,792。该短回归验证 GDI/USER 释放效果，普通句柄和内存门槛仍未通过。第三个事件句柄补丁的重建与回归继续进行。

三个补丁组合已完成 [x64 全依赖重建](../../verification/production-validation/kernel-three-fixes-x64/build-All.json)，实际内核 SHA-256 为 `2ed34e54c26fe074e020cee6d179ba2330dff7865db6fb3b75df039f4c69e565`。随后在 [v2 suite](../../verification/production-validation/d2af9ba9-7d50-4f79-a0d5-9b921474aace/build.json) 中完成三模式各 30 轮加 3 次预热、静置 100 ms，全部正常退出。下表均为预热末点 cycle 3 到末点 cycle 33，不跨候选混算：

| 模式 | 普通句柄 | GDI | USER | Private Bytes |
|---|---:|---:|---:|---:|
| 重载 | 296 → 320 | 60 → 60 | 23 → 25 | 31,784,960 → 75,579,392 |
| 销毁重建 | 280 → 280 | 55 → 55 | 9 → 9 | 26,501,120 → 51,101,696 |
| 三窗口 | 280 → 281 | 58 → 17 | 9 → 9 | 51,830,784 → 40,607,744 |

重建模式的逐 WebView 普通句柄累积已经在这个样本中消失。三窗口 Private Bytes 峰值为 108,367,872 字节，末轮发生明显回收；重载与短重建仍处于内存增长段。不能用三窗口末点下降抵消其他模式，也不能用 100 ms 静置短回归判定 1 秒静置的 100 次重载资源门槛。显式 Runtime 关闭后，三模式分别仍有 13/13/10 个线程，需另行解释存活线程及缓存所有权。所有 `resourceGate` 仍保持 `not-evaluated`，后续继续完整候选压力和两架构回归。

同样三个补丁随后完成 [x86 全依赖重建](../../verification/production-validation/kernel-three-fixes-x86/build-All.json)。x86 候选 SHA-256 为 `7ebac616b06aae2c86bc6a7775efed5afd6c86a542754e0dbe4e0179935131cc`；[独立消费者](../../verification/production-validation/c35a4d4c-70e4-4629-b6e8-d2e4a87fc760/build.json)三模式各 30 轮加 3 次预热、静置 100 ms 均正常退出。同一口径的 cycle 3 → 33：

| x86 模式 | 普通句柄 | GDI | USER | Private Bytes |
|---|---:|---:|---:|---:|
| 重载 | 311 → 311 | 60 → 60 | 23 → 23 | 28,024,832 → 50,339,840 |
| 销毁重建 | 294 → 294 | 55 → 55 | 9 → 9 | 22,401,024 → 36,282,368 |
| 三窗口 | 294 → 293 | 58 → 30 | 9 → 9 | 35,782,656 → 62,308,352 |

两架构的短样本均未再出现此前每次创建累加的窗口和句柄对象。x86 内存与线程仍需长运行判定；该结果不替代独立 32 位 Windows 目标机兼容性验证，也未将新 DLL 写入固定 prebuilt。

## 三补丁候选的 100 次重载

x64 候选 `2ed34e54c26fe074e020cee6d179ba2330dff7865db6fb3b75df039f4c69e565` 的[新 run](../../verification/production-validation/96823023-6bef-437f-b623-447093d95089/reload-c4134f7c-1363-4579-ba6c-99ffcc195ada/result.json)完成 100 次加 3 次预热、每轮静置 1 秒，exit 0，无监督超时。

| 采样点 | Private Bytes | 线程 | 普通句柄 | GDI | USER |
|---|---:|---:|---:|---:|---:|
| 预热结束 cycle 3 | 30,699,520 | 14 | 296 | 60 | 23 |
| 最后 cycle 103 | 40,546,304 | 18 | 295 | 32 | 23 |
| 显式 Runtime 关闭后 | 8,933,376 | 14 | 255 | 11 | 3 |

逐轮 Private Bytes 峰值为 68,771,840 字节，末点增量 9,846,784 字节（约 9.4 MiB）。[独立数值观察](../../verification/production-validation/96823023-6bef-437f-b623-447093d95089/reload-c4134f7c-1363-4579-ba6c-99ffcc195ada/resource-observation.json)记录末点增量低于方案 `max(16 MiB, 10% 基线)`，同时保留 `resourceGate: not-evaluated`。原生步骤结果保持原样；线程和缓存所有权、逐时趋势及业务长稳尚未闭环，不由此授予整体资源通过。

## 当前标准连接器的 1,000 次重建

更新于 2026-09-23。源码指纹 `b565bd098865`、x64 重编译候选内核 `02bd76ee` 的[公共消费者](../../verification/production-validation/gpu-lifecycle-2026-09-23/cpu-x64-1000-recreate/result.json)在软件呈现下完成 3 次预热加 1,000 次销毁重建，每轮运行真实页面连接器探针、静置 20 ms；进程 exit 0，0 超时。完整[构建身份和每轮样本](../../verification/production-validation/gpu-lifecycle-2026-09-23/cpu-x64-1000-recreate/build.json)与[每百轮观察](../../verification/production-validation/gpu-lifecycle-2026-09-23/cpu-x64-1000-recreate/resource-windows.json)已归档。它不同于上面的旧最小消费者冻结运行，不混算。

| 采样点 | Private Bytes | 线程 | 普通句柄 | GDI | USER |
|---|---:|---:|---:|---:|---:|
| 预热结束 cycle 3 | 36,995,072 | 14 | 305 | 33 | 9 |
| 最后 cycle 1003 | 123,981,824 | 13 | 533 | 19 | 14 |
| Runtime 关闭后 | 29,057,024 | 9 | 273 | 11 | 8 |

每百轮 Private Bytes 中位数为 87.8、90.8、91.6、100.8、99.4、104.2、98.6、105.6、105.7、101.3 MiB；峰值 157,917,184 字节。句柄中位数在 444–485 间波动，出现多次回收，不能根据前 30 轮端点判定线性泄漏。USER 在 cycle 128 随 `CicMarshalWndClass` 和 `SystemUserAdapterWindowClass` 出现一次跳升，后续小幅波动；仍需解释 Runtime 关闭后的 8 个对象和约 27.7 MiB Private Bytes。操作门槛通过，资源门槛保持 `not-evaluated`；不据此宣称生产稳定。x86 当前连接器及其他模式的 1,000 轮、100 次重载和 8 小时混合负载继续独立执行。

### 当前连接器的 100 次重载

同一 x64 冻结载荷随后完成[100 次重载](../../verification/production-validation/gpu-lifecycle-2026-09-23/cpu-x64-100-reload/result.json)：3 次预热，每轮静置 1 秒；exit 0、无超时。Private Bytes 预热末点 42,258,432、末轮 48,599,040，增加 6,340,608 字节，低于方案 `max(16 MiB, 10% 基线)` 的 16,777,216 字节。峰值 103,616,512 字节；每 10 轮中位数呈回收波动，不能只报末点。线程 16→16、普通句柄 319→324、USER 23→23；Runtime 关闭后 Private Bytes 9,723,904、句柄 254、USER 3。[资源数值观察](../../verification/production-validation/gpu-lifecycle-2026-09-23/cpu-x64-100-reload/resource-observation.json)保留完整分段与限制。这组结果满足当前内存数值观察，监督器仍输出 `resourceGate: not-evaluated`，线程/订阅/业务混合负载及整体生产资源门未关闭。

### x86 当前连接器的 1,000 次重建

同一源码指纹 `b565bd098865` 的 [x86 公共消费者](../../verification/production-validation/gpu-lifecycle-2026-09-23/cpu-x86-1000-recreate/result.json)使用独立编译的 32 位候选内核 `bbe3188f`，完成 3 次预热加 1,000 次重建，每轮静置 20 ms；进程 exit 0、0 超时。[构建身份](../../verification/production-validation/gpu-lifecycle-2026-09-23/cpu-x86-1000-recreate/build.json)与[每百轮观察](../../verification/production-validation/gpu-lifecycle-2026-09-23/cpu-x86-1000-recreate/resource-windows.json)已归档。

| 采样点 | Private Bytes | 线程 | 普通句柄 | GDI | USER |
|---|---:|---:|---:|---:|---:|
| 预热结束 cycle 3 | 27,049,984 | 14 | 317 | 33 | 9 |
| 最后 cycle 1003 | 71,786,496 | 13 | 449 | 19 | 14 |
| Runtime 关闭后 | 15,388,672 | 9 | 285 | 11 | 8 |

每百轮 Private Bytes 中位数依次为 71.4、60.9、62.0、70.6、65.2、76.6、74.8、65.8、66.7、72.5 MiB；峰值约 104.3 MiB。普通句柄中位数在 435–462 之间波动，未呈逐轮线性增长。USER 中位数从 9 逐段升至 14；当前尚未证明它是目标业务窗口泄漏还是输入法/系统辅助窗口的延迟创建，需要解释并在独立 32 位目标系统重测。此运行只满足 x86 软件路径的重建操作门；资源门仍为 `not-evaluated`。

### x86 当前连接器的 100 次重载

同一 x86 冻结载荷完成[100 次重载](../../verification/production-validation/gpu-lifecycle-2026-09-23/cpu-x86-100-reload/result.json)：3 次预热，每轮静置 1 秒，exit 0、无超时。Private Bytes 预热末点 36,704,256、末轮 44,261,376，增加 7,557,120 字节，低于 16,777,216 字节数值上限；峰值 76,374,016 字节。十轮分段中位数在约 39–69 MiB 间波动。线程 16→18、普通句柄 334→381、USER 23→25；Runtime 关闭后分别为 13、284、6，Private Bytes 8,437,760。与 x64 相比，句柄和 USER 首尾增量更大，仍需解释对象所有权与系统辅助窗口。[资源观察](../../verification/production-validation/gpu-lifecycle-2026-09-23/cpu-x86-100-reload/resource-observation.json)只确认 Private Bytes 数值线，`resourceGate: not-evaluated` 和 `releaseReady: false` 保持不变。

### 当前连接器的 1,000 次重载循环

[x64 同源候选](../../verification/production-validation/gpu-lifecycle-2026-09-23/cpu-x64-1000-reload/result.json)另完成 3 次预热加 1,000 次重载，每轮静置 20 ms，进程 exit 0、0 超时。这个口径验证频繁导航的操作正确性，不能与每轮静置 1 秒的 100 次资源数值线混作同一预算。预热末点到末轮的 Private Bytes 为 42,938,368→109,813,760，普通句柄 344→465，USER 25→25；Runtime 关闭后 Private Bytes 11,075,584、普通句柄 266、USER 6。[每百轮观察](../../verification/production-validation/gpu-lifecycle-2026-09-23/cpu-x64-1000-reload/resource-windows.json)显示 Private Bytes 中位数在约 132–193 MiB 间波动，峰值 262,529,024，普通句柄中位数在 624–887 间回收波动。它通过操作门，资源门仍为 `not-evaluated`，长期缓存/句柄峰值仍需解释。

[x86 同源候选](../../verification/production-validation/gpu-lifecycle-2026-09-23/cpu-x86-1000-reload/result.json)也在该口径完成 1,000 次，exit 0、0 超时。预热末点到末轮的 Private Bytes 为 37,445,632→142,479,360，普通句柄 334→977，USER 23→23；Runtime 关闭后分别为 7,729,152、265、3。[每百轮观察](../../verification/production-validation/gpu-lifecycle-2026-09-23/cpu-x86-1000-reload/resource-windows.json)的 Private Bytes 中位数约 94–123 MiB、普通句柄 608–926，均有回收波动；峰值 Private Bytes 为 171,413,504。这些较高的峰值及分段变化仍需解释，不能以正常退出等同资源合格。两架构 1,000 次重载均仅关闭当前候选的软件路径操作门。

## preview.4 固定包的生命周期复测

`0.3.0-preview.4` 固定包源码指纹 `f4cb6eba6ff8ed6119d702b81df62c2db8279829afeeb8e035bd28e425cf22c9`，x64/x86 内核分别为 `0801aa929336168c851df4a3f245f5984a88bd01af585aea752404fa07774816` / `3b9071413f704958fb5708b8bb065785a6c3ed03b7fd0fb04f5952201e5fe4f7`。新[原始证据及分段观察](../../verification/production-validation/preview4-lifecycle-2026-09-23/README.md)与上面的 preview.3 候选分开，不跨内核混算。

| 模式 | 当前操作结果 | Private Bytes 预热末点 → 末轮 | 普通句柄 | Runtime 关闭后 Private Bytes / USER |
|---|---|---:|---:|---:|
| x64 重载 100 次、静置 1 秒 | exit 0，无超时 | 41,988,096 → 49,356,800 | 319 → 322 | 10,387,456 / 3 |
| x86 重载 100 次、静置 1 秒 | exit 0，无超时 | 36,974,592 → 41,623,552 | 333 → 344 | 8,065,024 / 3 |
| x64 重载 1,000 次、静置 20 ms | exit 0，无超时 | 42,250,240 → 171,790,336 | 319 → 680 | 10,878,976 / 3 |
| x86 重载 1,000 次、静置 20 ms | exit 0，无超时 | 36,806,656 → 125,550,592 | 333 → 862 | 8,790,016 / 3 |
| x64 销毁重建 1,000 次、静置 20 ms | exit 0，无超时 | 36,257,792 → 122,355,712 | 303 → 551 | 25,337,856 / 7 |
| x86 销毁重建 1,000 次、静置 20 ms | exit 0，无超时 | 29,151,232 → 44,212,224 | 332 → 335 | 19,689,472 / 8 |

100 次重载的末点增量分别为 7,368,704 和 4,648,960 字节，均低于 `max(16 MiB, 10% 基线)` 的数值线；逐轮样本仍有较高峰值和回收。1,000 次重载的峰值分别为 244,695,040 / 169,025,536 字节，20 ms 静置下还有大量普通句柄在运行中存在；这不能套用 1 秒静置的预算。x64 重建末轮 USER 为 13、Runtime 关闭后为 7；x86 对应为 14、8，仍需区分系统辅助窗口与业务对象并解释残留。x86 的每百轮 Private Bytes 中位数后段从约 75–81 MiB 升至约 85–89 MiB，不能仅凭末点较低认定无长期增长。当前只证实列出的操作成功；所有 `resourceGate` 保持 `not-evaluated`，后续需补三窗口、Bridge、资源所有权与 8 小时混合业务。

## preview.5 x86 固定包长循环复测

2026-09-24 的 [preview.5 x86 固定包复测](../../verification/production-validation/preview5-lifecycle-2026-09-24/README.md)在独立公共消费者上完成 3 次预热加 100 次、每轮静置 1 秒的重载，exit 0；Private Bytes 从 37,027,840 增至 44,335,104 字节，末点增量低于 16 MiB 数值线。另两次 Bridge 100 轮正常退出。然而目标 1,000 轮 Bridge 两次分别在第 86、977 轮挂载阶段超时并由监督器终止，没有任何一次完成。此失败属于当前候选，不能沿用 preview.4 的 1,000 轮通过结果。当前 `resourceGate: not-evaluated`、`releaseReady: false`；受控复现和原生线程栈诊断仍待执行。
