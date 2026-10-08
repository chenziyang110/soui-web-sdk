# 0.5 本地 typed action 派发：本机双架构证据

本目录记录当前 Windows 11 主机的 Release x64/x86 静态 SDK 与所选原生回归。它验证 C++ 派发边界，不表示 pipe/gateway、Windows 主体认证、GUI/UIA、Win7/PE、8 小时运行、完整 Test/Collect 或正式发布完成，releaseReady=false。

原始目录为 out/dispatch-native-2358a5e1-49f1-420b-bac4-adff8aead6e0。原始索引 ae9b1dd796bb823a649bb9782bf8e879a3e65a7015a592b2992b50e737eda59f 包含 731 项；冻结输入 SHA db9926c773e17d349bbd6d0110b8ee33b3b61eb75ea654ea68d15c47f7e5dde0，68 项实际输入指纹 b48bb7ee7330fb5cda0cd89ab9cd797eeb0b942910cfe3832b524743b739db3c，规范化源码 bd61b7d836dbd75d503681fea382aa5f821deb6653381e2f6aba26a1ca4d14c6，原始源码 63b205edddd7d07f71f09c9f657f6585178d0b7e4c4e2c7ffd0b2f092594889a。8 次 configure/compile/CTest/direct-cases Job 均 root/supervisor exit 0、generic=true、natural completed、closureConfirmed=true、activeProcesses=0，没有套用尾进程例外。每架构实际 CTest 7/7，直接派发 15/15 个命名 case；16 项实际产物由路径/大小/SHA绑定，其中 14 EXE 已核对 COFF，2 项为静态 SDK LIB。实际 SDK19041/v143 14.44.35207 编译路径保留在 cache/vcxproj/compiler/tlog 原件中。

默认所有动作不向本地开放；只有应用明确审核的 typed action 能被原生标记，实际连接的 method 与 capability 均须允许。发现先过滤再分页，保留 canonical bridge.describe 的固定只读路径；内建 plain task.start/query/cancel 不能标记或转换，renew/subscribe 控制不开放。真实 binding 和 scope 对象不能由 wire 字符串替代，gesture=false，业务错误文本不向本地返回。关闭/撤销/过期后丢弃迟到结果不表示 handler 副作用回滚或 I/O 屏障；实际端点仍须 owner executor/generation/发送边界重验和精确资源清理。受控时钟与 TEST_ONLY 固定熵只证明策略和 bearer 复用边界，不证明长期运行或 OS 连接身份。

额外一次真正 Node npm CLI 执行 npm --prefix sdk/web audit --json，显式公共 registry、ignore-scripts、空 user/global npm config、子环境 allowlist 并移除 credential-like 名称，未安装/更新依赖，没有 Node/TypeScript 编译声明。实际审计 root=0、supervisor=0、generic=true、natural=true、vulnerabilities.total=0、auditAccepted=true；完整输入/锁/工具散列、原始输出与独立 Job 在 byte-exact-map 中的 audit 类别。没有把失败审计改写为成功。

byte-exact-map.json 逐项记录原路径、大小、SHA、复制路径或 hash-only 处置；index.json 只索引此公开目录实际文件，不含自身。归档中的完整原 index 仍包含未复制的私有路径，不能将它们误认为公开载荷。所有脚本、源码与生成 CMake/cache/vcxproj/tlog 均为 inert 快照，原字节和换行未改；flat raw 编号防止 Windows 长路径。EXE/DLL/COFF/OBJ/LIB/PDB 等、外部真实 vendor PE 与 MSBuild binlog 正文只留私有原路径/大小/SHA，见 private-artifacts.json；重命名为 inert 也不能绕过二进制 magic 检查。未声称重复扫描 binlog 中的凭据。

原14组测试由归档者编写，Root 与 Deps 已独立复核其设计；本目录归档者结论针对实际执行、输入输出身份和归档字节，不作自我批准。SHA/路径绑定只发现意外不一致，不认证可重写整包的同权限用户。原件不覆盖，canonical acceptance 未写。

首次补充审计在配置解析前失败：夹具将同一个空文件同时指定为 user/global npm config，npm 报 double-loading config。该次 root/supervisor=1、generic=false、closure=true，没有执行依赖审计；失败 raw、输入、context、driver 和首次公开 index/map 均保留，完整首次目录已逐字节移动至私有 first-public-attempt。当前实际审计采用两个不同空配置文件，未覆盖首次失败或原731项。
