# Miniblink 流式摘要纯数据回归

此独立小目标直接编译当前 `WebCryptoImpl.cpp`，显式链接同架构已构建的 OpenSSL 静态库，不启动 Miniblink、产品或 VM，也不加入 SDK 全量测试矩阵。

数据测试直接实例化真实 `WebCryptoDigestorImpl`，保留实际 Blink 头、Release 宏和 `config.h`。生产 CPP 同时发出未使用的平台 WebCrypto 虚表，单独链接会引用 `WebString::reset/assign/fromUTF8`。本测试 CPP 仅为这三个真实方法提供一律 `std::abort()` 的 poison 链接定义：不伪造 Blink 类型、不返回替代结果，任何误用都会使测试非零失败。它们只存在于此测试，不进入内核或 SDK。工厂仍只作静审，此测试不覆盖其平台调用；真实 Elements 回归另计。

准备的用例覆盖 SHA-1/256/384/512 的空输入和 `abc` 已知向量、逐字节及含零字节分块一致性、交错独立 context、零长度空指针、非零长度空指针导致永久失败、未知及非摘要算法、一次 finish、finish 后 consume 和结果保活。DOM 回归只复现 `DOMPatchSupport` 的文本节点摘要输入及其十字节截取：旧原文前缀把 `42`/`43` 判同，新 SHA-1 前缀必须不同。这不替代原 Elements 真实提交回归。

从 SDK 根目录，在已批准的编译窗口配置此目标；传入本机同架构现存内核 `openssl.lib` 的绝对路径以及仓库外的新构建目录：

```powershell
cmake -S tests/vendor_crypto -B <fresh-build-dir> -A x64 -DSOUI_OPENSSL_ARCHIVE=<existing-x64-openssl.lib>
cmake --build <fresh-build-dir> --config Release --parallel 2
ctest --test-dir <fresh-build-dir> -C Release --output-on-failure
```

x86 使用 `-A Win32` 及 x86 archive。编译器、工具与原始输出的实际身份由执行者按既有捕获契约绑定；这些命令只说明测试入口，不代表已经执行或通过。当前 poison 版本尚未编译或运行，先前类型声明编译失败和平台符号链接失败的两次独立记录均保留。
