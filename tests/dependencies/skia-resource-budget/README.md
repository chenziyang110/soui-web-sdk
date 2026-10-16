# Skia 驻留预算与外部所有权回归

main.cpp 是针对交付 Skia 实现的 10 类原生测试，不使用缓存替身。
使用 Build-Dependencies.ps1 生成的同架构 skia/libjpeg/libpng/zlib 库、
Miniblink 的 SkMemory_new_handler 对象和生产 SkResourceCache 编译参数构建。
测试主体需要 C++17 与异常支持；生产缓存对象仍使用原生产参数。
修改缓存对象时把它直接链接在 skia.lib 前，避免链接器选取旧实现。
实际验证需要同时运行 x86 与 x64；正常成功输出为
“10 actual SkResourceCache byte-budget/ownership cases passed”，退出码为 0。

测试使用真实 SkBitmap 和 SkMipMap 数据引用验证拒收与淘汰后的可用性。
它只验证缓存语义与所有权，不证明完整内核、实际呈现或长期资源资格。
