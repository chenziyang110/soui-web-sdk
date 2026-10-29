# 按层定位问题

## 启动失败

先读本次 `failed.json`、`qemu.stderr.log`、`supervisor.log`，不动其他运行。常见原因是依赖路径、端口竞争、WHPX 不可用、镜像锁、错误固件或父链缺失。start 超时不代表进程已停止；先核对本次 run 的 QMP UUID。没有匹配的 UUID 不发 shutdown，不按 PID 猜对象。

父盘被另一台 VM 使用时，保持锁与父盘不动。新建可用的关机基线或等待原使用者结束。不要 `qemu-img check -r`、rebase、commit 或删除别人磁盘解决问题。

## Windows 旧控制进程丢失，状态为 unconfirmed

普通 `start` 仍拒绝 `unconfirmed`，页面断线、PID 消失和磁盘时间不能证明旧运行正常结束。`pool.py recover` 只处理协作占用，不修复运行状态。确已结束旧控制端并持有本 VM 有效 token 时，可显式使用 Windows `generic-x86` 的 `offline-recover`；它不适用于仍有活进程、QMP 可连接、已有停止/失败回执或其他 VM 引用该 overlay 的情况。

先保存当前 `lab.json`、`started.json`、`active-session.json`、`start.lock` 的原始字节和 SHA-256，并从前两份会话标记核对相同 session UUID。下列占位值必须来自这次只读检查，不使用旧报告的摘要：

```text
python "<skill>/scripts/pool.py" offline-recover --name "<现有名称>" --token "<当前token>" --reason "<旧控制端已结束及本次离线恢复依据>" --expected-session "<当前session UUID>" --expected-lab-sha256 "<lab.json SHA256>" --expected-started-sha256 "<started.json SHA256>" --expected-active-session-sha256 "<active-session.json SHA256>" --expected-start-lock-sha256 "<start.lock SHA256>"
```

