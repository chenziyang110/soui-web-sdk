# 标准动作生成工具

在项目根目录运行：

```powershell
node tools/sdk/actiongen.cjs --source contracts/actions.json --root .
node tools/sdk/actiongen.cjs --source contracts/actions.json --root . --check
```

首次使用不需要构建 Web SDK、安装 npm 或引用 Demo。Node 最低版本仍为 20.19。生成必须先于构建身份记录；构建入口只能执行 `--check`，缺失或过期返回非零且不写文件。

输入只接受 `schema: "soui.action-source/1"`、单个 `cppNamespace` 和 1–64 个 `actions`。每项只有 `method`、`capability`、`cppName`、`action`；`action` 使用现有完整 `soui.action/1` 描述。输入文件不超过 512 KiB，schema/描述、Unicode、安全数字和深度沿用 SDK 校验。方法和能力使用 1–128 个 ASCII 字母、数字或 `_.:-`，方法不能占用 `bridge.`、`task.`、`desktop.` 公共空间。命名空间、类型名及递归属性使用以字母开头的 1–64 个 C++ 标识符字符，拒绝关键字、双下划线、constructor/prototype 属性、生成符号冲突及保守列举的 SDK/标准/Windows 宏，例如 NULL、EOF、INT64_MAX、errno、interface、near/far、min/max、GetMessage；明确报错，不改 JSON 字段名或 undef 宏。宏保留名单见 `tools/sdk/actiongen/generate.cjs` 的 `macroNames`，不涵盖调用方所有头文件版本或应用自定义宏；应用仍须避免与生成名冲突。现有手写动作仍可使用更宽的原生字段范围。

输出固定为 `app/generated/actions.hpp`、`web/src/generated/actions.ts`、`contracts/generated/tools.json`、`contracts/generated/actiongen-manifest.json` 和 `docs/generated/actions.md`。所有输出归应用团队；规范化源 SHA-256 与输出散列记录在 manifest 中，不含时间或开发机路径。不能直接编辑输出。生成器先验证全部目标（含 manifest），拒绝符号链接、目录联接、硬链接及未被 manifest 管理的已有文件。`--check` 比较原始字节与 manifest 散列，坏 UTF-8 字节不能靠替换解码蒙混通过。

生成先独占创建同目录临时文件，写入并 flush，然后逐文件 rename 替换，最后发布 manifest；不原地写穿已有文件的硬链接。发布前重查旧内容和临时文件身份。整组不是原子事务；中断可能留下本工具临时文件或部分发布，check 会按散列拒绝。保留现场，恢复对应已生成文件，或把所有自有输出与 manifest 移到另一个目录后重新生成，确认归属后再清理临时文件。生成期间不能并发编辑受管输出或替换项目目录结构；此 CLI 不提供抵御同权限恶意进程替换父目录的安全边界。`--check` 永不修复。

C++ 应用链接 `Soui::Sdk` 和可选 `Soui::ActionJson`，包含生成头，再逐项调用 `register_<cppName>`。回调接收类型化输入和原生已经解析授权的 `bridge_request`，返回类型化成功输出。生成器不改 `set_session_capabilities`：应用必须按自身授权策略配置原生白名单。注册逐项返回 bool，必须检查，不保证多项注册原子回滚。成功输出根的 `error` 字段被禁止；处理器异常沿用 `E_NATIVE_FAILURE`，错误输出沿用原生 `E_SCHEMA_INVALID`。

TS 的 `ActionMethods` 可与公共服务 MethodMap 交叉。`callAction` 同步校验并进入原生客户端，失败输入会同步抛出 `SouiBridgeError`；成功响应继续检查输出。它不重试、授予权限或创建任务。整数是安全范围内的 `int64_t`/number，number 是 double，数组为 vector/Array，object 为结构体，缺失可选字段与显式 null 分别使用 optional/null。字符串 enum 在 C++ 中是 string（原生最终检查），在 TS 中是字符串联合。

静态工具文件标记 advisory，只能用来了解接口。`authorizedActionTools` 必须接收当前原生客户端发现的已授权 catalog，且只返回本项目动作。描述和网页自报身份都不能提升原生权限。GUI 注册器和已有公共任务服务继续由应用显式接入；超时不表示取消或回滚。
