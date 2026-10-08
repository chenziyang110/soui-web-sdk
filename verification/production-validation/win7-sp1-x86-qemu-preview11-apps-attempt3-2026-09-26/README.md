# preview.11 Win7 SP1 x86 第三轮：Workbench 关闭超时

**三入口验收失败。** 使用已冻结的 v3 输入目录布局，确认 Win7 桌面与实际 D: 盘后仅发送一次命令。Starter 达到就绪并退出 0；Workbench 默认入口达到就绪后停在 `host-close-start`，触发原 60 秒期限。React 未执行，运行后载荷检查未执行，不能宣布当前 Win7 应用整体通过。

`raw/attempt3/serial-export.txt` 保留完整终态串口（95,773 字节、29 帧）。冻结校验器原始退出 1，首先拒绝缺失的 `run-complete.txt`，没有为失败放宽门槛。`partial-review.json` 独立核对全部帧散列、Win7 6.1.7601 / SP1 / 32-bit / PointerBytes 4 / PowerShell 2 身份、10 个传输文件和 546 个运行前文件（包括 10 个 legal 文件）。冻结输入 SHA 为 `10c9a4e90cf5e25a2c98d0f6107eb7881a5ab39caf768973d761a1045566826d`，候选编译输入仍为 `a0c1d7900d39555508288d5459e65d3852e543cf7ec3879126bd6af59d2c2550`。

Workbench 原始日志包含 document/bridge ready、应用就绪、关闭开始；没有 `adapter-unmount-start` 或关闭完成。客体驱动在期限后终止该进程并确认无残留，其 `-1` 是**驱动的超时标记，不是取得的原生退出码**。现有日志只能把范围缩小到进入引擎侧卸载记录之前，不能直接断言具体锁或线程；下一轮须采集关闭各阶段或线程栈，保持原期限。

QEMU PID 7932 正常关机退出 0，未触发外层 12 分钟超时；基础磁盘前后 SHA `fdf5eb826c9868622e218d1d54f328970ad190f848cd0777174885b1d072ddb4` 一致，冻结输入复验退出 0。串口时间采用来宾自身时钟，不能与宿主 UTC 直接合并作为性能计时。屏幕、单次命令发送、监督和原始输出均归档。PPM 到 PNG 仅无损编码转换，未改像素。

此结果证明 v3 传输布局通过本轮严格检查，保留 Starter 的有限功能证据；不覆盖运行后文件身份、完整关闭稳定性、真实输入、长稳、物理设备、安全授权或生产准入。`allApplicationsPassed=false`、`sourceReproductionClaim=false`、`releaseReady=false`。
