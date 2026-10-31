# 透明备用画布及完成任务退休：限定验证

基线提交`c88309fb9dabf643dae6d3f71c6de10098b03b38`。设计见ADR-0150/0151，来源见两个`miniblink-*-20261031.json`专属记录。默认预算仍0，旧高精度SharedTimer、GDI提交完成边界、完整copy和lease保持。

## 修改与实际回归

新建透明备用槽已清屏，字段构造后复制前不重复清屏；有效旧透明槽仍清屏，保留SrcOver复制的像素语义。实际方法片段回归x64先在两次clear指定断言失败，自然exit1/1；候选x86/x64各6组通过，观察清屏/完整copy次数、透明/opaque像素、失效重建、两槽lease和分配失败。依赖为明确的fake Skia；不证明真实GDI/Skia像素或FPS。

显式provider机会在每个完成任务析构前检查原QPC截止，仅入口允许一次最低退休，剩余对象有去重continuation/workRemaining。原代码在过期调用中仍退休95项，指定RED自然exit3/3。最终两架构各44/44通过：过期1项、未来截止第三项后停止、无参32项、无默认QPC开销、ready callback最低进展、析构重入与shutdown全排空。初次retire后的bounded状态采样时点保留，避免析构重入改变willExit后用旧值。

首轮候选43/44的旧huge-deadline oracle失败保留；最终核对普通timer实际arm且句柄存在，或高精度timer实际arm且timer/wait存在，原目标/取消/不提前规则不变。fixture SharedTimer全文件pin绑定当前高精度版本。README中被source-map冻结的准备段落为当时快照，实际执行以本记录、ADR及机器收据为准；不为文档时态重复编译。

92/92 SOURCE门禁通过，8类recipe有57原字节产物，worker有17个原字节/严格派生产物。此为authored graph，不替代编译资格；生成器/输入未知、缺件、重复、篡改或伪冻结仍拒绝。六个画布选定输入、39个worker选定输入各在执行前后相同。

## 真实内核增量

实际CL include跟踪确认两架构各11个TU使用改变的WebThreadImpl头，连同LayerTreeHost为各12个完整受影响集合；不只编译两个修改的CPP。原PCH creator不使用该头，新编译均禁旧PCH并保留实际config.h。4511项source（含12个主CPP）与2912项工具/外部头预先固定；每个实际showIncludes必须命中预先pins，未知即失败。保留c883位图修复对象，其他3021/3019个原链接输入逐pin核验，各12个对象唯一替换，链接参数及x86符号大小写保持。

外部r1仅准备，审查发现actual include尚未逐项匹配预先pins，未执行；r2严格化后执行。两架构各12编译+1重链均自然0/0、owned group闭合，全部源/工具/原对象前后一致。两架构原生阶段合计21.260/21.104秒，含准备与核验的整个执行约62秒。不是公开fresh全内核构建。

| 架构 | DLL bytes | SHA256 | 相对c883 DLL差额 |
|---|---:|---|---:|
| x64 | 39209984 | `a172947c7ddd0b23ec294d7442bc51e22ccfb6e6460265261a4076358ecdbaa5` | +1024B |
| x86 | 28322304 | `4d91c279a1bf535d8799d211c5d8f88c5b4f4adc5cd3da4fa49991d99676024c` | +512B |

这是两项源码改动合计的内核差额，不能拆成各项贡献，也不等于EXE/完整runtime/ZIP差额。历史正式包、prebuilt和用户安装未替换。实际加载/关闭结果单列；完整性能、物理输入/CRC、长期稳定性和最终完整包仍待，`performanceVerified=false`、`releaseReady=false`。

## 实际加载与正常关闭

两架构分别用受支持的完整SHA外部内核部署到全新隔离runtime，复用经837项SDK输入与原编译roster核验未变的loader、owner及SDK库。旧全局source-manifest只作历史编译上下文，其他客户端输入与二进制逐pin相同；没有重新编译客户端或替换用户安装。

每架构一次loader及显式budget1000的实际SDK Host短测，均自然0/0并闭合。新DLL路径、请求与加载文件身份、getter/尾回调所属module及deadline capability核对通过；loader不输出数值Win32 file ID，实际身份比较由冻结源码执行，报告保留这一边界。Host页面就绪、各4次DOM timer→Bridge、Host销毁和final Runtime关闭完成，运行目录前后相同。隐藏自有HWND，无桌面输入，没有直接调用私有provider服务。

实际owner阶段x64 816ms/x86 838ms。它证明短程集成，不证明回调具体执行次数/长时资源趋势、画布真实像素、CRC、输入延迟或FPS。外部结果为`native/source-cleanups-smoke-r1/{x64,x86}-budget1000/result.json`，来源角色与SHA见专属清单。
