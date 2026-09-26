# preview.11 续期页面分段诊断

**真实 x64 诊断仍因初始 60 秒未就绪退出 1，观察范围已缩小到 DOMException 兼容补丁执行前后。尚未确定补丁内部具体停止点，不宣称 SDK 续期已修复。**

父输入仍为 v2 freeze SHA `80d19d4e2008a505bbb8a9293849f3756d4bf07ad7838ad38aa5bb054ac0ee3a`。先逐文件核验父 x64 的 70 文件运行载荷，在新外部副本中只改 `resources/index.html`，保留全部 EXE/DLL/JS 原字节。原冻结输入在准备前、运行后均校验一致。诊断脚本也纳入准备清单，不修改产品 SDK。

头部观察器显示内联执行、body、兼容补丁前后、polyfill 前后、System.import 返回/完成、DOMContentLoaded、window load 与一秒定时器。最初占用检查拒绝启动；随后桌面空闲，通过两次检查才启动 PID 36752。监督器每五秒检查外部负载，运行期间与结束检查均无其他受监控桌面/QEMU；未发生外层超时，原生退出 1、载荷校验通过。

两秒与四十秒的自有窗口截图均显示 `head-observer`、`body-marker`、`prelude-before` 和 `timer-1000 state=loading`，没有 `prelude-after`、polyfill 或模块导入标记。定时器工作，不能将整个 JS 事件循环描述为停止。19 条原生记录仍是 bootstrap/ready/任务调用为 0，达到原 60 秒期限后 Host/Runtime 完整关闭。截图只抓取与确切 PID/EXE 匹配的窗口，不是整个桌面。

下一份独立诊断增加 DOMException 读取、构造、异常捕获、原型及全局赋值前后的标记。70 文件中仍只改 HTML，准备及语法检查通过；随后其他 QEMU 出现，监督器记录退出 2、没有启动应用。该副本位于 `preview11-renewal-prelude-stages-preparation-20260926`，空闲后运行 `node Run.cjs --run`，不得把准备或 Node 语法检查当作实际浏览器通过。

46 个原始文件由 `raw-manifest.json` 绑定，包含诊断源、父/新载荷散列、启动/退出/占用检查、原生日志及两张截图。运行副本二进制保留在外部目录。观察器和额外 Promise 回调可能影响时序，所有本轮结果仅作故障定位；不是正式 v2 续期验收，更不证明 preview.13。35 分钟、八小时和完整生产门槛仍未通过。
