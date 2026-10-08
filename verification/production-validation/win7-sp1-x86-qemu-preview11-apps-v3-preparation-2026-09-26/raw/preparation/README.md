# preview.11 Win7 x86 三应用输入布局 v3

状态：**文件准备与合成校验已完成，v3 客体尚未执行**。本轮仅修正传输卷布局，不修改 SDK、已编译应用或客体执行脚本，不把准备、自检或历史样本称为三应用通过。

## 为什么改变布局

真实 `preview11-win7-apps-attempt2` 的 5,978 字节串口包含完整终态及 8 帧有效散列，Win7 SP1 x86 / PowerShell 2 实际诊断显示：清单 809 字节、9 条记录，与冻结输入一致；卷根实际枚举 11 项，其中额外 `D:\$RECYCLE.BIN` 属性为 `Hidden, System, Directory`。无目录/10 项的原 guard 因此正确拒绝，退出 1，ZIP 与应用尚未开始。

此记录确定直接触发项，未确定谁创建该目录。原串口和 8 份原文件字节保留在 `parent-attempt2/`，来源与散列见 `layout-preparation.json`；它们是 v2 失败证据，不是 v3 结果。

v3 将完整输入放入卷内专用 `soui-input` 目录。卷根系统元数据在输入目录外；**没有加入忽略 `$RECYCLE.BIN` 或任何未知目录的规则**。若该目录或任意额外文件出现在 `soui-input` 内，原 guard 与宿主校验器仍拒绝。

```text
volume/                         ← 唯一挂载到 QEMU 的宿主目录
└── soui-input/                 ← 客体 D:\soui-input\，严格验证此目录
    ├── 7za.exe
    ├── SouiStarter.zip
    ├── SouiWorkbench.zip
    ├── SouiStarter-payload.tsv
    ├── SouiWorkbench-payload.tsv
    ├── transfer-files.tsv
    ├── run-preview11.cmd
    ├── run-apps.ps1
    ├── verify-apps.ps1
    └── export-serial.ps1
```

## 输入身份与保留范围

- 编译候选：`0.3.0-preview.11` x86 Release，build ID `7e000158-98f0-4349-854e-15c581663737`。
- compiled source SHA-256：`a0c1d7900d39555508288d5459e65d3852e543cf7ec3879126bd6af59d2c2550`。
- 父准备目录：`preview11-win7-preparation-v2-20260926`，freeze SHA-256 `20f5b68956e9483dfeff26349bd81d49bcd81e4f6aeb8bf0ee7d462588dc9d13`，input SHA-256 `d8d509e2932074a5a677b9dab4e4c1d77c0f9d34108edf95c4a132e4a6d1f4e6`。原文保存在 `parent-freeze.json` / `input.parent.json`，旧目录保持只读。
- 父输入的全部 10 个传输文件保持原字节（包含两个 ZIP、脚本和全部 TSV），Starter 71 文件、Workbench 475 文件及两者各 5 个 legal 文件也逐项复制核对；不重新压缩、不重编。
- `input.json` 增加 `layoutVersion=3`、精确宿主 `mountRoot`、`inputSubdir=soui-input`，保留 `diagnosticsVersion=2`。`Verify-Frozen.cjs` 检查宿主挂载根只有该目录、目录内恰好 10 个普通文件、全部散列，以及父输入的载荷/传输记录完全一致。
- `Verify-AppSerial.cjs` 要求实际 guest Transfer 为某盘符下的唯一 `soui-input`（可带尾斜杠），每项 FullName 与该路径相符；仍要求全部四份诊断、逐文件输入/资源散列、原始退出码、命令行、ready 和有序关闭。

不复制旧 synthetic 大样本或历史记录 ZIP。当前目录保留运行所需完整载荷、父 freeze/input、构建记录、实际 v2 失败来源、新验证脚本与当前 synthetic 自检；`freeze.json` 精确冻结本目录所有文件，自身除外。

## 已执行的文件与合成检查

`self-test-only-v4-results.json`：1 个完整 synthetic 样本接受、27 个负例拒绝；新增负例包括错误卷根/子目录、目录内额外文件及 `$RECYCLE.BIN`，并保留原有 hash 正确但非零退出、超时、缺失/乱序关闭等拒绝案例。校验器只读，不创建 `results/`。所有 synthetic 文件都不是客体运行证据。

`SelfTest-Diagnostics.ps1` 在宿主仅调用四个诊断函数，读取当前输入；另用原始 guard 对增加一个真实 FileInfo 或 DirectoryInfo 的枚举数组验证两者均拒绝。它不执行 guest harness 主体、ZIP 工具或应用。结果见 `host-diagnostic-self-test.log` / `host-transfer-diagnostics/`；语法检查见 `syntax-check.json`。这些检查不替代 v3 的实际 Win7 执行。

## 第三轮精确启动入口

由监督者确认无其他 QEMU、SOUI 桌面负载或续期夹具后，先执行：

```powershell
node C:\SOUI-SDK-production-validation\preview11-win7-preparation-v3-20260926\Verify-Frozen.cjs
```

新建独立 attempt，使用已授权基础磁盘的新 snapshot、既有硬件配置和无网络。只读 VVFAT 的唯一挂载路径如下；不能误挂 `volume\soui-input`，否则又会把输入放回卷根：

```text
-drive file=fat:ro:C:\SOUI-SDK-production-validation\preview11-win7-preparation-v3-20260926\volume,format=raw,if=ide,index=1
```

实际空闲端口、PID、串口路径由监督者确定并归档。确认桌面与盘符后，只发送一次（若传输盘为 D:）：

```cmd
cmd.exe /c D:\soui-input\run-preview11.cmd
```

原入口通过 `%~dp0` 将子目录传给 guest harness；不需要修改任何脚本。客体固定输出目录仍拒绝覆盖，故使用 fresh snapshot；每应用期限仍 60 秒，整体客体监督建议 10 分钟。失败保留原样本，不重复发送命令。

将原始串口保存到新 attempt 目录（冻结输入目录之外），以本目录校验器检查：

```powershell
node C:\SOUI-SDK-production-validation\preview11-win7-preparation-v3-20260926\Verify-AppSerial.cjs <本次实际串口绝对路径>
```

成功仍需 34 个基本结果文件，可额外包含 0–3 个对应导航日志。即使三应用短测通过，也不覆盖真实 30 分钟/8 小时续期、V02 输入/滚动/缩放、V04、物理 Win7、资源性能或生产准入。`guestExecuted=false`、`sourceReproductionClaim=false`、`releaseReady=false` 是当前 v3 准备状态。
