# ADR-0136：有界 universal2 应用目录交付

状态：接受（2026-10-23）

SDK 已支持 macOS universal2，应用 staging 原来仅接受 thin Mach-O，交付链不完整。
现在支持恰好包含通用 x86_64 与 arm64 两个 slice 的 big-endian fat32/fat64。
检查表、文件范围、对齐、重叠、CPU/subtype 一致性及每个 slice 的 MH_EXECUTE、
macOS load commands；不接受额外/重复 slice、arm64e 或 x86_64h 专用目标；x86_64允许LIB64 capability位。
识别 swapped-endian magic 后明确拒绝，并要求x64 4KiB/arm64 16KiB页面对齐，
避免仅满足表内align却违反dyld布局。

staging/verification 共用 inspectBinary；架构为 universal2，format 仍为 Mach-O。
最低系统版本按整数取两 slice 实际要求与框架12.0下限的最大值，作为整个 .app
保守下限。SDK CMake 目标属性描述可提供的slice；staging按应用目标的OSX_ARCHITECTURES，
或Runtime验证的consumer架构选择实际交付架构，允许universal2 SDK薄片消费。
显式目标架构只接受x86_64/arm64列表，拒绝未知值/生成器表达式和超出SDK的slice。doctor 仅对
x64/arm64 宿主匹配，不把未知架构视为通过。ZIP不改二进制或签名，保持现有布局。

不泛化为任意 fat 文件，不提取/重写 slice，也不调用 lipo 或下载工具。仍是输入
身份/结构校验，不是完整 Mach-O loader、安全签名或可运行性认证。旧 thin 行为
保持；现有manifest schema/1增加已知架构值，旧版校验器会拒绝新产物，须使用
同版SDK工具。使用合成结构做少量解析和stage→verify→archive验证，不声称在Mac
编译、启动或两个架构实跑通过。

官方依据：[Apple fat.h](https://raw.githubusercontent.com/apple-oss-distributions/cctools/main/include/mach-o/fat.h)、
[CPU subtype定义](https://raw.githubusercontent.com/apple-oss-distributions/xnu/main/osfmk/mach/machine.h)、
[dyld FatFile识别与slice检查](https://raw.githubusercontent.com/apple-oss-distributions/dyld/main/common/MachOFile.cpp)。

消费架构保存于安装SDK imported target的独立属性，不依赖find_package调用结束后
仍可见的普通变量。未设置目标架构仅可使用已验证的薄片消费身份；空值或已清除的目标架构不能继承非空全局架构选择或universal2
声明，须指定slice。源码树默认使用同一工具链
的SDK身份。显式未知架构/生成器表达式和当前构建配置的任何显式
`OSX_ARCHITECTURES_<CONFIG>` 覆盖（包括空字符串）拒绝，配置级变化应使用独立构建目录；
非macOS路径保持原行为。配置fixture不代表Mac编译或运行。

空配置属性并非未设置：CMake 会采用该空值而非通用 slice 列表，可能退回工具链默认架构。
因此在配置期拒绝该覆盖，避免 staging 对实际薄片产物声明 universal2。
依据：[CMake 3.24 GetAppleArchs](https://raw.githubusercontent.com/Kitware/CMake/v3.24.0/Source/cmGeneratorTarget.cxx)。
