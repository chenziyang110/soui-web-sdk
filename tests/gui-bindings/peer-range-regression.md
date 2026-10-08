# GUI peer 范围回归记录

2026-10-08，在未修范围 `>=0.4.0-0 <0.6.0` 上运行：

```powershell
node --test --test-name-pattern="npm peers accept" tests/gui-bindings/packages.test.mjs
```

真实结果：1 项失败，exit 1。npm 对临时 `@soui/native-sdk@0.5.0-preview.1` tarball 返回 `ERESOLVE unable to resolve dependency tree`：已找到 SDK `0.5.0-preview.1`，但 `@soui/gui-react@0.1.0` 的 peer `>=0.4.0-0 <0.6.0` 不接受。独立审查也分别确认两包同样冲突。

夹具仅复制当前 SDK 编译字节并在临时目录更改 package 元数据版本，不改交付根身份，不验证真实 0.5 SDK 行为。安装使用离线 npm 正常 peer 解析，不使用 force、legacy-peer-deps 或源码 alias。回归还检查支持的稳定版本接受及未承诺版本拒绝。

修后明确采用 `>=0.4.0-0 <0.5.0 || >=0.5.0-0 <0.6.0`。同一实际 npm 回归通过：0.5.0-preview.1 与 0.5.0 接受；0.4.1-preview.1、0.5.1-preview.1、0.6.0 均返回 ERESOLVE。最终完整模块测试 27/27，React18 11/11，两个 consumer 类型检查通过；相关 npm 安装/锁更新 audit 为 0。此记录只对应包声明修复，不扩大 SDK 或平台认证。
