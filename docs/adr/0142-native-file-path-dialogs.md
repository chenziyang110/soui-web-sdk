# ADR 0142：统一打开/保存路径选择

## 背景与决定

阶段二最后补齐常用文件选择入口，而不是扩张成任意文件读写框架。新增 C++ `choose_files(open_file_options)` 与 `choose_save_file(save_file_options)`，只返回用户确认的本平台文件系统绝对路径，不返回 URL。文件系统路径仍可能指向网络挂载或 UNC 资源，不保证存储物理位于本机。原有目录接口不变。

打开默认单选，可显式多选最多 32 项；保存只选一个路径。每项最多 32768 UTF-8 字节，合计最多 128 KiB，拒绝 NUL、无效编码和非本平台绝对路径。取消仍为原生 `E_CANCELLED`；窗口关闭为 closed 类错误，不混为取消。

参数只含 title、extensions，以及打开的 multiple 或保存的 default_name。标题最多 4096 字节；扩展名集合最多 16 项，每项 1..16 ASCII 字母数字，规范化小写并去重，空集合不限；不接受通配符、MIME、路径或过滤器组。扩展名是选择 UI 提示，不保证文件内容或安全。默认名只允许最多 255 UTF-8 字节的便携 basename，拒绝路径、控制字符和保留名称；原生面板可能调整后缀，返回最终确认路径。

## 权限与生命周期

网页 `desktop.dialog.openFiles` / `desktop.dialog.saveFile` 各由独立、默认 false 的 `file_open_dialog` / `file_save_dialog` 控制。typed dialogs facade 返回显式 cancelled 联合类型；只映射 E_CANCELLED，其他失败拒绝。未知方法不拦截业务回调。选择路径不扩大 WebView/资源服务根目录，不自动授予读写、持久访问或安全书签权限，也不是被验证的系统手势。

复用窗口所属模态生命周期，持有 owner，关闭取消原生面板；返回后核对窗口，Bridge 复用导航代际屏障，不把旧对话框结果发给新文档。

## 平台与兼容

Windows IFileOpenDialog/IFileSaveDialog 使用本地文件系统路径，保存设置 FOS_NOTESTFILECREATE，不主动做写入探测。GTK 使用现有 chooser OPEN/SAVE，Mac 使用 NSOpenPanel/NSSavePanel。保留保存覆盖确认，GTK/Mac 禁止面板创建目录；SDK 不创建、截断、读取或写入所选文件。用户在系统 UI 中的操作和 OS 自身行为不等于 SDK 文件写入承诺。

不新增任意初始目录、自动打开文件、文件 I/O、下载、Portal/沙箱授权管理；旧 API 默认行为保持。公开结构追加字段要求重新构建，不承诺二进制 ABI。

## 验证与收口

少量 fake/adapter 契约检查输入边界、取消、错误、选择数/总字节、关闭/导航隔离。原生编译仅说明接线可编译，不实际操作用户文件对话框。本纵切后进入阶段三限定集中回归与交付记录，不继续无限追加特性。
