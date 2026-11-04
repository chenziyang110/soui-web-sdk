# 当前 0.6.0 原生内核组件工程 crosswalk

本档案推进 [Issue #39](https://github.com/chenziyang110/soui-web-sdk/issues/39) 的工程材料整理：把当前两架构精确 MAP 成员、12 个本地构建上下文（11 个复用静态库及 Main/direct）、Source2 源文件、版本声明和已有许可文本分别绑定。它不完成完整二进制许可审核，也不构成法律意见、再分发许可或发布批准。

`Crosswalk.json` 记录明确组件、嵌套组件、声明版本及原始文本 pins、缺口；`MemberSourceCandidates.json` 保留所有 12 个上下文的成员到源文件候选映射及真实歧义/无候选项。`SUMMARY.json` 是便于审查的数量摘要，`Collection.actual.json` 记录实际读取、散列和收集边界。`Files.index.json` 是排除自身的闭合文件集合。

收集基于已冻结的 [当前 link members](../current-link-members-0.6.0-2026-10-29/README.md)，复用其精确 x64/x86 DLL 与 MAP 结论，不再次编译、加载 DLL、执行产品或测试。Source2 完整清单的身份为 `26c5b73e4915bb4539e87b987cd8e952005f0fe41d987138d8d8e3b13eb25c95`；本次对项目文件、引用的版本/许可材料以及候选源文件重新做当前物理字节与清单的比较。原 11 个静态库的复用、历史外部 provider 全量来源尚未证实等限制继续保留。

候选匹配只采用同一 MAP 库上下文中的对象 basename 与项目 `ClCompile Include` / `MASM Include` 默认对象名。原始项目没有 `ObjectFileName` 覆盖。它没有求值 MSBuild 条件、预处理分支、PCH/头文件代码、生成资源、相同代码折叠或对象改名，因此不能把一个候选或目录分类当作完整编译来源/许可结论。父子组件计数会重叠，不可相加；项目中出现但未命中的 sfntly 等源项不据此认定进入了 DLL。CRT/Windows SDK 与 unattributed 成员单独保留在 `outsideContexts`；Main 的 absolute/common/linker-defined/resource 原始伪成员也原样保留为无源候选，不能当作丢失的 C++ 文件。

需要特别保留的版本边界：zlib 当前头文件声明 1.2.11、README 声明 1.2.5、LICENSE 开头仍写 1.2.4，完整上游身份保持 unknown；OTS 的恢复许可文件名包含 5.0.0，不能作为源码版本；Skia 本地 README 明写 `Version: unknown`。两个 V8 静态项目共用头文件所声明的 5.7.246.0 源族。ICU 的 `_unicode` 头文件声明 54.1，但 Main 编译本地适配 `uchar.cpp`，不能据此宣称完整 ICU 54.1。其他版本也只表示具体源文件的本地声明，未证明是未修改的完整上游发行版。

原 `kernel-source/index.json.txt` 的 Source24 身份和旧 0.5.0 `license-review.json` 均未改写。已有 49 份材料逐项与当前 Source2 原文件及保留副本比较；相同字节可复用材料身份，不产生新的适用条款批准。OTS/IJG/curl 的恢复文本与当前 API 声明分开 pin，不伪造已经缺失的 accompanying LICENSE/README/COPYING。历史六个 0.5.0 包的 198 份许可字节一致结果继续仅属于当时包，未挂到当前 0.6.0 包。

首方 SDK 的 MIT 决策已确定并 pin `LICENSE.md`，不再列为待决定。仍待完成完整的逐对象/嵌套/文件头及实际交付物条款映射、来源冲突补证、责任人及适用条款人审、再分发批准；详见 `Crosswalk.json/gaps`。`responsibleReviewer=null`、所有许可人审/批准字段、`releaseReady`、`productionQualificationComplete`、`fullDevtoolsGoalComplete` 均保持未完成。

本目录当前收集版本为 revision 3。自审发现第一次收集的块正则会漏掉部分自闭合 `ClCompile` 项，第一次的 7 个原件保留在 `initial-incomplete-collection/`，并明确标为 `incomplete-parser-superseded`。revision 2 修复为逐 opening tag 解析、增加每项目声明总数一致断言，但只覆盖 ClCompile；其 7 个原件保留在 `clcompile-only-collection/`。revision 3 进一步绑定项目明确声明的 17 个实际 x86 OpenSSL MASM 成员，分别校验两类声明数。旧候选数不作为最终结果或许可通过证据，构建及测试证据未改变。

实际收集命令（从仓库根目录执行）：

```powershell
node verification/production-validation/current-license-crosswalk-0.6.0-2026-10-29/collect.cjs
```

收集器仅新建本目录的 JSON，拒绝覆盖既有结果。归档完成后该命令再次运行会拒绝；后续变化应新建有独立身份的档案，不回写本次材料。本次没有构建、测试、长测、VM 操作或法律批准。
