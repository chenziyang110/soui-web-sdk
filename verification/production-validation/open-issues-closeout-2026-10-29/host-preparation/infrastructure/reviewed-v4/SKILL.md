---
name: qemu-novnc-lab
description: 使用 QEMU 与 noVNC 在后台运行可由网页操作的隔离虚拟机，支持 Windows 宿主上的 x86_64 来宾及 Intel Mac 上的 macOS 来宾。通过跨 Agent 共享登记、独占操作和状态交接复用已有镜像与虚拟机，避免重复创建增量盘。用于桌面软件、安装卸载与启动介质测试；Apple Silicon macOS 来宾尚不支持。
metadata:
  display-name: QEMU + noVNC 隔离测试
---

# QEMU + noVNC 隔离测试

用后台 QEMU 运行来宾系统，通过本机网页里的 noVNC 操作真实界面。保留持久 VM，按任务保存证据和交接摘要。安装标识统一为 `qemu-novnc-lab`。

## 所有 Agent 共用的入口

先读 [共享镜像与 VM 管理](references/shared-vms.md)，再操作 VM。两个平台统一使用根目录 `scripts/pool.py`，默认登记表在当前用户的 `~/.qemu-novnc-lab/catalog.json`，与任务目录和技能安装位置无关。大镜像仍放在已选数据盘；不要为每个 Agent 改一个 `QEMU_LAB_HOME`。

1. 先 `pool.py list`，再检查用户已指出的 VM 路径和已有配置；清单为空只表示尚未登记，不证明本机没有 VM。找到可用实例后登记原路径，不复制、不重新安装。
2. 按系统、软件状态和网络需求选择兼容 VM，`claim` 领取使用权；忙碌时等待或协调交接，不改名建替代 VM。同一台 VM 同时只允许一个 Agent 输入，子 Agent 的操作由持有者串行调度。
3. 带返回的 token 执行 `start`；运行中复用当前实例，已关机复用原增量盘和 NVRAM。读取此次 URL，查看实际来宾状态；不能用基础设施 running 代替桌面验收。
4. 交接前停止输入、关闭自己打开的控制页面，用 `release --summary` 写明来宾状态、已安装软件、未保存工作、结果与证据位置。关机和释放使用权是不同操作；可按任务需求保持 VM 运行。
5. 只有已核对现有资产、确实没有兼容实例或任务明确要求独立环境时，才登记新配置并显式 `create --reason`。目录非空、磁盘不足、状态异常或创建失败时停止检查，不自动换目录、下载镜像或重复 prepare。

`pool.py` 的使用权用于 Agent 协作，不是访问安全隔离；旧网页仍能输入，所以交接必须停止旧控制端。底层 `lab.py prepare / fork / ensure / start` 保留用于维护与兼容，不作为绕过登记和占用规则的日常入口。不自动清理、合并或删除既有磁盘。

## 先选择平台

先确认宿主系统、CPU 架构、来宾系统及是否已有 VM，再选分支。两个分支的配置、运行元数据和生命周期不同，不能互换启动器或把已有运行目录交给另一分支。

新配置和运行记录显式携带 `profile`：Windows 通用分支为 `generic-x86`，Intel Mac 为 `macos-intel`。启动器会在写入或启动前拒绝混用；旧记录按原磁盘布局识别，不为补标识重写现有 VM。

| 宿主与来宾 | 启动器与文档 | 日常使用 |
|---|---|---|
| Windows 宿主，x86_64 来宾 | `scripts/lab.py`；[Windows 设置](references/setup.md) | 通过共享管理器复用运行中的 VM 或原位重启已关机 VM。 |
| Intel Mac 宿主，macOS 来宾 | `platforms/macos-intel/scripts/lab.py`；[Mac 使用指南](platforms/macos-intel/guide.md) | 同样通过共享管理器复用；保留系统盘、OpenCore、恢复盘和 NVRAM。 |

Mac 分支使用 HVF/host CPU，包含可选的 UTM launcher 源码适配、OpenCore/恢复盘独立 overlay、持久复用和带确认的网页重启按钮。Apple Silicon 不使用此 Intel 配置。通用启动器虽接受 KVM/HVF 参数，Linux 宿主及其他宿主/来宾组合仍需单独验收。

下文“Windows 分支”只适用于第一行；Intel Mac 直接进入 [Mac 使用指南](platforms/macos-intel/guide.md)。两边的历史验证分别记录，不能把已有记录当成本机验收。

## 技能目录与安装

通过 Skills CLI 安装后，先定位当前加载的 `SKILL.md` 所在目录，记为技能根目录。本文所有 `scripts/`、`references/`、`assets/` 路径都相对该目录；运行脚本时使用完整路径，例如 `python "<技能根目录>/scripts/lab.py" --help`。不要在用户项目目录直接执行 `python scripts/lab.py`，也不要把配置、镜像或运行产物写入技能安装目录。

Skills CLI 只安装本技能及随包资源；系统依赖按所选平台文档准备。两个平台共用根目录的 `scripts/lab_common.py` 和 `assets/index.html`，必须完整安装整个 Skill，不能单独复制平台子目录。

## Windows 分支：开始

