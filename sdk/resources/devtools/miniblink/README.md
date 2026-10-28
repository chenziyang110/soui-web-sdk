# MiniBlink离线前端源覆盖（候选）

`overrides/front_end/`拥有公共候选使用的前端修复源码。基线为此前已核对的650成员离线快照，manifest SHA256为`2ecbd35cbc51c30efe97717c2fb8713fb032979b47286673cf70d2e6b1dcf7c2`；供应包必须保留全部原许可。此目录包含严格UTF-8/inline map与生命周期修复、协议参数修正及有界元数据错误日志，不依赖实验消费者的原生binding。

`InspectorBackend.js`的自动协议错误日志仅保留固定类别及有限整数错误码，至多64条；协议结果和错误data仍交给请求面板的原回调，不将它们复制到自动诊断日志。显式开启的开发者协议dump不属于默认路径。

原基线中的有限数字暂停观测仍为候选内部实现，不授予native policy/grant，不是公开API或生产观测保证。当前日志修订必须生成新的manifest/技术配对pin，不能覆盖旧2ecb载荷并借用旧测试结论。

覆盖源统一使用LF，与源码SDK打包/安装的文本规范化一致；`source.json`的源文件pin固定这些实际字节，`baseSha256`继续固定原供应基线。通过`tools/internal/devtools-frontend.cjs compose`显式合成新的前端清单，输入包括独立650成员基线、基线manifest及pin、当前源目录与source.json pin、明确内核pin和全新输出目录。输出的receipt只记录技术组合，不验证内核运行或生成法律批准。

内核与前端的实际配对通过`tools/internal/devtools-package.cjs`校验，原生应用编译固定外部选定pin。生成目录不是源码修复入口。当前候选、许可审核及实际面板/源码断点/长测的通过状态仍分别记录。
