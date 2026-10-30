# ADR 0123：公共 DevTools 候选契约与原生调用链

状态：2026-10-23 候选实现，使用独立本地版本0.6.0-preview.1；按用户要求先完成代码整合，集中运行验证后才确认正式交付。保留既有0.5.0包和安装内容。

## 背景与依据

原型A已给出当前MiniBlink ABI2的受控前端、暂停控制及严格退休轨迹。真实SDK WebView2原型的限定场景已证明：原生open accepted、真实pause、scope撤销、Controller与completion退休、独占browser NORMAL事件和保留句柄退出，然后才建立无grant的G2。其窗口可见/连接状态仍unknown。原型的故障和源码断点缺口保留为集中验证项，不阻止候选实现。

## 决定

- `<soui/devtools.h>`定义原生策略、终止策略、逐项能力、三值观察、操作编号、撤销原因及不透明grant。`webview_host`提供configure、authorize、open、revoke、observe和窗口内快捷键判定。普通Bridge、CLI、环境变量、UIA和Agent不导出授权入口。
- 策略默认disabled，在首次mount前由原生应用配置。开启前必须明确选择后端实际支持的终止策略；`destroy_target`表示应用已处理未保存状态，并接受结束诊断关闭该Host。`detach`只在后端实现且能证明完整排空时可用；不可静默退化为销毁目标。
- grant是不可序列化的C++对象，绑定Host独立控制对象、当前进程实例、owner线程、实际文档generation/identity及grant epoch。只有成功authorize才生成。普通构造、其他Host、旧文档、撤销及过期对象均不能打开。可选到期使用原生steady_clock；不延长业务授权。
- renderer扩展是受保护的可选虚方法，仅Host facade调用；旧adapter默认明确E_NOT_SUPPORTED。原生应用本身仍是可信边界，本契约不抵御进程注入或任意原生代码篡改。
- Host负责统一许可检查与有界操作身份，renderer负责目标身份、受控窗口与资源所有权。open成功仅为accepted；attached、paused、visible分别依据真实后端观测，无法证明时unknown。不提供通用协议/脚本入口，也不猜测WebView2 inspector HWND。
- revoke先废止grant并立即关闭后端业务/调试新请求入口，然后非阻塞推进原暂停栈退出、detach和回调排空。destroy_target模式继续原Host unmount，并在后端最终屏障通过后才报告drained。WebView2拥有自己的独占runtime watch，不能借用外部watch后声称SDK已完成finish。
- `close`/`destroy`同样先关闭受控inspector并继续其原操作；pending保留Host、adapter、Bridge、窗口和回调。显式目标关闭意图优先于诊断的detach策略，且不以inspector曾经打开为前提：`pump`必须继续原unmount和最终屏障，不能走仅撤销未使用许可的快捷完成分支。watch终态未证明之前不能丢弃adapter。`pump`继续同一撤销，不嵌套模态循环，不重置外层应用截止时间。
- reload/导航必须在新文档暴露前阻断旧调试连接，旧grant立即失效。需要销毁目标的后端返回E_TARGET_RETIREMENT_REQUIRED；不得先导航后撤销。手动关闭前端与许可失效分开观察。
- F12/Ctrl+Shift+I仅匹配本窗口按键并返回请求，不发放许可、不注册全局快捷键。菜单和按键由应用使用同一原生授权函数处理。

## 所有权与行为后果

公共头和Host facade由root唯一负责；MiniBlink、WebView2分别拥有自己的内部会话状态。MiniBlink资源读取与native frontend代码进入SDK私有目录，不引用experiments或out库；沿用现内核导出ABI，保留raw-view保护、受限暂停控制与严格drain。WebView2将已验证的策略与导航门禁用于普通构建，固定debugger实验入口仍只在实验宏下编译。

SDK和应用目标显式要求MSVC `/EHsc`，保证native边界捕获异常时按C++规则展开RAII owner。构建调用不得以自定义`CMAKE_CXX_FLAGS`意外替换工具链默认异常选项。外部消费者仍使用各自合法的工具链配置。

正式前端修订源码放入`sdk/resources/devtools/miniblink/overrides/`，基线与逐文件身份由相邻`source.json`记录。自动协议错误诊断只记录固定类别/有限整数码，共用64条额度；结果及error data仍返回原面板callback，不被诊断日志复制。旧2ecb快照保持历史身份，新源码必须形成新manifest和原生交付pin。代码核查还发现旧vendor请求路径覆盖调用方认证数据的固定值；候选恢复由实际请求的`m_user/m_pass`提供认证，未指定值继续按原空字符串规则处理，不引入新的默认账户。此修改需要重编受影响TU与内核并重新绑定配对身份，旧9b516运行结果不能转挂修订载荷。

MiniBlink必要引擎实现由正式`third_party/sources/miniblink/`拥有：暂停时只服务同一实际Engine、Agent和session的受限控制任务，frontend client不进入业务绘制暂停标记；资源关闭先发布closeSequence，以callout计数、一次性permit和lookup lease维持callback owner，真实退休前保持pending/BUSY。旧原型的观察traces、固定坐标触发及诊断getter不属于候选能力。迁移必须核对实际编译的源文件与包含目录，不能只凭vendor树散列未变就认定实验实现已合入；新增头依赖和旧观察对象的外部引用都纳入受影响编译集合。SDK私有ABI2表保持既有布局，未向公共SDK增加实验观察接口。

授权/打开可在UI callout重入；Host生命周期guard应先于后端调用生效。后端未知关闭状态必须保留失败或pending，不能依据计时器、窗口消失或主进程退出合成成功。日志仅允许状态、错误和非敏感身份，不记录协议、源码、业务bearer和Console正文。

## 兼容性与迁移

新增可选虚函数及Host私有状态需要原生消费者重编译；维持旧adapter源码兼容和原构造函数。没有显式配置的应用保持disabled。WebView2配置必须早于prepare_async，Host配置后才开始准备；独占profile与SDK拥有watch是受控模式的明确条件，外部watch不能重复acquire。未验证的OS/Runtime与资源组合不新增支持声明。

不把实验宏打开作为产品交付，不把固定实验EXE的运行结果转挂新SDK。安装资源、模板接线、独立消费者及D01–D14仍属于本目标。

## 验证与恢复

实现期间执行受影响的编译、链接、资源身份和源码审查。集中验证覆盖默认拒绝、目标/进程/文档隔离、重复打开、到期、撤销和真实暂停清理、初始化取消/故障/迟到回调、旧adapter及外部消费者、双后端面板与源码断点。原30/40秒实验和各应用10/20秒关闭期限保持各自身份；新候选测试预算须事先确定，失败不放宽。候选稳定后执行原循环与8小时负载。

失败时保留原操作、owner和证据，仅修复受影响实现；不重建测试链路或机械重跑已通过且无相关变化的原型。接口存在或编译通过只计“已实现待验证”，不计生产验收。
