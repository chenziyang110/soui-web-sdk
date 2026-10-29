# ADR-0137：Windows 工具准备的自然退出与可校验恢复

状态：源码候选，待本轮必要短验。基线为合并PR127后的main；不修改已发布0.6.0载荷或资格。

## 事实与决定

MSVC已固定工具集14.44.35207/Hostx64。匹配VCTIP自身帮助列出`-timeout:<seconds>`；历史固定二进制真实CMake四阶段已使用20秒idle并自然归零。正式MSVC入口在configure/build各自持有共享mutex，拒绝已有compiler/service/ready event，固定VS实例及工具身份，启动自己持有的`-timeout:20`服务，等待其自然退出。30秒本地等待在原root预算内，不扩大外部root/drain，不终止进程或授予compiler-tail特例。原服务结束后出现替代服务仍失败。LLVM不启动该服务。预备工具fingerprint包含所有新执行的发现及生命周期脚本；旧prepared记录不能改挂新脚本。

原八次compiler失败保留，有例包含多个残留成员；新机制仅处理已证实的服务生命周期不匹配，不把它描述成所有旧失败的唯一根因。固定二进制pin不匹配则拒绝；工具集升级需重新核验帮助、配置、IPC及生命周期。

SDK默认发布清理与六次rename/450ms不变。显式`--retain-failed-publication`只在完整校验后rename被EPERM/EACCES拒绝时留下stage，并输出manifest SHA256。`recover-publication`是独立操作，要求原输出的UUID sibling stage、未变manifest、所有payload字节及完整文件闭集、无现有目标后才rename；失败保留stage，不先复制覆盖，不结束持有者或改ACL。原失败仍非零，恢复不追认它成功。保留模式由调用者控制，避免默认积累大stage；调查完成后只清理自己持有且核对过的stage。

原EPERM现场已销毁，无法判定锁持有者。目录内打开的子文件可以阻止rename，所以root-CWD的EBUSY控制不足以排除子文件争用；不把它归因Defender或MAX_PATH。[Microsoft目录重命名约束](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/ns-ntifs-_file_rename_information)与[libuv错误映射](https://github.com/libuv/libuv/blob/v1.x/src/win/error.c)支持该边界，并不证明历史持有者身份。

PS5历史空stage来自外部数据准备脚本，没有独立解压退出/stream证据。新增可复用checked ZIP入口：调用者提供归档pin和完整文件清单；只读枚举拒绝非法成员，7-Zip写短UUID sibling stage，在同一个单调总期限内复核归档/工具身份、完整目录与文件闭集、大小/SHA后发布。退出0、空目录或部分解压不能当成功；清理再次失败不掩盖原错误。它用于数据准备，不执行解出的应用或安装器。旧解压失败原因保持未知；不改变无关联的WebView2 NuGet恢复实现。

## 兼容、验证与回退

SDK包格式、版本、业务运行、权限和资源fence不变。新增CLI可选项与独立恢复命令不改变默认pack成功结果。既有toolIdentity无deadline调用保持；新的准备入口共享单调期限。

验证仅覆盖当前改变的失败/恢复、路径与身份边界，以及两个目标架构的微型真实CMake阶段自然Job收尾；不重做DevTools已过用例或整包长测。长测及完整生产资格继续后置，三资格false。可整体revert本候选；不要回写已发布包或旧报告。
