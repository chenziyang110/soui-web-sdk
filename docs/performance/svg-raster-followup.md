# SVG 栅格输入与缓存等价性检查

本轮继续SOUI，未启动Electron、注入鼠标键盘或请求前台切换。实际WebGL使用已经交付的同一缓存版Scratch bundle；没有改动其生产渲染代码或重建上游产物。这里的几何/像素检查不能替代前台FPS、输入延迟或完整项目生命周期验收。

## 已查明的差异位置

最初两次真实检查在“相同SVG造型应重现历史凸包”的强断言失败。第三次保持原断言，只复制已有的`Silhouette._colorData`；主要观测之前没有新增Canvas读回。记录表明：SVG源、尺寸64×48、MIP1、中心、缩放、方向、effects及变换矩阵相同，但实际RGBA不同。

- 历史RGBA SHA256：`cd628bc743c4cfb62a01b2be00727236b981ae3e9a7e3de4694a2393b24bdafb`。
- 最终RGBA SHA256：`ac727546b2f228fdb6c546867fd354aa958e91bfec145a0b91b7e43965558f1a`。
- 变化53个字节，其中49个alpha像素变化，4处跨越零/非零边界。两renderer的最终RGBA逐字节相同。各8次同状态凸包计算稳定；3轮相同SVG重解码稳定为后者。
- 两份最终舞台图相对历史图各变化88字节、44像素，最大通道差21。完整buffer参与比较，未用误差容限掩盖差异。

把实际捕获的两份RGBA交回固定源码中的原始凸包算法、候选未缓存算法和缓存算法，三者分别精确复现8点/9点凸包，以及bubble bottom `5.5`/`8.500000953674316`。同一Skin和同一保留Cache再执行A→B→A，得到8→9→8点；每次revision更新、新凸包引用替换、同revision命中身份和单项容量均正确。此证据把已观察差异定位在凸包算法之前的栅格输入，不能单凭相同SVG URI假设ImageData不变。

这不等于已经查明Canvas为何改变输入，也不代表整个renderer已验收。缓存契约应对当前Skin/revision/尺寸/effects及实际像素输入比较原算法；历史栅格重复性问题独立保留。没有通过删除强断言、加大像素容限或改生产Canvas设置获得通过。

## 独立用例继续执行，整体仍失败

后续夹具只收集已知的那一条历史凸包错误，保留原错误、firstDifference和失败case，然后用固定bitmap重新建立双方状态，继续独立用例。其他错误仍直接失败；最后`passed`取决于失败列表是否为空，原生非零退出规则保持。

实际r4为312项总断言、27个case，`completed=true`而`passed=false`，自然退出2/2，9869ms，9个进程全部退休，无强杀。以下后续用例已运行且没有新增差异：SVG→bitmap重置、pen line/stamp、pen clear、1025个真实skin的LRU（1024项/2048点）、销毁所有drawable/skin后缓存归零。不能把执行完成写成整体验收通过。

实际编译r7为x64 Release、显式LLVM22/MSVC142，6276项所选输入前后不变，真实1个TU/24个头文件及内嵌脚本已核验。EXE为472064B、SHA256 `8f1a6fc9297de5768be292c976226091bf5f149fb36c671c8c41affd58e7813c`。这是独立验证消费者，不能替代默认MSVC143、x86正式应用或SDK运行包身份。

## 尚待验证的归因

Chromium存在有条件的Canvas读回降级逻辑；固定提交的[实现](https://chromium.googlesource.com/chromium/src/+/ec373e32f54110710e96649730bae8b4a706383a/third_party/blink/renderer/modules/canvas/canvas2d/base_rendering_context_2d.cc)和[Canvas资源选择](https://chromium.googlesource.com/chromium/src/+/ec373e32f54110710e96649730bae8b4a706383a/third_party/blink/renderer/core/html/canvas/html_canvas_element.cc)提供后续配对实验线索，但实际Runtime156没有绑定到该源码提交。WebGL可用不能证明小型2D canvas在GPU路径；读回警告也不能证明已发生GPU→CPU切换。本轮未修改`willReadFrequently`、浏览器参数或画质设置，不把该假设写成根因。

完整VM clone、项目reload/undo/redo、context loss/restore、131072点饱和、长期资源趋势、物理输入/上屏和前台性能仍缺相应证据。

## 原始证据

验证负责人保存`renderer-correctness-r1`至`r4`全部原始失败、脚本、资源清单和原生终态；`r3/raster-observation.json`含精确字节/源身份分析，`r3/silhouette-replay-transitions.json`含同一cache的离线重放。`native/scratch-renderer-correctness-x64-r7/actual-read-closure.json`绑定实际编译闭包。便携重放入口及参数见[独立消费者说明](../../experiments/scratch-renderer-correctness/README.md)。这些证据不加入SDK运行包，不改变`releaseReady`。
