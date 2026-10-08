# 第三方声明材料交付路径验证

2026-09-25 在 `0.3.0-preview.6` 工作树检查源码 SDK、安装项目、CMake 安装包和应用资源。`releaseReady=false`；以下只证明材料按原始字节复制，不判断固定二进制的完整链接组成或再分发条款。

| 路径 | 实际执行与结果 |
|---|---|
| 源码 SDK | `soui-sdk.cjs pack` 生成 408 文件的预览包，`verify` 和 `verify-source` 通过；`sdk-manifest.json` SHA-256 `E9310C4FB818275260B896049B5894D7EC0573CA4EF508C9A49C810D89028819` |
| 新项目安装 | `soui-sdk.cjs install` 到隔离目录并通过 `verify-project`，保留 `THIRD_PARTY_NOTICES.md` 与 `third_party/licenses/` |
| 双架构构建与安装 | x86/x64 `tools/Build.ps1` 完成 Web、原生构建与载荷身份记录；分别执行 `cmake --install`。两套 `share/SouiSdk/` 都含声明与四份许可文本；后续[独立安装消费者预检](../preview6-independent-consumer-2026-09-25/README.md)分别编译并运行控制台消费者、准备独立桌面应用，未运行桌面场景 |
| 应用资源部署 | Starter 与 Workbench 的 x86/x64 实际应用输出分别含 11 与 415 个资源文件，均含 `resources/legal/`；尚未运行应用 EXE 或桌面 smoke |

四份 `third_party/licenses/` 文件在源码 SDK 包、已安装项目、x86/x64 CMake 安装目录及四个实际应用输出之间共比较 32 份副本，SHA-256 全部与源码一致。原始散列分别为：element-plus `0790118BB4D66681DB1D63181F72EF68E632D632F6DB0373EF87CF328561AF27`、IJG 9a `C791DA525733040E622ED257C7B096B8F5191B332604EAD0C6BC2B54CBD8E0D1`、libcurl 7.61.1 `5F3849EC38DDB927E79F514BF948890C41B8D1407286A49609B8FB1585931095`、OTS 5.0.0 `984523EE987F4E8B72D61DF37D8F1189A7077CD4B77E41A397E35593B297A29D`。`THIRD_PARTY_NOTICES.md` 在源码、两种 CMake 安装及四个应用输出的 SHA-256 均为 `21C9DD51116F3E8B4C189AC220ECC41735B3CA0BED30DC1EFD876B86F08E8288`。

两套 `tools/Test.ps1 -UnitOnly` 在 preview.6 完整构建后各有 9 项交付身份测试、58 项 Web/协议测试和 6 项原生契约测试通过，1 项因缺少调试器跳过。宿主八小时测试和其他 QEMU 占用期间没有运行新的桌面或 Win7 客体场景。

运行包目前只复制上述四份许可文本；[源码声明材料清单](../kernel-notice-inventory-2026-09-25/README.md)中的 V8、Skia、HarfBuzz、libxml、libpng、c-ares、Brotli、zlib、OpenSSL 等材料尚未逐项选定并完整汇入运行包。固定 DLL 的实际链接成员、完整运行载荷、首方许可、维护者审阅和目标系统发布仍是独立门槛。
