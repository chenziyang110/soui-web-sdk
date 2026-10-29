# ADR-0145：去除稳定 QPC 路径未使用的 UTC 转换

状态：生产函数局部重排与两架构源namespace行为验证已完成；完整内核/消费者重建待执行，不计入当前用户r2包。日期：2026-10-29。

## 背景

`content/web_impl_win/CurrentTimeImpl.cpp::currentTimeImpl()` 每次先读取 `lowResUTCTime()`。在QPC可用且已同步时，其结果既不参与返回值，也不更新在用锚；初次同步时又在精度窗口内重新读取并覆盖它。每次额外读取包含GetSystemTime和mktime。此数据流经独立源码复核，原始文件字节SHA256为 `83de811fe28ef57f6b0e0a73614ef81925c08097288be9f4de4c8eddc06c8d96`。

这能证明调用冗余，尚不能量化实际耗时或帧率收益。它只影响MiniBlink自己的时钟路径，不能解释或宣称改善当前WebView2 Scratch窗口的成绩。

## 决定

将低分辨率UTC读取限定到QPC不可用回退，以及首次同步的既有timeBeginPeriod/timeEndPeriod窗口。QPC elapsed、初始UTC/QPC锚、backwards guard、毫秒到秒转换及共享状态保持；不新增锁、线程局部时基、全局精度策略或运行依赖。

保留同步顺序：highResUpTime → timeBeginPeriod(1) → lowResUTCTime → timeEndPeriod(1) → 保存highRes锚 → synced。不可用分支仍每次读取一次UTC；首次QueryPerformanceFrequency会先于该读取。这不是逐bit相同的真实时间采样承诺，mktime也不作为无副作用纯函数处理。

保留旧未调用函数 `currentTimeImpl_unuse()`，不借机清理。它才使用文件级s_syncedTime进行drift检查；在用函数检查的是自己的s_isSyncedTime。UTC输入经本地mktime、tm_isdst未初始化和共享static并发属于独立既有风险，本改动不扩大为对它们的修复，不把它们当成已测FPS原因。

## 备选与兼容

保留每次读取会继续支付已证明未使用的API/CRT工作。替换整个时钟域、复用原始QPC trace值、改为线程局部锚或串行锁都会扩大精度/单调/并发契约，故本次不选。内部绝对deadline贯通是另一项，需要保持WTF时基与34ms提前flush，不混入这里。

不改变WKE/provider ABI、最低Windows、工具链、公开返回单位和正式发行版本。原文件有旧编码注释，只按原字节做限定ASCII改动并核查前后SHA，禁止全文件转码。实际DLL/消费者须从源码重建，不能替换预编译文件或重贴旧证据。

## 验证和回退

直接执行生产函数的有界确定性夹具，应证明初次同步、稳定QPC、QPC不可用、backwards guard、tick wrap/drift提示和精度提示平衡。使用受控Win32/CRT观测边界统计调用，不复制返回算法冒充真实函数；完整源/HEX守卫及原编码编译投影沿用项目做法。先对原函数执行能暴露稳定路径多余调用的用例，再对候选执行，记录失败与修复，不放宽断言。

在用户人工FPS期间仅准备源码/夹具。实际构建和执行待该轮结束，随后执行相关两架构验证及内核/消费者重建，报告EXE/DLL/runtime/ZIP实际增量。小测试不替代浏览器FPS、物理输入或完整生产资格。回退此函数的局部重排，并重建匹配载荷。

## 已执行的限定证据

用户报告本轮WebView2约69FPS，未导出JSON；该数值按人工观察保留，后续原生编译阶段已明确区分。x64/x86原生产函数均实际复现指定RED：4次稳定读取仍有4次UTC转换。首轮x64构建先因旧union零初始化的两类clang样式诊断失败，日志保留；仅在夹具导入区局部处理这两条诊断，生产字节与W4/WX保持。外部监督器将普通失败退出标记为root-failed，修正资格判定时复用已保存的x64真实RED，没有重复执行或改写该失败。

随后生产函数按原字节局部修改，SHA256变为 `28e65698db58df2261a054bce73b67b86c692e21ddcaa948e9d611e72fe58495`，源码减少50字节；在用函数外全部字节及原编码/行尾保持。候选x64/x86各5项实际namespace场景自然通过，覆盖steady/fallback/backwards/tick-wrap/drift。这是生产函数体加确定性Win32/CRT shim的范围，不是完整生产TU、真实系统计时、并发或FPS证据。

补丁及原始前后字节身份见 `third_party/manifests/miniblink-steady-clock-20261029.json` 与 `third_party/patches/miniblink/0026-steady-qpc-utc-read.patch`；当前树已执行反向非写入检查。完整新DLL、实际加载与消费者尺寸继续保持未完成。
