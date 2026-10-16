# Issue #76 / #39 专项记录

2026-10-16，基于 PR #98 合并后的 `40f04eb1f63158c6489fd23509d66fb47f2f38c6`。
本目录记录实际执行和对既有原件的复核；`SUMMARY.json` 汇总结论，
`INDEX.json` 绑定原路径及归档字节。`raw/` 保留原字节，较大文件使用 gzip。

## 便携构建与配置边界（#76）

- 从公开源码选择规则复制 1,476 个文件到全新仓库外目录。私有 Miniblink 根完全不存在，选中源码散列与原工作树一致；没有复制旧 out/、Web dist/ 或 SDK 编译输出，固定第三方预编译依赖按源码交付规则保留。
- 通过实际 `tools/Build.ps1` 执行 x64/x86 Release，使用本机显式 LLVM22MSVC142 档位、Windows SDK 10.0.19041.0，并启用可选 WebView2。每架构四个 Web 包及默认 ALL 构建完成；没有传入 vendor 开关或 SdkOnly。每个架构构建目录初始不存在，整个自有进程组自然退出 0，源码前后不变。
- 两架构均实际读取 CMake cache 和 CTest 注册：SDK tests ON，vendor 默认 OFF；135 项 SDK 原生用例仍注册，vendor 为 0。这里没有执行这 135 项用例，也不将 OFF 解释为内核通过。UIA 保持默认 OFF，所以不以数量比较完整 Source30 的可选 UIA 范围。
- `tests/sdk/vendor-tests.test.cjs` 共 21 项通过、0 失败、0 跳过。其中新增 7 项调用真实 CMake，覆盖缺根默认 OFF、完整形状默认 ON、四种明确拒绝和缓存 ON 后缺失输入。策略夹具用 `project(... NONE)` 和合成普通文件；它证明配置预检，不能替代编译或源码字节门禁。
- 已有 Source30 两架构完整源码通过 `--with-vendor-tests` 构建、`-RequireVendorTests` 测试。逐名读取已归档的 `070-unit-stdout.bin` / `072-unit-stdout.bin`，各自与 `vendor-test-names.json` 的全部 54 项准确对应，均 Passed；所属 208 项完整运行退出 0，自有进程组自然闭合。此次复核 180 份相关输入；私有源码按原字节一致，其他文本按既有 LF 归一化规则一致，原始散列亦保留。

这些结果补齐 #76 指定的构建与配置边界；原模板配置失败保留。没有重跑底层依赖、生产矩阵、桌面或长测，也没有修改固定 SDK 内容。

## 发布材料机械核对（#39）

逐个打开六个应用 ZIP，先核对其字节数和 SHA-256 与已发布 0.5.0 的 `RELEASE-FACTS.json` 一致，再读取其中 `resources/legal/` 的材料。每包 198 份源码声明均逐字节一致，包括完整 OpenSSL/SSLeay 文本；兼容包的实际 node.dll 散列也保存在记录中。

这项检查只确认材料存在且内容一致。没有认定精确 DLL 的完整链接成员，也没有作出再分发条款或首方许可决定；`completeLinkedComponentInventory=false`、`redistributionTermsApproved=false`、`licenseReviewComplete=false`、`releaseReady=false`。#39 继续开放，旧 preview.15 的缺件事实不改写。

## 复核方式与范围

从仓库根目录执行：

```powershell
node verification/production-validation/issue-maintenance-2026-10-16/verify.cjs
node --test tests/sdk/vendor-tests.test.cjs
```

第一条只复核归档散列、已记录结论和原有 54 项日志，不重新构建或读取外部 ZIP。原始执行/审计脚本保留在 `raw/`，其中路径属于当时真实环境；重新执行需要新的目录与输入身份。构建产物、外部 ZIP 和全源工作树继续保留在外部，归档不是完整独立构建环境。

#77 的实际准备选择另经现有文件系统回归复查，13 项通过；双架构浏览器四臂仍未执行。#27/#29 的真实 Shell/驻留、#42 的 Win7 交互、#31 的长时续期、#96/#23 的完整生产条件继续开放。此记录不改变根验收报告的未验收结论。
