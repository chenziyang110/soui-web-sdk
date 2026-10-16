# 0056：参考应用保留可重试的 Host 关闭状态

状态：已采用；修复后的真实应用验证待执行。本记录只修改参考应用，不修改 SDK 契约。

## 背景

三应用入口的首次 x64 React smoke 已真实创建正常 SDK/页面，并到达
`application-ready` 和 `gui-native-document-bound`。关闭时公共 Host 返回
两次 `E_CURSOR_CLOSE_PENDING`；应用继续保留对象并重试，随后真实完成 Host
销毁、HWND 撤回、Bridge join 与 runtime shutdown。然而应用把该等待状态
写为 `exit_code=3`，所以自然退出仍为 root3/supervisor3，不能记为通过。
原始失败及编译 0/124 partial 记录保持不变。

公共 `dynamic_miniblink_adapter` 的生命周期说明要求该状态下保留适配器和
两个 HWND，直到活动原生调用返回并完成 unmount。它不是关闭完成证明。

## 决定

只在参考应用 `close()` 的原失败分支中，把 `E_BUSY` 与精确的
`E_CURSOR_CLOSE_PENDING` 视为可重试状态。`E_BUSY` 保持原来的不记录行为；
`E_CURSOR_CLOSE_PENDING` 仍按原路径记录原始错误码，但本次不将原本的零退出
码变为 3。所有其他错误继续先设置 3 再记录。已有非零退出码绝不清零。

原 20ms owner timer、返回路径、借用对象的保留、实际 `destroy()` 重试、
成功后的 Host/HWND/Bridge/runtime 清理顺序完全不变。等待本身不授权释放，
不表示取消、回滚、自然结束或资源已排空。若等待不能结束，外部 Root Owned
deadline 仍按真实失败处理；不新增应用模式、后台重试服务或 SDK 特例。

## 兼容性和迁移

只有参考应用的退出状态分类改变，没有公共 ABI、Bridge、授权、观察字段或
observer 调用变化。部署须重新构建三个参考应用。旧执行与旧载荷身份保留，
不得将旧 root3/supervisor3 重新解释为成功。

## 验证

候选以原始 main.cpp 整文件 SHA 和单一 ASCII byte span 绑定；该 span 反向
替换必须逐字节恢复原文件。固定判断表覆盖 busy、pending、未知错误及既有
退出码为 3 的情形；这只是纯内存分类核对，不是 C++ 或应用实际执行。
采用后由 Root 重新执行 fresh 正常 SDK/三前端/三真实应用 smoke，绑定每个
实际 PID、创建 FILETIME、完整生命周期观察、载荷与自然 Job 闭合。工具链、
所有系统、长稳、光学、发布和完整应用资格仍未由该局部修复关闭。


## 标准验收消费者的采用补充

标准安装验收程序也必须保留公共 Host 返回的精确可重试状态。仅在
tests/acceptance-app/main.cpp 的 close 步骤中，对 E_BUSY 和
E_CURSOR_CLOSE_PENDING 泵送原 owner 消息与 Host，再重试实际 destroy；
所有尝试共用原 settings_.timeout_ms 期限，期限不重置。实际成功才释放
test_window；未知错误或期限内仍未完成保持原真实失败及析构 fail-stop。
外部步骤/进程监督仍负责同步调用阻塞，不能把重试描述为硬抢占。

本补充采用经双人静态复核的唯一关闭函数差异；正常安装消费者的四模式短
基线、100+10 重载资源、千次循环与真实长期验证尚待执行。参考应用已完成
的有限验证不能替代这些标准消费者结果。
