# 依赖来源声明与本地资源校验

实际生成项目编译成功后，原未修改的兼容模板启动返回 `E_REMOTE_RESOURCE`。公开宿主扫描 `resources/legal` 内原始 package.json 的 repository.url，并把来源网址当作运行地址。修复将 68 份来源声明改名为 `upstream-package.json.txt`，更新材料索引，全部声明和许可原文的字节与散列保留。

- 两项材料回归原输入失败、修复后通过；采用后的完整交付身份与材料测试 18/18 通过。
- 用实际已安装的 Source22 SDK 链接原生探针，旧材料返回 `E_REMOTE_RESOURCE`，新材料自然退出 0 并到达适配器。远程脚本、JSON 地址、CSS、iframe 及 legal/custom.json 五项负例仍返回原错误。宿主代码和规则未修改。
- 将同一个历史生成项目 EXE、内核及页面复制到独立目录，仅更正材料附件及索引，真实 `--smoke-test` 达到 application-ready、host-close-complete，进程及自有 Job 自然 0/0；原失败目录不变。该对比是包装修复证据，不是最终 0.5.0 包认证。
- 原生探针的前两次私有构建分别因输出目录归属、ComSpec 精确路径校验失败，日志保留。第三次编译成功。负例退出 1 后监督器清理了自有 console 尾进程，不能描述为自然成功。

`RENAMES.json` 记录 68 组原名、新名、字节及 SHA-256；`index.json` 记录归档原件身份。完整应用四配置、当前固定 SDK 及生产验收继续单独执行。来源材料不替代再分发许可审阅。