操作持有登记事务与原 VM 生命周期内核锁，重新核对上述精确标记，要求旧 QEMU/监督器 PID 均不存在（PID 已复用也拒绝），父链长度/修改时间不变且无后代。旧 QMP 必须明确拒绝 TCP 连接；若仅连接超时，必须另由 Windows `GetExtendedTcpTable` 的 IPv4/IPv6 两张 OWNER_PID_LISTENER 表成功查询并确认旧端口均无监听，超时本身不算证明。接受连接、查询失败、不支持某张表或发现同端口监听都拒绝。回执记录实际方法与表摘要/计数，不把超时写成连接被拒绝；不公开其他端口/进程的网络明细。原生结构、端口字节序与有界缓冲重试依据 [GetExtendedTcpTable](https://learn.microsoft.com/en-us/windows/win32/api/iphlpapi/nf-iphlpapi-getextendedtcptable)、[IPv4 行](https://learn.microsoft.com/en-us/windows/win32/api/tcpmib/ns-tcpmib-mib_tcprow_owner_pid)和 [IPv6 行](https://learn.microsoft.com/en-us/windows/win32/api/tcpmib/ns-tcpmib-mib_tcp6row_owner_pid)。

Windows CIM 必须完成全部 PID/名称的枚举，拒绝命令行可见的该 run、磁盘或 UUID 引用，以及缺少命令行的已知 Python/QEMU 控制进程。另一台 QEMU 仅在图像与完整命令行可读、经 Windows 原生 argv 解析明确列出其他绝对本地磁盘绑定且无本 VM 引用时可并存；相对路径、外部 readconfig、无法判断的后端或缺少完整身份仍拒绝。回执仅记录该竞争 QEMU 的 PID、图像和命令行摘要，不操作它，也不把本次产品短测外推为性能隔离结果。普通权限通常不能读取部分受保护进程的路径/命令行；这些进程的不可检查数量与 PID 摘要明确记录，不用系统名称白名单推断其安全，也不声称 CIM 已排除所有写入者。

排除现有磁盘写入者的依据是对原 overlay、NVRAM 及全部父盘取得并持续持有禁止写入和删除的原生只读句柄；共享冲突即失败。第一份进程快照只是加锁前的辅助观察，不能授权归档；第二份（归档前）与第三份（归档后、发布回执前）快照和 QMP 复核必须在全部磁盘句柄持有期间完成。归档前后同时核对原生文件身份，不写磁盘内容、不创建新盘、不结束进程。枚举本身失败或缺少 PID/名称仍拒绝；未检查的进程不能被记成“已识别无写入”。

原会话标记和日志按原字节移入 `sessions/offline-recovery-<UUID>/`；`lab.json` 保持原位并另存原字节副本。新 `offline-recovered.json` 只记录本次离线检查与原件索引，明确 `priorOutcome=UNKNOWN`、`originalExitCode=null`、`cleanShutdownVerified=false`，不伪造 `stopped.json`/`failed.json`，不升级旧测试结果。新状态 `offline-recovered` 仅在结构化回执与完整归档仍匹配时允许后续独立 `pool.py start`，恢复命令不自动启动。原 `lifecycle.lock` 保留。

归档中断会尽力把已移动文件放回，但保留 `offline-recovery.pending.json`，使启动继续失败；回执或原件缺失/变化同样拒绝。遇到此状态保存现场检查，不删除 pending/锁，不重复恢复、不换目录新建 VM。恢复仅建立下一次启动的离线维护边界，不证明客体文件系统干净或产品功能通过。

## 页面有画面但不能操作

依次核对：

1. 网页状态是已连接、VM 的 QMP 状态正常。若已断开，用页面“重新连接”；页面只保留一份有效 RFB 实例，不无限创建重连对象。
2. 截图当前宽高与浏览器输入坐标一致；切换可见性/侧栏/viewport 后重取截图。不要点击截图之外的坐标，不为让图片更大而盲改 viewport。
3. 键盘焦点在来宾画面与预期 Windows 窗口。文本输入 API 可能只修改浏览器文本域，而 noVNC 依赖键盘事件；严格按工具文档使用受支持的键盘接口，不自写按键注入。
4. 实际操作后是否有新截图变化。鼠标形状变化、页面事件回执和 HTTP 200 都不能证明来宾按钮被按下。工具点击接口未能传递有效输入时，检查受支持的浏览器/计算机工具和官方 noVNC 行为；不能把某次未证实的拖动、延迟或键码替代方案写成通用修复。
5. Windows 是否卡住、仍在初始化驱动或处于安全桌面。按环境授权处理 UAC；不要因为用户“没看到窗口”就称虚拟机已可见或擅自关掉 UAC。

无法证明输入有效时保留现场，标记 GUI 未验证。基础设施问题在基础设施层修复，不改产品逻辑来避开测试。

## 黑屏或启动循环

确认固件类型、boot order、原系统驱动与镜像链。`media` 只换 CD，不自动把启动顺序改成从 CD 首启。进入 PE 的 boot order 要在新建测试运行前按已授权目标明确配置；当前通用启动器默认硬盘启动。不要把切换启动配置当作产品重装步骤。

## 日志取出与磁盘容量

优先来宾程序自己的真实日志、小型专用导出盘或已授权测试 harness 的串口输出。串口默认为文件输出，并不自动产生 Windows/应用日志；来宾必须实际写 COM1。

对整盘做 raw 转换，可能需要接近虚拟磁盘容量的空间；raw 的逻辑长度和实际占用不同，部分 Windows 文件 API 扩展文件会填零。不要用“只设一个稀疏标志”推断不会占满磁盘。转换失败保留错误，确认范围后只能处理自己本次生成的临时文件，不能清理别的项目或既有证据。

## 正常关机未结束

`shutdown` 只是发送 ACPI 请求。来宾可能忽略或有未保存对话框；用浏览器观察并按授权完成正常关机。`status` 返回 stopped 及回执后才离线读取 overlay。不把“请求成功”写成“已关闭”，不自动强杀、重置、删除锁或重启。
