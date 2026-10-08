# preview.9 基础能力证据

本轮执行日期：2026-10-02。结论范围与命令见 [验证说明](../../../docs/acceptance/agent-foundation.md)。全部记录保持 `releaseReady=false`。

- `build-x64.json`、`build-x86.json`：实际 Release 构建、输入摘要和完整运行载荷。
- `smoke-x64.json`、`smoke-x86.json`：同次构建的三个真实应用启动场景，包含标准连接器和模板方法发现。
- `test-x64.log`、`test-x86.log`：Test.ps1完整输出。开发机路径归一化为 `<SDK_ROOT>`，换行统一为 LF；未经归一化的本地原始副本留在忽略目录 `out/<arch>/agent-foundation-test-raw.log`。
- `generated-build-x64.json`、`generated-smoke-x64.json`：仓库外新建业务项目的独立构建及真实启动。
- `sdk-manifest.json`：该生成项目安装的0.3.0-preview.9源码SDK包身份；410项文件。

本轮没有修改历史 `verification/acceptance.json`，也未运行完整生产汇总。成功的短启动与单元/契约检查不能替代长期运行、性能、目标系统、授权和发布门槛。
