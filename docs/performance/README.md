# 双后端性能候选

2026-10-29 用户授权一起实施 MiniBlink 与 WebView2/Scratch 两个方案，并要求发布体积不增长。当前工作是隔离源码候选，构建、测试及实际性能由专人集中验证；此目录不代表性能目标已经达到。

| 工作 | 当前源码状态 | 入口 |
|---|---|---|
| MiniBlink ready/deadline、2ms/32项批次 | 保留主线 AVL 索引，补充唤醒及 worker 合作预算，待实际验证 | [调度交接](scheduler-handoff.md)、ADR 0143 |
| 精确帧节拍、按需mirror、资源就绪通知、legacy延后、dirty策略 | 已实现并修复静审发现，待实际验证 | [呈现交接](render-handoff.md)、ADR 0138 |
| 关键输入结束容量 | 保留主线 mailbox、FIFO ordinal 和 ordered cancel，替代旧候选 tickets | ADR 0141、[主线集成](main-integration.md) |
| WebView2 show/resize、2ms回复预算、锁外lease释放 | 已实现，待实际验证 | [WebView2交接](webview2-scratch-handoff.md)、ADR 0139 |
| 完整高刷采样、结束后排序、真实工作量能力、面板低扰动 | 已实现，待实际验证 | `demos/scratch/web/performance-meter.js` |
| E/W/S 中的独立 raw WebView2 W 宿主 | 不链接完整 SDK Host，独占 profile、完整资源身份与有限采集入口已提供，尚未编译 | [W 实验说明](../../experiments/scratch-webview2-baseline/README.md) |
| Scratch源码候选 | reducer一次遍历、真实VM/draw计数及可追溯生产构建入口；尚未编译 | [源码构建](../../demos/scratch/upstream/README.md) |
| 共同体积门禁 | 三项字节预算和ZIP/runtime内容绑定工具与回归源码就绪 | [体积门禁](size-gate.md)、ADR 0140 |
| GPU/Composition/PGO/Worker/skin共享/列表虚拟化 | 没有本候选profile，未默认采用，条件工作未验收 | 不将设计或诊断能力标成收益 |

PR #129 已按当前主线重新集成，底稿为 `0cdc0a9ace0cdce664ec4a9b54e5f8f71b97ba48`。旧冻结候选 `ce996ac8e534c634ced4389dd06f0a8dba0f0def` 及其基线 `c17c79bdf470fae681c63324ceaa2a519151afc9` 仅为移植来源；原有快照不整体覆盖主线。保留主线发布、DevTools、输入、渲染和工具修复，具体取舍见 [主线集成](main-integration.md)。源码保持 `VERSION=0.6.0`，没有重发或替换已发布包。

交付范围、条件优化和一次集中验证顺序见 [验证交接](validation-handoff.md)。新报告以完整实际墙钟窗的 `windowRafFps` 计回调率，旧 `rafFps` 保留历史闭合间隔含义；真实停顿及零帧结果不丢弃，头尾缺帧使分位数资格单独失效。历史结果不改写。

统一交接只运行受影响集合：native MiniBlink scheduler/frame/input、WebView2 owner policy、Scratch meter/reducer与大小读取器；再完成对应内核两架构匹配构建与实际输入/画面/关闭。相同设备/页面/工作量下的前后和 Electron 对照、GPU/CPU/内存、ZIP与安装大小必须绑定真实候选，未执行全部保留未验证。QEMU功能结果不证明物理帧率或发光延迟。

当前未能由代码单独决定的热点不擅自采用大重构；首次集中profile后依数据继续剩余条件工作。代码完成、编译通过、大小通过和性能通过分别记录，不能以当前文档结束性能验收。
