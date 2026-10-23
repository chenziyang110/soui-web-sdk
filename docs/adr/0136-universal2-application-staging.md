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
保守下限。SDK CMake 目标既有 universal2 属性直接传播到交付工具。doctor 仅对
x64/arm64 宿主匹配，不把未知架构视为通过。ZIP不改二进制或签名，保持现有布局。

不泛化为任意 fat 文件，不提取/重写 slice，也不调用 lipo 或下载工具。仍是输入
身份/结构校验，不是完整 Mach-O loader、安全签名或可运行性认证。旧 thin 行为
保持；现有manifest schema/1增加已知架构值，旧版校验器会拒绝新产物，须使用
同版SDK工具。使用合成结构做少量解析和stage→verify→archive验证，不声称在Mac
编译、启动或两个架构实跑通过。

官方依据：[Apple fat.h](https://raw.githubusercontent.com/apple-oss-distributions/cctools/main/include/mach-o/fat.h)、
[CPU subtype定义](https://raw.githubusercontent.com/apple-oss-distributions/xnu/main/osfmk/mach/machine.h)、
[dyld FatFile识别与slice检查](https://raw.githubusercontent.com/apple-oss-distributions/dyld/main/common/MachOFile.cpp)。
