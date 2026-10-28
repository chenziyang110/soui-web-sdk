# 拖出数据有限所有权回归

一个测试 EXE，11 项 CTest，每项 TIMEOUT 10。正常与故障路径均要求测试进程返回 0；不使用 WILL_FAIL。构建前强制检查七个 live 源文件的完整 bytes/SHA256、fixture hash 和十个 source/fixture 精确字节区间，禁止以失效的复制算法继续通过。

复用实际 WebVector、WKE 类型及 buffer 创建/释放、BrokerDragPayload、数据转换器/释放器、startDragging、callback scope、拖动标记与 CheckReEnter 计数。array new、UTF8 返回 std::string 和下游 make_shared 使用真实 C++ allocator 故障；已持有的 2/4 个真实 malloc block 必须由实际释放器归零，heap/buffer delta 同时为零。检查受控 257 项走实际 invalid/NONE 完成分支、legacy/custom 257 项继续传递、callback 与参数异常恢复局部计数，以及混合/空数据正常释放。

Blink string/URL、page/frame、GC 和调用入口是声明支持，不证明完整 Blink/GC/page 生命周期、legacy 内部异步 task、真实 OLE 或 vendor DLL。unchecked malloc 的 null/non-return 故障不在此恢复资格内。进程自报 PID/decimal FILETIME 只是观测，外部运行器仍负责真实进程与作业闭合绑定。

局部 .gitattributes 强制 CMakeLists.txt、*.cmake 与属性文件自身使用 LF，避免 checkout 行尾转换改变固定字节。

2026-10-26 仅更新 SOURCE 来源：渲染接线改变 WebPageImpl.cpp 的完整文件散列与前置偏移，原十个拖放片段逐字节未变，只精确重定位并刷新完整源文件 pin；fixture 与十一项判断不变。未执行配置、编译或测试，历史结果不能作为当前源码通过证据。

2026-10-09，full-native-suite-v5 在同一 Windows 开发机的 x64 与 Win32 各通过 87/87 项注册 CTest，其中本目录 11 项公共回归均通过。CTest Owned Job 的根进程/监督器退出码为 0/0，无强制 disposal 并自然闭合；配置/编译的 0/124 仍为 genericPassed=false 的受控尾进程 partial，整体 allGenericPassed=false。该有限实测不关闭 Win7/PE、GC/OOM 或 DLL 卸载资格。后续已接入默认 legacy DragHandle::startDragging 的同步异常清理修复，并新增相邻 legacy_drag 三项源码体回归；2026-10-09 的 full-native-suite-v10 在同一开发机 x64/Win32 各通过本目录十一项与相邻 legacy_drag 三项（各 14/14）；当轮八个 Owned 阶段均自然 0/0、allGenericPassed=true，绑定当时 083f78... 的实际源码与字节夹具。原 V5 的 0/124 partial 记录不变，既有异步交接仍未取得完整生命周期资格。finite source-body scope=true，fullGCQualified=false、pending_ctorOOMClosed=false、fullVendorBuildQualified=false、releaseReady=false。可通过父测试入口调用；独立构建时默认从目录相对位置找仓库，源码整体搬移不依赖开发机路径。
