# 内核源码字节保真前置变更

本提交将 7723 个已有 vendor 文件恢复为固定源码输入的原始换行字节，其中包括原静态库对齐的 3204 个文件。逐项原始长度/SHA256与 CRLF→LF 后的 canonical 长度/SHA256 见 [rows.json](rows.json)。比较只替换 CRLF 对，不忽略其它空白、BOM、字符或编码字节；全部 canonical 字节严格相等。

此变更在 MiniBlink vendor 树末追加 `-text -eol` 规则，避免 Git 再次转换固定供应输入；whitespace 设置只作用于 vendor 树并保留其已有行尾/空行。来源与基线有功能差异的 55 个文件，以及后续集成会移除的 519 个旧文件，在本提交均保留基线 index 原字节，不夹带功能或许可清理。

这是可复核的纯数据/源码交付前置变更，没有编译、产品运行或新增测试通过；不改变 SDK 版本、默认运行包、权限或生产资格。完整源码清单由公开 `node tools/internal/source-archive.cjs create` / `verify` 从暂存区生成并核验，源码条目以原字节保留。后续功能 PR 单独记录其源码、二进制与实际验收。

PLY 原 README 第139行的七个等号是章节标题，原字节保持；仅该文件设 `conflict-marker-size=32`，避免新字节差异被 Git 当成旧冲突标记，未来真实合并标记使用32字符。没有给前端或第一方源码添加规则。

Vendor 专属 whitespace 属性也保留已有 space-before-tab 缩进；这是历史上游原字节，不以格式化改变它。canonical 校验仍逐字节比较所有空白，仅折叠 CRLF 对。
