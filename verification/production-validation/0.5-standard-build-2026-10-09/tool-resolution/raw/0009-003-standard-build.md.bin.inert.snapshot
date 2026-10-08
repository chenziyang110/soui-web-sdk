# 标准受监督构建

先准备所选架构的当前工具，再通过标准调用者执行构建：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/internal/Build-ValidationTools.ps1 -Architecture x64
node tools/internal/standard-build.cjs --architecture x64 --configuration Release --with-webview2 --install
```

调用者只接受自己的工作区 `tools/Build.ps1`，启动前固定原始源码、工具、完整命令和旧 build 报告；结束后检查新 buildId、当前构建报告、实际库或应用载荷及选中的编译器/SDK。同工作区所有架构的 SDK/application 构建共用独占租约，避免同时改写共享前端输出与依赖缓存；发现已有租约时需先核查原持有者和记录，工具不会按 PID 猜测并删除它。

已配置工具可以使用 NVM junction 等启动路径；记录同时保存原路径、解析后的实际路径和文件散列。构建前后重新核对解析目标和 bytes，任何目标变化都拒绝。源码、构建证据、所选 prepared runner 与 tail pin 的路径仍拒绝链接；工具不会修改全局 NVM 设置。

默认要求整个 Job 自然闭合。`--sdk-only`、`--skip-web`、`--with-webview2`、`--with-gui-bindings`、`--install`、`--fresh-configure` 分别映射既有 Build 开关。预算使用十进制毫秒 `--timeout-ms`（默认 900000）和 `--drain-ms`（默认 30000），不改变失败的意义。x86 使用 `--architecture x86`，Debug 使用 `--configuration Debug`。所选架构准备器缺失、旧源或工具字节不符均拒绝，不改用另一架构的旧输出。

只有维护者明确选择已经审过的 MSVC 14.44.35207 Hostx64 目标对象时，才可增加 `--compiler-tail --tail-image <规范绝对文件路径> --tail-sha256 <已审精确SHA256>`。两个目标架构的 SHA 在既有政策中分别固定；工具启动前生成新的 nonce，原生 supervisor 从持有文件句柄再次核验对象。该入口拒绝 `--with-gui-bindings` 与 compiler-tail 的组合，因为当前政策尚未审过这个参数。不能用于任意脚本、应用、测试、超时、STOP 或测量组。

结果写入新的 `out/standard-build/attempt-<UUID>/`：启动前 inputs、旧报告原件与索引、工具探针、实际 context/native stream、stdout/stderr、本轮 build 报告、输出日志与配置原件映射、独立 result 和完整文件散列索引。原始 generic false/124 不改写；窄 compiler-tail 接受可令调用者退出 0，但 compilationCompleted、genericSupervisionPassed、closureConfirmed、compilerTailAcceptance 和 accepted 分开记录。所有结果保持 `releaseReady=false`。

当前版本只支持既有 preview 身份格式。调用者成功不关闭目标 Windows/PE、GUI、长期稳定性、签名、许可、RC/stable 或发布门槛。SHA 与 nonce 只核对这组文件的一致性，不认证可以重写整组文件的同一用户，也不认证进程执行内存或 DLL。

标准 `Test.ps1` 以所选架构执行 Tail reader/client/policy/caller 与原生 fixture；`Collect-Verification.cjs` 执行一次协议组和两个架构的原生组。原生测试缺工具必须失败，不能形成 mandatory skip。fixture 的政策通过只证明测试链路，不能冒认实际 MSVC 编译尾已经接受；测量组仍必须自然完成。
