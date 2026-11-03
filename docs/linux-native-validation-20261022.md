# Linux x86_64 原生验证记录（2026-10-22）

## 结论与范围

SDK 基线 `abdbbbf0c7e62f2cddc81d596f583c4772111e52` （对应远端提交 `9cac14b3f10cd90bf0adc2846b5d6807d7eab126`）
加本次 Linux 无返回值求值修复，在云 Linux x86_64 桌面完成真实 WebKitGTK 窗口验证。
这不是仅编译或 fake backend 通过，也不是全平台、发行包或生产验收完成。

- 全新 SDK Release 构建成功，使用新运行时而非先前提取的系统 WebKit 库。
- `sdk.desktop.linux-native` 修复后 1/1 通过（1.42 秒，CTest 退出 0）：
  原生窗口、剪贴板、目录选择取消、远程导航/新窗口拒绝、刷新 preload
  换代，以及根窗口先关闭后子窗口继续工作。
- `sdk.desktop.linux-document-lifecycle` 真实 JSC 无 GUI 定点回归通过（112 checks、0 failures，CTest 0.05 秒）。
- 同代码 unified-desktop 示例实际显示并完成 Bridge JSON 往返、异步 readAsset、
  ES module、dynamic import、同源 fetch。修复前还检查了刷新重新连接、
  创建独立子窗口和关闭；修复后重新验证核心 Bridge/资源链路并正常退出（0）。
- 未执行全量回归、长期稳定性、DPI/多屏、Windows/macOS/ARM 原生验收。
  不声称 Electron 等价或可直接分发这个自构建运行时。

## 实际发现与修复

首次真实 CTest 失败，保留失败日志，未跳过测试：

1. WebKitGTK 2.52.6 的 `postMessage` 返回 Promise。SDK 的 evaluate 不对外返回
   JS 值，但 WebKit 默认序列化脚本完成值，导致 `Unsupported result type`。
   本次保持源码原样，仅忽略 `WEBKIT_JAVASCRIPT_ERROR` 域的
   `INVALID_RESULT` 完成值序列化错误，不忽略 `SCRIPT_FAILED`。真实 JSC 回归
   检查顶层声明与语法/运行异常；原生回归也检查截断语法和抛出 Promise。
   初版追加 `;void 0;` 会意外补全 `if(true)` 语句体，已在最终复核时撤销。
2. fixture 错把 `location.reload()` 之后同一 JS 任务发送的消息一律视为已过
   native 导航边界。现在仅允许 native reload 尚未被观察时的消息，边界之后
   仍断言拒绝；并未更改生产导航授权规则。

## 运行时来源与配置

