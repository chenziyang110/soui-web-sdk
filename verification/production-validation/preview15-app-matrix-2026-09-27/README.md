# preview.15 四配置构建与契约测试

当前候选的 SDK、Starter、Workbench 完成 x86/x64 × Debug/Release 四配置构建，四次原始退出码均为 0。717 文件构建输入 SHA 为 `b0d7cc7d52de3493084e0dec6813f92f9a90a1bbce2f75ad5a62532a6dd87d07`，构建和测试前后未变化。

每组分别保存构建 ID、71 文件 Starter 和 475 文件 Workbench 完整运行载荷，每份含 60 个 PE；全文件散列、PE 架构及归档副本复核。实际 CL/link 命令没有外部 vcpkg 路径。只修改构建子进程环境，宿主设置未改。

x64 Release 执行完整 `Test.ps1 -UnitOnly` 退出 0；另外三组执行 CTest 均退出 0，合计 48 项原生 CTest 通过，包含内核实际选择策略回归。单元/契约入口的包、安装中断、源码/交付、Web 和验收协议测试原始输出完整保留。

`raw/independent-review.json`、四份 `build.json` 和 204 文件的 `raw-manifest.json` 记录输入、退出和复核。这里只验收编译与契约；新 SOUI DLL 的应用运行、当前 GUI/Win7、完整源码搬移、真实续期、V02、生命周期与生产门槛尚未关闭。