1. 确认测试对象、已有 VM 的系统/软件状态、网络授权和证据目录；执行上面的共享流程。
2. 确实需要首次配置时，读 [references/setup.md](references/setup.md) 和 [assets/lab.example.json](assets/lab.example.json)。需要 Python 3.10+、QEMU、noVNC 和合法获取且已关机的基础镜像；包内不含操作系统或第三方二进制。
3. `pool.py start` 返回 URL 后，用当前获准的浏览器工具打开。在 Codex 中按 [references/browser-testing.md](references/browser-testing.md) 使用 `mcp__cua_repl`；看见实际桌面后再测试。
4. 保存真实截图及结果。需要关机时带 token 执行 `pool.py shutdown`，用 `status` 确认停止，再交接摘要。不自动删除运行目录，不批量结束 QEMU/Python 进程。

示例的 `python` 指已发现的 Python 3；路径有空格时按当前 shell 规则引用，脚本内部不通过 shell 拼接命令。

## 运行约束

- 默认后台 `-display none`，VNC、WebSocket、QMP、HTTP 都只绑定 `127.0.0.1`；不改防火墙、不暴露公网。loopback 服务仍可被本机其他进程访问，不把 VM 当作对恶意样本的强安全沙箱。
- 父镜像链显式只读，只有新建的 overlay 可写；UEFI 变量使用本次运行的副本。禁止提交、修复、rebase 或写入父镜像；禁止把物理宿主盘当测试盘。
- 配置的 `network` 必须明确为 `none` 或 `user`。`user` 是 NAT，可能访问网络及宿主服务，不等于网络隔离。只按项目授权启用。
- 此技能提供基础设施，不授权安装陌生软件、接受协议、修改凭据、付款、清空真实 U 盘或绕过 UAC；沿用当前工具和用户授权规则。
- 来宾 UI 全程走获准的计算机/浏览器工具。QMP 仅用于身份/状态、换 CD 和关机；Mac 分支额外允许用户明确要求的唤醒和重启。不通过 QMP 键鼠、VNC 自写客户端或来宾命令注入代替真实 GUI 验收。
- 不运行宿主上的被测产品、安装器或业务 worker。串口测试 harness 可导出已授权的来宾测试日志，但必须标为自动化证据，不能冒充 GUI 证明。
- Windows → PE 的启动方式与原系统一致。BIOS 与 UEFI 不能随意切换；USB 写盘测试只使用专用、可丢弃的虚拟 USB 盘，不直接透传宿主 U 盘。

## 验收与排障

结论标明 `已验证 / 失败 / 未验证`，附实际包 hash、来宾系统、触发步骤和证据位置。安装测试按用户范围分别观察首次安装、自动打开、退出后的覆盖安装、卸载、快捷方式与登记残留、卸载后重装。PE 测试另核实启动、目标身份和重启后的真实结果。

画面出现不等于输入有效。点击后读新状态，核对焦点、画面尺寸和结果；无响应时按 [references/troubleshooting.md](references/troubleshooting.md) 排查，不能反复盲点或改产品来迁就测试环境。

Windows 旧控制端丢失并留下 `unconfirmed` 时，普通启动仍拒绝；持有原 VM 使用权后，按[显式离线恢复](references/troubleshooting.md#windows-旧控制进程丢失状态为-unconfirmed)的精确会话与摘要检查恢复原运行目录。该维护能力保留旧结果为 UNKNOWN，不自动启动、不新建磁盘、不替代正常关机证据。

离线取证优先只读 ISO 输入、专用来宾日志和小型导出盘。不要默认把整块系统盘转成 raw；其物理占用可能耗尽宿主空间。只在 VM 已关机且容量、格式和工具支持都核实后离线读取。

## 随包工具

- `scripts/pool.py`：跨平台共享登记、镜像清单、独占使用权、持久 VM 复用与交接；见 [共享管理](references/shared-vms.md)。
- `scripts/lab.py`：底层 Windows 启动器，支持同一运行目录持久重启。
- `assets/index.html`：官方 noVNC RFB 的显示与输入页面，有连接状态和手动重连；不含自定义输入注入桥。
- `scripts/test_lab.py`：配置、磁盘链、HTTP 暴露范围及 QMP 身份回归；不启动产品。
- `scripts/lab_common.py`：共享 JSON 原子写入、文件与平台检查、只读磁盘链、端口、QMP 和 HTTP 资源服务。
- `scripts/test_cross_platform.py`：平台混用、旧记录兼容、并发写入、路径、HTTP 与真实子进程协议夹具；`scripts/test_page.mjs` 验证共享页面的能力开关和交互状态。
- [references/validation.md](references/validation.md)：此版本的实际验证范围及复刻后的验收要求。
- `platforms/macos-intel/`：完整 Mac 分支，含启动器、UTM launcher 源码与许可证、页面、配置、回归和平台文档；详见 [Mac 使用指南](platforms/macos-intel/guide.md)。

先用 `python "<技能根目录>/scripts/lab.py" --help` 查询命令。缺依赖时报告确切缺项；不静默安装、不启用系统虚拟化功能、不静默切换慢速模拟。
