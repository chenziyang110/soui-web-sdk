# preview.5 Windows 7 SP1 x86 QEMU 任务容量原生测试

日期：2026-09-24。目标系统是独立安装的 Windows 7 Enterprise SP1，来宾报告 `6.1.7601`、`32-bit`、`ServicePackMajorVersion=1`。QEMU 11.1.0 使用 `pc`/TCG、Core 2 Duo、2 vCPU、2 GiB、标准 VGA、无网卡；系统盘以快照模式启动。测试文件通过只读 vvfat D: 提供，结果写在来宾 C: NTFS，再经 COM1 导出。虚拟磁盘与安装镜像不进入仓库。

测试 EXE 来自 preview.5 [x86 Release 构建](../preview5-task-capacity-2026-09-24/x86-sdk-build.json)：源码指纹 `16d1dccc47ebacff83bf0a94abeed21c75cd7bcd049559802598a5e44656f237`，build-id `426d9be7-c49a-43df-a4c1-7532087a23e2`，EXE SHA-256 `21e61266b21ff51ac506e288f5bba8b711e57c915982ab23735f27d503cae07e`。来宾 `certutil` 复算的 EXE 散列与宿主一致。运行时支持 DLL 从已验证的 preview.5 x86 公共消费者载荷取出；宿主逐文件比对了 59 个 DLL 的 SHA-256。详细环境见 [environment.json](environment.json)。

独立原生测试输出 `Native task store capacity: boundary, cancellation, parallel admission, and scanner rejection passed`，退出码 `0`、标准错误为空。它覆盖 1,024 个活跃任务硬边界、满载时同作用域幂等与冲突、拒绝不消耗 ID、取消确认后的容量回收、八线程并发准入，以及目录扫描向调用者传播 `E_BUSY`；具体断言见 [测试源码](../../../tests/native/task_store_capacity_tests.cpp)。[原始串口流](serial-export.txt)含 5 个文件及长度和 SHA-256；[校验器](Verify-SerialExport.cjs)复核帧完整性、来宾版本、EXE 散列、退出码与输出，并还原[原始结果](results)。从仓库根目录执行：`node verification/production-validation/win7-sp1-x86-qemu-preview5-task-capacity-2026-09-24/Verify-SerialExport.cjs`。来宾[运行脚本](harness/run-native-capacity.cmd)与[串口导出脚本](harness/export-serial.ps1)保留，[完成画面](completed-screen.png)仅作辅助。

首次临时载荷只带 CRT DLL，进入测试代码前因缺少 `api-ms-win-core-localization-l1-2-0.dll` 出现加载错误，退出非零；[首次串口](first-attempt-serial.txt)和[错误画面](first-attempt-missing-dll.png)保留，不计 SDK 测试失败或通过。补齐全部 59 个运行 DLL 后重新启动 Win7 来宾，才得到上面的有效结果。中间一次 QEMU 启动因 VNC 端口占用未进入来宾，同样不计测试结果。

这关闭 preview.5 **原生任务表容量边界在 Win7 SP1 x86 QEMU 的单次测试**，不等于完整公共任务业务链、并发关窗、长时间运行或生产兼容性通过。此前的[公共消费者四模式短测](../win7-sp1-x86-qemu-preview5-consumer-2026-09-24/README.md)和[三应用短测](../win7-sp1-x86-qemu-preview5-apps-2026-09-24/README.md)仍是单独证据。物理设备、8 小时混合负载、资源所有权、中文 IME/DPI、TLS/代理、签名安装升级和 Electron 对照仍缺证据；`releaseReady=false`。
