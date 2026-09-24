# preview.5 Win7 SP1 x86 混合场景准备记录

状态：**已准备，尚未在来宾执行**。本目录只保存冻结输入、客体运行脚本和宿主校验器；目前没有可计入 Win7 混合场景通过的结果。宿主 x86 8 小时诊断仍在运行，QEMU 客体将串行执行，以免相互争用桌面与 CPU 影响时延。

输入来自已完成的 [x86 60 秒公共 SDK 消费者预检](../preview5-mixed-soak-preflight-2026-09-24/x86-60s/result.json)：源码指纹 `b8355092795ee5bb08f36dc4c677c6259f5eb857032fc5cd8d3fdd9859a6d392`，消费者 build-id `072e9f25-8a9a-4c97-879f-a081d8ddd815`，运行载荷指纹 `6c34e4909ba7059a89536d19777d55ba1dcdfd30def0cc9e6d35f6140d8824e9`。已从结束的 suite 复制并逐项比对 76 个运行文件，按验收器规则排除可变的 `resources/source-navigation.log` 后，复制载荷指纹与原记录相同。独立 ZIP 的 SHA-256、大小、EXE/内核/SDK DLL 散列及来宾参数列于 [input.json](input.json)；ZIP 完整性已在宿主通过 `7za t` 校验。

计划来宾环境为用户已授权的 Windows 7 Enterprise SP1 x86 QEMU 快照：TCG、Core 2 Duo、2 vCPU、2 GiB、无网卡；使用只读传输盘，解压并运行于来宾 NTFS。客体执行 60 秒同进程混合生命周期，10 秒空闲、20 ms 静置、单步 60 秒限时。回传后由 [串口校验器](Verify-SerialExport.ps1)逐文件核对长度、SHA-256、系统版本、ZIP/二进制身份、操作退出码，再由 [事件校验器](Verify-Events.cjs)检查完整步骤、时长和资源样本。操作通过也只说明这一小范围的 QEMU 预检，不代表 8 小时业务长稳或生产准入；`releaseReady=false`。
