# ADR-0139：显式授权的统一桌面服务

状态：代码实现，原生交互验收待完成。复用既有原生接口，不修改旧Host。

options.services 四个独立开关 window_properties、clipboard_read、clipboard_write、
directory_dialog 均默认false；与 enable_window_controls 分离。它们是宿主对可信应用
页面的预授权，不是原生用户手势证明，不自动接受OS权限提示，不信任网页userGesture。
目录返回选择路径不授予文件读写权限。能力不支持或OS拒绝仍返回原生错误。

保留以下固定方法名，其他相同前缀业务方法仍由invoke处理：
- desktop.window.setTitle({title})、resize({width,height})、show({visible})、focus({})。
- desktop.clipboard.readText({})、writeText({text})。
- desktop.dialog.chooseDirectory({})。

字段/类型严格，拒绝额外窗口ID/权限字段；仅当前窗口。标题UTF-8最多4096字节，尺寸
整数64..16384。剪贴板写入最多32KiB UTF-8且仍受64KiB请求限制，读取最多64KiB，
目录路径最多64KiB，拒绝NUL/非法UTF-8，确保JSON转义后仍低于既有1MiB响应限额。
空剪贴板文本有效；选择取消仅精确映射后端E_CANCELLED，不吞掉其他错误。
操作返回{accepted:true}，读返回{text}，目录返回{cancelled,path}。

原生调用期间串行业务回调；模态重入期间保持来源/token/导航generation/关闭屏障。
重载或关闭后迟到结果不能发送到新页面。前端facade复用invoke、不复制Bridge协议；
原示例改用统一服务而非重复业务handler。旧业务若占用固定方法名须迁移，框架没有
方法注册清单，不假装初始化时能检测通用invoke函数内部的所有方法。

这里只做聚焦契约和适配器验证，不在测试读写用户剪贴板或接受真实权限弹窗。
