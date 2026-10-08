# ADR-0024：单一受限动作源与标准 SDK 生成工具

状态：接受，首批切片；不代表完整 0.5 或生产验收。

## 背景

模板目前分别手写 C++ 动作描述、参数解析和 TypeScript 类型。旧 `tools/contractgen` 服务 Demo 的另一种契约，不能生成可通过公共 SDK 注册的类型绑定。原生 `soui.action/1` 已有受限 schema、最终输入/输出检查和会话白名单；生成器不能另造权限或任务框架。

## 决定

- 标准入口为 `node tools/sdk/actiongen.cjs --source <contracts/actions.json> --root <项目根>`，附加 `--check` 只读检查，过期、缺失或修改的输出返回非零。生成发生在构建身份冻结之前；构建期间只检查，禁止自动改写身份。
- 唯一输入格式为 `soui.action-source/1`：`cppNamespace` 与有界 `actions` 数组；每项只有 `method`、`capability`、`cppName` 和完整 `action`（原样沿用 `soui.action/1`）。命名空间、类型和递归输入/输出属性拒绝未知字段、重复方法/类型名、C++ 关键字、保留标识符及保守列举的 SDK/标准/Windows 头宏（例如 NULL、EOF、INT64_MAX、errno、interface、near/far、min/max、GetMessage），不静默重命名或 undef 宏。保留名单在生成器 `macroNames`，不能预测调用方全部头版本或自定义宏；应用仍须避免与生成标识符冲突。成功输出根禁止 `error`，避免已有 Bridge 业务失败标记歧义。
- 浏览器和无构建依赖的 Node CLI 共享无副作用 ESM schema 核心，保留现有公共 TS 类型和 `SouiBridgeError` 错误码。原生最终检查及公开 `register_action` 不变。保持 ES2015/Chrome 49 构建目标、Node 20.19 和 C++17 基线。
- 生成 C++ 输入/输出结构、JSON 编解码及逐动作 `register_<cppName>`，回调接收类型化输入和经过原生授权的 `bridge_request`，返回类型化输出。可选头文件目标 `Soui::ActionJson` 导出随 SDK 已固定的 JSON 头文件；基础 `Soui::Sdk` 不公开增加 JSON 依赖。
- 生成 TS MethodMap、描述与调用包装器；调用前检查输入，调用成功后检查输出。包装器同步进入原生客户端，保持既有用户手势时序；不自动重试、取消或授权。可选字段映射 `std::optional`，显式 null 与缺失分别表示。字符串 enum 首批映射 `std::string`，仍由 schema 校验；安全整数映射 `int64_t`，number 映射 double。
- 静态 Agent 工具定义仅是说明。运行时工具发现必须从原生已授权 catalog 生成，再限制到本项目动作；静态文件不能作为授权证明。保持既有公共任务服务，生成器只处理同步动作。
- 固定输出在 `app/generated/actions.hpp`、`web/src/generated/actions.ts`、`contracts/generated/{tools.json,actiongen-manifest.json}`、`docs/generated/actions.md`，都属于应用而非 SDK 锁管理范围。元数据记录规范化输入 SHA-256、格式与生成器版本及输出 SHA-256，不含时间、绝对路径或当前目录。整个 write set（含 manifest）先拒绝符号链接、硬链接、越界和未标记的已有文件。check 同时核对原始 Buffer 字节与 manifest 散列，不以 UTF-8 替换解码相等作为通过依据。
- 生成采用同目录独立、独占创建的临时文件，写入并 flush 后以 rename 逐文件替换，manifest 最后发布；不原地截断受管输出。发布前重查全部旧内容与链接，逐项核对临时文件身份，避免写穿发布时新增的文件硬链接。整组不是原子事务，中断可留下临时文件或散列可检测的部分发布。此本地 CLI 要求生成期间项目目录结构不被并发替换；不声称抵御同权限恶意进程在检查与系统调用之间替换父目录，亦不提供通用安装恢复框架。
- 两套模板提交已生成文件，正常新建即可接入；编辑源后显式生成，再执行只读检查。问候由生成绑定真实注册和调用，计数任务保留原公共服务。

## 备选方案与兼容性

不扩展 Demo 旧格式，不依赖 SDK `dist` 作为生成器启动前提，不重复三套 schema 校验，不新增 JSON 解析器或公共 Bridge ABI。已有手写动作和原模板任务继续有效。生成器接受的符号范围比运行时字段名更窄，这是类型代码生成约束，不能反过来收紧原生契约。

## 验证

Node 测试验证不合法输入、确定性、搬移、只读检查与输出保护。TS 编译验证 MethodMap 输入/输出类型，运行测试验证非法输入不发请求、坏输出拒绝和已授权工具发现。独立安装 CMake 消费工程仅通过 `find_package(SouiSdk)`、`Soui::Sdk` 与 `Soui::ActionJson` 编译生成头，真实执行 bootstrap、动作注册、合法输入、绕过 TS 的非法输入、白名单、错误输出及处理器异常。不启动 GUI；目标系统、两架构和生产矩阵仍由整合验证完成。
