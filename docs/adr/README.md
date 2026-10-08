# 架构决策记录（ADR）

当公共 API、Bridge 协议、权限、线程与生命周期、兼容目标、SDK 包格式或核心依赖发生变化时，在本目录新增 ADR。

文件命名：`NNNN-short-title.md`，编号递增。状态使用 `proposed`、`accepted`、`superseded` 或 `deprecated`。已接受 ADR 不回写历史结论；后续变化新增 ADR，并在旧记录中标明替代关系。

从 `0000-template.md` 复制模板。PR 必须链接相关 ADR，并在合并前将已批准决定标为 `accepted`。
## 0.4 新增

- [ADR-0016：运行档位与可选现代渲染后端](0016-runtime-profiles-and-modern-backend.md)