官方源包：[webkitgtk-2.52.6.tar.xz](https://webkitgtk.org/releases/webkitgtk-2.52.6.tar.xz)，
[官方散列](https://webkitgtk.org/releases/webkitgtk-2.52.6.tar.xz.sums)。
源包 SHA-256：`179a2ea3f8f6edd4be7f31fdc55afc57bd0729f1fba648c61d4181539ac116fc`。

GCC 14 / C++23、CMake 3.31.10、Ninja 1.13；GTK 3.24.49、libsoup3、API 4.1。
Release 所有单元 `-O2 -DNDEBUG`；`PORT=GTK`、`USE_GTK4=OFF`、
`DEVELOPER_MODE=OFF`、`ENABLE_UNIFIED_BUILDS=OFF`、
`ENABLE_DOCUMENTATION/INTROSPECTION/MINIBROWSER=OFF`。
`ENABLE_BUBBLEWRAP_SANDBOX=ON`，使用提取的官方 bwrap/xdg-dbus-proxy，
未设置禁用 sandbox 的环境变量，未改系统权限或第三方二进制。
安装仅在工作区 `.build-tools/webkit-runtime`。

这是带本地源码编译补丁的验证运行时，不是官方发行二进制。补丁不属于 SDK
发布载荷，原始文件、patch 与生成输入清单留在本轮本地构建证据中：

- `0001-qualify-renderbox-isnan.patch`：限定 `std::isnan`；SHA-256
  `85a75eeea510be086973d6ce9a0fa7093451f72edc36ea988c40a33a529721b2`。
- `0002-qualify-shapeoutside-isnan.patch`：同类限定；SHA-256
  `b75268f4c67b7df0b89f5d846aa6e2e69b8127066726e972435d5f61d78b2bae`。
- `0004-partition-generated-serializers.patch`：本地可选
  `WEBKIT_SERIALIZER_PARTITIONS=4`，完整 type/enum 定义确定性分片，显式声明
  enum validator，四个输出全部注册 CMake；SHA-256
  `6ac4a66bf2fec20a9007d6eef76fe50824ac99f0fd7665af3a8ab757cf8bcfd3`。
  默认 1 输出与原版逐字节相同；4 模式核对 1863 个定义及条件守卫恰好一次。
  四个正式对象及最终共享库/三个进程均编译链接成功。
- 曾试验的 `0003` 单单元 `-O0` 方案失败并已撤销，不在最终构建中。

单体 serializer 曾在单并行约 7,090,448 KiB RSS 被终止；四分片独立 O2
探测峰值为 4,716,652–4,894,056 KiB。最终正常依赖重编，未伪造时间戳或
Ninja 日志，未用探测对象替代正式对象。构建机器约 9.7 GiB RAM、无 swap，
最终构建完成时磁盘可用约 25 GiB。

安装后库 SHA-256：

- libwebkit2gtk-4.1.so.0.21.10：
  `54daea8cc28a5ad9975aed39ce5b1415016754ab892dc8b2689eab043ad9e9fa`
- libjavascriptcoregtk-4.1.so.0.10.14：
  `6fe74e4d7624e2be8645b3afcb2216cabd4c987cb30efdab54a5b72a034e3759`

## 命令及证据索引

以下为本轮工作区命令，依赖提取目录和本地补丁并非仓库默认提供的安装环境。
SDK pkg-config 使用新运行时优先的元数据；提取依赖的 `.pc` 副本仅重定位
`/usr` 前缀到用户工作区，未改原包；运行时 LD_LIBRARY_PATH 也优先新运行时。

```sh
source .build-tools/webkit-source/env-userprefix.sh
cmake --build .build-tools/webkit-build --parallel 1
cmake --install .build-tools/webkit-build
# 新建 SDK 构建目录；PKG_CONFIG_LIBDIR 指向上述重定位元数据。
cmake -S . -B out/desktop-userprefix-live -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DSOUI_BUILD_DESKTOP=ON \
  -DSOUI_BUILD_DESKTOP_EXAMPLE=ON -DSOUI_BUILD_TESTS=ON \
  -DSOUI_DESKTOP_GUI_TESTS=ON -DSOUI_BUILD_DEMOS=OFF -DSOUI_BUILD_TEMPLATE=OFF \
  -DSOUI_DESKTOP_WEBKIT_HELPER_DIR="$PWD/.build-tools/webkit-runtime/libexec/webkit2gtk-4.1"
cmake --build out/desktop-userprefix-live --parallel 2
# 修复后的增量仅构建以下目标：
cmake --build out/desktop-userprefix-live --parallel 2 --target \
  soui_desktop_example soui_desktop_linux_integration soui_desktop_linux_document_lifecycle
# 在受支持桌面会话执行，不是无 display 的 shell：
ctest --test-dir out/desktop-userprefix-live -R '^sdk.desktop.linux-native$' --output-on-failure
out/desktop-userprefix-live/examples/unified-desktop/soui_desktop_example \
  "$PWD/examples/unified-desktop/web" --resource-server
```

本地日志/截图不混入源码或冒充可下载发行物：
`out/webkit-userprefix-build-partitioned-reconnect2.log`（5126/5126，退出 0）、
`out/webkit-userprefix-install.log`、`out/desktop-userprefix-configure.log`、
`out/desktop-userprefix-build.log`、`out/desktop-userprefix-final-build.log`、`out/userprefix-jsc-detail.log`、
`out/userprefix-native-before-fix.log`、`out/userprefix-native.log`、
`out/userprefix-native.exit`（0）、`out/userprefix-example.exit`（0）、
`out/userprefix-bridge-modules-final.png`。

SDK 实测代码为上述基线加三个 C++ 文件补丁（Linux backend、
linux_document_lifecycle、desktop_linux_backend）。验证时仅代码补丁快照
`out/userprefix-validated-sdk.patch` SHA-256：
`36b74bf02bcb50d780ed4548a4cb56c5e3a1e95d44bde543d32aa08f41606f24`。
本记录是后补证据说明，不改变版本、发布范围或全平台验收结论。

## 安装 SDK → 生成项目 → 搬移应用目录（2026-10-22）

后续基线为远端 `9b92d07924d1f1b39ce29da12e436ff46197837a`
（本地快照 `145ad84`）。复用上文同一自构建 WebKitGTK 2.52.6 和 sandbox
配置；未重新构建引擎、安装系统包或关闭安全保护。

从最新 SDK 构建安装到新的 `out/sdk-generated-live-20261022`，仅调用已安装
生成器创建仓库外 `../SouiGeneratedLive20261022`（12 个文件），不修改生成
源码。配置使用安装 SDK，而非直接包含仓库源码：

```sh
source .build-tools/webkit-source/env-userprefix.sh
unset PKG_CONFIG_SYSROOT_DIR
export PKG_CONFIG_LIBDIR="$PWD/.build-tools/webkit-runtime/lib/pkgconfig:$PWD/out/userprefix-sdk-pc"
export LD_LIBRARY_PATH="$PWD/.build-tools/webkit-runtime/lib:$LD_LIBRARY_PATH"
cmake --build out/desktop-userprefix-live --target soui_desktop --parallel 2
cmake --install out/desktop-userprefix-live --prefix "$PWD/out/sdk-generated-live-20261022"
node out/sdk-generated-live-20261022/share/SouiDesktop/tools/create-desktop-app.cjs \
  --output ../SouiGeneratedLive20261022 --name SouiGeneratedLive \
  --identifier org.example.souigeneratedlive
cmake -S ../SouiGeneratedLive20261022 -B ../SouiGeneratedLive20261022/build \
  -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="$PWD/out/sdk-generated-live-20261022"
cmake --build ../SouiGeneratedLive20261022/build --target SouiGeneratedLive_stage --parallel 2
cp -a ../SouiGeneratedLive20261022/build/staged-Release '../Soui Delivery 20261022'
node out/sdk-generated-live-20261022/share/SouiDesktop/tools/verify-desktop-app.cjs \
  --input '../Soui Delivery 20261022'
```

构建/staging 成功，搬移后校验 12 个文件、555815 字节，完整性一致。
在既有云桌面会话中从 `/tmp` 工作目录启动搬移目录中的 `launch.sh`，沿用
上述运行库环境，另设置解包依赖的 GSettings schema 与独立 XDG cache/data
目录。真实窗口已验证：

- 默认资源路径不依赖 cwd，含空格的搬移目录可正常启动；
- 前端适配层 `invoke('app.info', null)` 返回正确名称、标识及版本；
- 静态 ES module → 动态 import → 同源 JSON fetch 返回正确配置；
- 正常关闭窗口后退出码 0，没有应用错误输出。

没有发现新的生产代码缺陷，本轮不制造功能改动，也没有重复全量测试。
独立只读复核确认生成文件与已安装 SDK 的关联及资源路径逻辑。

**限制：同机可搬移应用目录不等于无依赖、离线或跨发行版独立分发。**
此 consumer ELF 的 RUNPATH 包含工作区 `.build-tools/webkit-runtime/lib` 的
绝对位置，且测试显式提供外部依赖环境；WebKit 库、helper 及其传递依赖没有
打入应用目录。doctor 在本机对非系统前缀的 WebKit/JSC/helper 报 UNKNOWN，
未阻止运行，也未被解释成依赖已满足。干净目标机、Windows/macOS、其他架构、
签名与部署许可仍未由本次验收覆盖。

保留的本地证据：`out/generated-live-sdk-build.log`、
`out/generated-live-sdk-install.log`、`out/generated-live-consumer-configure.log`、
`out/generated-live-consumer-stage.log`、`out/generated-live-relocated-verify.log`、
`out/run-generated-staged-live.sh`、`out/generated-live-launch.log`、
`out/generated-live-launch.exit`（0）、`out/generated-live-bridge.png`、
`out/generated-live-modules.png`。生成项目、原 staging 与搬移副本均保留，
截图和二进制不作为源码或可分发运行时提交。
