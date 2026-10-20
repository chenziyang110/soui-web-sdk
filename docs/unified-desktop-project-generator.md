# 生成统一桌面最小项目

工具仅创建本地源码，不下载 SDK、不执行生成的程序、不安装 Node 包或系统依赖。
需要 Node 18+ 和一个已安装的 SouiDesktop SDK；不要把生成成功视为真实平台验收。
历史 Windows 模板不受影响。

仓库内调用：

```sh
node tools/create-desktop-app.cjs --output ../MyApp --name MyApp --identifier org.example.myapp
```

已安装 SDK 的同一入口：

```sh
node <SDK前缀>/share/SouiDesktop/tools/create-desktop-app.cjs \
  --output MyApp --name MyApp --identifier org.example.myapp --version 0.1.0
```

输出父目录必须存在，目标目录必须不存在。名称为以字母开头的至多64个 ASCII
字母、数字或下划线，拒绝平台设备名、构建保留名和资源目录 web；identifier
必须为显式反向 DNS 标识。版本是应用自身三段数字版本，默认0.1.0，不改变
SDK VERSION 或许可。模板只替换明确 token，不将命令/路径插入源文件。

生成器拒绝 symlink 父路径和模板项、特殊文件、非法名称与大小写碰撞；模板
最多64项、深度8、每文件64 KiB、总计512 KiB。模板必须可信且复制期间不可变，
检查不保证恶意并发修改安全。已有目录绝不覆盖；失败产物保留，由用户检查，
工具不会自动递归删除。代码模板使用系统 WebView，不复制演示业务。

## 生成的内容

- 单窗口 C++17 程序，默认资源按真实可执行文件位置查找，不依赖启动 cwd。
- 仅 `app.info` Bridge allowlist，返回明确应用元数据，不开放任意系统调用。
- 模板显式启用 HTTP 资源服务，SDK 默认仍关闭；前端静态模块、动态 import、
  同源 JSON fetch 和独立样式，无内联脚本、CDN 或外部包依赖。
- 已安装 SDK 的 CMake consumer，前端单独修改也会在下次构建刷新资源。
- 复用 `soui_stage_application` 的应用目录与 doctor 入口，不另建打包体系。

生成目录 README 提供配置、构建、`MyApp_stage` 目标及平台 doctor 用法。
配置时以 `-DCMAKE_PREFIX_PATH=<SDK前缀>` 指定本机 SDK；生成文件本身没有开发机
绝对路径。staging 默认输出必须不存在，重复交付应选择新的 APP_STAGE_DIRECTORY。

## 验证边界

本增量仅检查生成器边界/语法，并用安装 SDK 在仓库外生成、配置、编译一次
Linux x64 consumer 和应用目录。未运行 GUI；Windows/macOS 编译、三引擎模块/
Bridge 交互、外部依赖和正式交付验收仍待完成。详见
[ADR-0128](adr/0128-generated-unified-desktop-project.md) 和
[应用 staging](desktop-application-staging.md)。
