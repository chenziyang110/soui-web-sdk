# ADR-0135：Linux staging 显式采用安装期 RPATH 策略

状态：接受（2026-10-22）

## 背景

staging 复制构建目标，不执行 CMake install/relink。Linux 构建目标会自动携带
开发依赖目录的 RUNPATH，上一轮生成应用实测包含用户工作区 WebKit 路径。
目录可搬移不等于其运行依赖可部署。

## 决定

`soui_stage_application(... USE_INSTALL_RPATH)` 为 Linux 非 imported、非 alias
可执行目标提供显式选择：设置 `BUILD_WITH_INSTALL_RPATH=TRUE`、
`INSTALL_RPATH_USE_LINK_PATH=FALSE`，保留应用自身的 `INSTALL_RPATH`。
不修改二进制、第三方库或全局环境，不自动打包运行时。其他平台明确拒绝此选项。
生成模板的 Linux 分支及启用 staging 的统一示例选择此策略。

这会同时改变原目标的开发启动、IDE、测试和 staging 行为，不只是复制出的文件。
默认空 INSTALL_RPATH 使用目标系统加载器搜索规则；非系统前缀须由开发者提供
外部运行环境。旧调用不加选项则保持原行为。应用可在调用前显式设置部署路径
（例如 `$ORIGIN/runtime`），但必须自行部署其合法依赖，不承诺自包含分发。

全局 `CMAKE_SKIP_RPATH` / `CMAKE_SKIP_INSTALL_RPATH` 仍生效，不覆盖它们。
此策略只控制 CMake 自动添加的路径，不清洗用户 linker flags、toolchain、
绝对 DT_NEEDED、显式绝对 INSTALL_RPATH 或依赖库自己的搜索路径。

## 替代方案

不采用复制后 patchelf 或手工改 ELF；不自动运行整个项目 install（可能触发
无关安装脚本）；不静默修改所有调用方的构建语义。直接 CLI staging 继续原样
复制输入，调用者负责准备部署构建产物。

## 验证

小型实际 ELF 链接夹具比较旧调用、空安装路径及显式 `$ORIGIN/runtime`；验证
自动开发路径消失、显式路径保留且 DT_NEEDED 不变。恢复环境丢失既有自构建
WebKit runtime，本轮不重编引擎，不将轻量链接检查声称为重新 GUI 验收。

依据：[BUILD_WITH_INSTALL_RPATH](https://cmake.org/cmake/help/latest/prop_tgt/BUILD_WITH_INSTALL_RPATH.html)、
[INSTALL_RPATH_USE_LINK_PATH](https://cmake.org/cmake/help/latest/prop_tgt/INSTALL_RPATH_USE_LINK_PATH.html)。
