# MiniBlink native admission metadata 夹具

本目录只验证私有同步视图与原解析投影，不模拟 Host、jsEval、导航、Engine credit、worker或权限。已接入顶层原生测试CMake，并随candidate-owner-admission-sdk-r2在x64/x86 Release、LLVM22/MSVC142/19041各实际编译和执行33+4例通过；独立MSVC v143入口尚未执行。

配置时逐字节提取当前 `soui_miniblink_host.cpp` 的 bounded parser、packet_string、bridge_wait_timeout，生成源/片段SHA；任何片段缺失/歧义使配置失败。33个输入例覆盖投影/边界，另4个短/长raw最终移动及异常路径验证借用字段存活；类型约束拒绝复制/移动视图和借用临时字段字符串。无分配只在已解析字符串存在之后检查，不声称整个Bridge无分配。

预计独立执行入口（尚未执行）：

```powershell
cmake -S tests/native/miniblink_admission_metadata -B ../out-miniblink-admission-metadata -A x64 -T v143 -DCMAKE_SYSTEM_VERSION=10.0.19041.0
cmake --build ../out-miniblink-admission-metadata --config Release
ctest --test-dir ../out-miniblink-admission-metadata -C Release --output-on-failure
```

Host接线已完成非作者生命周期/错误顺序窄审，适用容量/旧context合同随上述两架构测试通过。匹配LLVM消费者还完成私有桌面真实bootstrap/probe、32预热+1024异步echo/保存/正常关闭；正常路径不替代真实导航重入和完整错误路径。本夹具不能证明已优化FPS、输入到光子或真实重负载成本。
