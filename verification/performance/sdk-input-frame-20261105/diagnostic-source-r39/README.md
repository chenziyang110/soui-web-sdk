# R39 v5 源码交接快照（未采用、尚未真实验收）

本目录只保存已经过源审的 15 个文件的原字节副本，以及本次说明和复制清单。没有构建、运行或自动启动；不含 EXE/DLL、旧 build/entry 操作驱动、日志或 SDK 包。原件、R38 快照、旧失败与冻结请求均保持原处。

## 目录与接入边界

- 根 `CMakeLists.txt` 与 `project/benchmarks/protocol/`：只定义 `BurstInputCellFeedback`，输入器和原 observer 两个 TU，标准 v143/C++17、user32/gdi32；不编译 Host、SDK 或内核。
- `page/`：独立 `soui.burst-page/v5` / `spatial-cell-feedback64/v5` 页面，仍使用原单次 bootstrap、原 Host 生成的受校验 `burst-config.js` 和同步 getter。它不是可由任意浏览器独立打开并取得同等证据的页面。
- `reader/`：严格核新 v5 输入/页面身份；保留原 desktop v2、24 字节 marker、flags2、CRC、持久 mask 和捕获门。未将底层像素布局重新版本化。
- `tests/`：针对实际页面/读者的有界 CPU 合同，不能证明真实 Windows 输入、Blink、SDK mailbox、TLS、GPU 或物理显示行为。

实际运行仍须明确复用被冻结的 R2 Host `c4320c14cc641b904e920bd6a1eff918cb002a560e430837b4a0fde9a9339aaa`、匹配已安装 SDK `0.7.0-preview.4`（源码 e1d…）及原 node56d…身份。它们未复制到这里；完整载荷必须由原冻结收据验证，不凭散列前缀构造新来源。没有新增 Host/SDK/public ABI，也未提供 0.6.0 基线适配或资格。

## v5 的新语义与不变硬门

64 个固定半开 cell：i 从 0 起，x=[32+16*i,48+16*i)、y=[120,136)，请求中心为 (40+16*i,128)。仍为64@125 Hz、原 normalizer/DPI setter 时序、NOCOALESCE、ACK500ms和原全部监督/关闭期限。它是不同轨迹，不能与旧相邻单像素序列称为同负载或计算收益。

cell只编码序号，不替代因果关系。全部64 native tag及T1、完整tuple+inputID、正常T3/T4对同步getter的包围、DOMordinal、严格1..64首次setter顺序和每项真实firstsetter仍必须齐全。首次setter的DOM实际XY必须与其tag原生rawXY精确相同，且该点属于预声明cell；不做nearest/round/clamp，不容忍SDK→DOM的1px错误。请求中心与实际native坐标的delta另报，不称精准落点已修复。

旧Host的相邻网格mismatch按旧公式重算并核原summary；本轨迹的legacy flag应为true，不能改false或隐藏它。tag0、多DOM、unknown原样保留；缺失isTrusted仍为unknown，固定源作用域不等于任意页面认证。run/PID/birth/QPF、同EXE TLS、cold0、实际Engine point12负控、完整trace无loss/overflow、CRC/nonce/零位/单调mask/全64、真实前台/命中和自然关闭门保持。

## 当前证据与交付限制

源审绑定 `source-plan-r2.json`，SHA-256 `baf93aa86bc2fb169bebf06f75e931ce0ef67831975e4b62641d57684f6f7135`；独立源审收据SHA-256为 `8fe103fbd31388ed40a3a61dcf1b311aaef67674e53afa66f2f47dafb5d04df0`。CPU原件是首轮8/8，随后FIFO/旧schema定向2/2，再Host排序键定向1/1；不是最终整组10/10复跑，也不是原生验收。

新地点尚未构建，v5尚未取得完整真实64项资格。Root在原目录执行的构建/未来采样不自动授权此副本或新二进制；需重新绑定实际源码、工具链、产物和请求。若以后明确决定构建，应复用仓库标准MSVC隔离/准入/自然生命周期流程，在独立输出执行；不使用本机旧操作脚本，不覆盖冻结SDK/原产物，不自动GO、延时或重复运行。

R37/R38精准点偏差、缺8个native tag及失败原因仍开放。v5即使通过，也只限定为实际native cell事件到首次有效软件捕获区间，不代表物理输入/光子延迟、64独立呈现帧、默认采用、Electron对齐、性能收益或整个Goal完成。

`source-inputs.json` 使用本目录相对目标路径及原证据根相对来源，逐项记录字节数/散列；所有源副本已逐字节等于冻结原件。复制收据和README为本次生成元数据。清单不包含自身散列，避免循环引用。
