# ADR-0065：独立 MessagePort 与 Dedicated Worker 有限行为 fixture

- 状态：proposed
- 日期：2026-10-11
- 决策者：维护者 / 评审人
- 关联：[Issue #77](https://github.com/chenziyang110/soui-web-sdk/issues/77)

## 背景

普通软件资源消费者与私有通信观察消费者是不同的负载。此前通信 fixture 的真实动作注册因字符串枚举未指定 maxLength 失败；修正后的私有 native/Web/Worker 在 x64/x86 baseline/candidate 四臂完成有限行为验证。这个动作不存在于普通 acceptance-app，不能把两个字段加到普通 main 冒充通信 fixture，也不能用通信 Web 的资源结果替代普通 100+10 负载。

## 决定

新增 tests/message-port-app 的独立 native/CMake/Web/Worker 源。继续使用公开 installed Soui::Sdk、原 SouiAcceptance WIN32 目标和三份资源部署。独立进程保留原动作 namespace、typed contract、真实 session 授权及 owner 关闭次序。SDK validator 和权限规则不改。

tests/acceptance/runner.cjs 保留默认 ordinary，增加只允许 ordinary/message-port 的固定源码选择及 prepare-message-port 命令。tools/Test-Acceptance.ps1 的 Fixture 默认 ordinary；通信分支只接受 PrepareOnly，并在准备副作用之前拒绝其它使用。真实 SDK/Web/typecheck/Vite/install/consumer/stage 流程保持，tests 根自然进入源身份。

补充 reader/controller 放在 tests/acceptance。专用 tests/acceptance/message-port-stage-candidate.cjs 向共享 runner.prepare 明确传入第五参数 message-port，不改变泛用普通 stage 的默认选择。两臂每架构复用同一实际 EXE/Web/CRT/安装 SDK，只允许 node.dll 的真实差异。执行输入和 Root guard 仍须从新真实准备产物冻结，不能移植旧 UUID、BuildID、路径或 hash。Root 必须提前创建公共 runs 父目录并保持 DesktopValidation 互斥、环境恢复和 strict Owned 自然闭合。

## 备选方案

把私有观察点加入普通 acceptance-app 会改变资源负载，因此不采用。复制 SDK 算法或放宽 contract validator、授权、Owned 读取器也不采用。通信 fixture 不注册为正常根 native 测试叶；它需要真实安装包、公开 Web 产物、运行时内核和桌面环境。

## 影响

不改变 SDK 公共 API/ABI、auth、业务或运行时算法。源码交付新增测试文件和显式准备选择。controller/reader 通过新实际 inputs 加载同 deliveryRoot 的共享 runner 和 reader，正式源归档须正常更新 tracked source-manifest。

## 迁移与回退

共享默认 ordinary 保持；通信测试仅显式选择。新目录应重新正常 PrepareOnly、创建 candidate suite 并执行四臂。历史注册失败和私有四臂结果保留。回退可去掉专用 fixture 文件和 selector spans，不重写普通或历史证据。

## 验证

真实 runner 的文件系统测试检查默认/显式 ordinary、显式 message-port 全部 Worker 输入、未知选择的早拒绝、缺目录不回落和链接目录拒绝。这些 build/Web 数据明确是 unit fixture，不是生产构建证据。源码测试草案尚未执行。

新持久入口仍需每架构实际准备/链接/资源部署及四臂 strict Owned 验证：3 reload cycles、0 warmup、settle500、timeout20000，原9步骤/7样本以及每臂4文档/32行为案例/40 sidecar 行，原 processTimeout214500、Owned394500、drain2000。历史私有四臂已完成的有限范围不能继承为新路径通过。Worker native join、debugger wait/kill、polyfill scheduler、完整内核工具链、资源增长、目标系统和发布资格仍未验证。
