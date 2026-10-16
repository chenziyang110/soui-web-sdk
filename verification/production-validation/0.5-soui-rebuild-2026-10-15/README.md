# SOUI 两架构重建与独立消费者

日期：2026-10-15。版本：0.5.0-preview.1。范围限组件构建和独立原生消费者，
不代表完整 SDK 应用、目标系统、工具链资格或正式发布已经通过。

公共 `Build-SouiLlvm.ps1` 在 x86、x64 各重建 utilities、soui、render-gdi、
imgdecoder-gdip、soui-sys-resource。两次实际源码身份相同：
`c633158960d39a7cc5e94b4ef959df0cfe59aef7e660aeaf4b4f131fcf1ae4da`。
固定 LLVM 22.1.3、MSVC/ATL 14.29.30133、SDK 19041；不覆盖原源树。
原始捕获均证明根进程/监督器 0/0、自有进程组自然归零，未使用强制清理。

- `x86/`、`x64/` 的 `build/` 保存实际参数、编译记录、源码/工具输入与 MAP；
  `capture/` 保存原始进程记录和实际 ready、finished。
- `contract/` 保存实际编译、链接与运行。消费者直接导入两个原有单例
  数据符号，并检查四种尺寸文本和局部工厂注册后的创建、释放、重复拒绝。
- `exports/` 对比旧、新 DLL。原有 4,046 个 SOUI 导出全部保留，新增 12 个
  模板实现导出；Utilities 的 445 个导出及组件入口保持不缺失。
- `missing-exports-regression/` 保留修复前实际链接失败：两个原有数据
  符号缺失。不能用成功编译库本身替代消费者 ABI 验证。
- `msvc-build-negative-closure/` 的根进程虽然返回 0，编译服务未在期限内
  退出，监督结果仍失败；`msvc-products-consumer/` 只证明其产物通过行为
  回归，不将编译进程的失败状态改成通过。

原始记录中的绝对路径是当时执行事实。`index.json` 记录所归档原件的物理
散列，源码补丁及重放身份见 `third_party/manifests/soui-llvm-source-050.json`。
导入五个 DLL 和两个既有导入库的两架构字节身份见
`third_party/manifests/soui-build-050.json`；旧文件已在本地保留。

完整候选需继续完成 SDK 四配置、当前内核组合、桌面关闭、长期资源、
目标系统及授权复核；`releaseReady` 保持 false。
