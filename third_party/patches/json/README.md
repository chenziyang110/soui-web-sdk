# nlohmann/json 3.11.3 析构修复

只移植 `json_value` 的释放实现，保留头文件其余原始内容和 MIT 授权。

- 原始版本：3.11.3。
- 原头文件 LF 规范化 SHA-256：`9bea4c8066ef4a1c206b2be5a36302f8926f7fdc6087af5d20b417d0cf103ea6`。
- 固定上游：[9b179cee1e1813020fbd14d15bcced6b4ffee109](https://github.com/nlohmann/json/commit/9b179cee1e1813020fbd14d15bcced6b4ffee109)。
- [#5762：无分配、无深度递归析构](https://github.com/nlohmann/json/pull/5762)。
- [#5767：前向迭代器支持](https://github.com/nlohmann/json/pull/5767)。
- [#5770：完整枚举处理](https://github.com/nlohmann/json/pull/5770)。
- 本地补丁：`0001-allocation-free-destruction.patch`。

使用仓库根目录的 `git apply --unidiff-zero --check` 和
`git apply --unidiff-zero` 可在原始版本重放补丁。
最终交付头文件散列由 `third_party/manifests/dependencies.json` 管理。
实现、兼容性和测试边界见 ADR-0093。
