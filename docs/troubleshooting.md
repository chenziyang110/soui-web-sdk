# 故障排查

| 现象 | 处理 |
|---|---|
| 缺 C++ / ATL / mt.exe | 运行 Doctor，在 VS 安装器补齐 C++、ATL 和 Windows SDK 10.0.19041.0 |
| npm ci 失败 | 查看对应 install.log，检查网络、registry 和锁文件 |
| SDK 文件 modified | 恢复 SDK 文件或发布新 SDK 版本；业务改动放 app/web/resources/contracts |
| 输出目录已存在 | New-Project 只创建新目录，选择新的 OutputRoot |
| 页面修改未生效 | 完整 Build，不使用 SkipWeb；确认运行本次输出目录 EXE |
| 缺 DLL | 保留完整运行目录，检查 DLL 与 EXE 架构一致 |
| 白屏或超时 | 查看 smoke.log、Web 日志、HTML 挂载节点与 Chrome 49 转译产物 |
| bridge 被拒绝 | 检查握手、方法注册和当前会话的 token/origin；导航后旧会话失效 |
| 内核编译失败 | 查看 out/dependencies/架构/组件.log 的首次错误；检查空间与 SDK |
| 同版本内容冲突 | 增加 VERSION，生成新包；安装器有意禁止同版本不同内容 |

报告问题请附版本、架构、配置、系统、最小复现、退出码和日志，不附密钥、令牌或用户文件内容。
