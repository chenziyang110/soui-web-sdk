# SOUI C++ 兼容修复

`0001-llvm-conformance.patch` 基于本仓库提交
`505e3db2db83daa38ddcd66b92a0d10c00d0594e` 中的 SOUI 2.9.0.3 派生源码。
这是本地修复，不声明已经上游合入；原 SOUI 授权及文件版权保留。

补丁包含静态成员特化声明、COM 直接初始化、工厂局部对象、变参字符串
指针、命中坐标副本及 GDI 图像尺寸边界。两份交付头文件同步修改。
具体兼容性决定见 [ADR 0096](../../../docs/adr/0096-soui-llvm-dependency-build.md)。

将本补丁复制到上述基准提交的独立 checkout 对应路径后，在其根目录执行：

```powershell
git apply --check third_party/patches/soui/0001-llvm-conformance.patch
git apply third_party/patches/soui/0001-llvm-conformance.patch
```

基准文件与修改后文件分别保存物理散列和明确采用 LF 规范化的重放散列，
补丁保存物理散列，见 `third_party/manifests/soui-llvm-source-050.json`。
Git 应用补丁可能保留基准 CRLF；重放比较使用所声明的 LF 身份。实际组件构建仍记录完整
源树的物理文件和目录快照，不能用这个局部补丁记录代替构建或运行验收。
