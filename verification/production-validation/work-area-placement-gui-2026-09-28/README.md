# 普通窗口工作区定位：现代 Windows 原生缩放实测

本次使用源码 SHA-256 `60d729c8c0fe55dcabd72bf3a1aa2255af42017c10c63151a880fecd7d511640` 的正式 Release 应用。x64 构建 ID 为 `c8d857b1-0083-40bf-ae3a-3b5ed5f4ad47`，x86 为 `b6caec6c-117e-49eb-86fa-4182e64d5b55`。两次成功测试的应用及输入探针、完整 Starter/Workbench 载荷和源码在运行前后经身份核对；本机主显示器工作区为 `(0,0,1920,1032)`。

首次 x64 `workarea-gui-x64-attempt1` 原始驱动退出 1：探针在窗口就绪前返回“visible unowned top-level target window missing”，当时的脚本把这一暂态错误当作终止异常，随后自身清理应用。原始失败、应用退出及前后载荷身份均保留。该失败不证明窗口功能缺陷；首次脚本的字节版本未在执行前冻结，归档的控制脚本是修正后的版本，不能反向冒称首次脚本的精确源码身份。

修正探针等待后，用全新 `workarea-gui-x64-attempt2` 和 `workarea-gui-x86-attempt1` 串行实测：两次应用在自然前台由原生探针执行鼠标边框拖拽，窗口从 `(535,196,1385,836)` 的 850×640 扩大到 `(535,196,1465,896)` 的 930×700；窗口句柄保持一致。两次都发出正常关闭消息、原生退出 0、无超时，完整载荷和源码前后身份一致。宿主分别记录 14 和 16 次外部负载检查，均为空，驱动原始退出 0。此结果只证明现代 Windows 本机 x86/x64 的普通窗口初始边框可操作、80×60 原生缩放和关闭，不证明 Win7 x86、800×600、小尺寸页面内容、中文 IME、30 秒公共任务并行、DPI/多屏或生产放行。

同时保留了 Win7 V9 的**静态输入源准备**：新 x86 构建、无 V8 标题栏移动补救动作、直接测试初始位置的 80×60 原生缩放；脚本 SHA-256 `206c8f86a481e44ee7392cb5a112fe28157840e9868efb2a0e8701a8d2c391df`。静态校验原始退出 0，但新输入尚未打包/冻结，客体未执行。详见 `raw/control/V9-PREPARATION.md`。

[原始文件清单](raw-manifest.json)绑定 76 份控制、GUI、命令和 V9 静态输入原件。`node Archive.cjs verify` 比较归档与仍存在的外部原件，并复核两次成功、首次失败和门禁语义；暂存后 `node Verify-Index.cjs` 比较 Git index 字节。`modernWindowsGuiVerified=true` 仅限上述窗口范围，`win7Verified=false`、`v02FullVerified=false`、`releaseReady=false`。[Issue #42](https://github.com/chenziyang110/soui-web-sdk/issues/42) 仍需 Win7 真机或客体新冻结输入复现/反证，保持开放。
