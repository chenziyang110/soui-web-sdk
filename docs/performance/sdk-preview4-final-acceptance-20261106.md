# SOUI Web SDK preview.4：当前验收报告

**结论：本轮64项原生输入到软件反馈的严格诊断已通过；整个SDK的输入延迟与帧率提升仍未通过验收。** 实现、限定正确性、诊断及体积材料可交开发评审，PR129不能据此宣称性能已达标或生产就绪。最新成功与此前失败分别保留，不将缺失证据改为通过。

## 载荷与本轮范围

产品为 `0.7.0-preview.4`、x64 Release，SDK source SHA `e1d2dd8b7699930f678e21a77772b8fa4e371f5b67a53937d98470ee1659e87c`，build ID `18e47fa2-69df-4530-b1f9-365f6b2aec28`，默认MiniBlink node.dll SHA `56d460c0b4136d85d3fcb8d4e4f00c19a04ff56532228031b55368742ef54308`。匹配的0.6.0基线及不同私有DLL各保留原身份，不能相互继承结果。

R39只修改独立测试工具、页面和读取器，复用原Host、SDK和内核，没有重新打包。新轨迹为64个不重叠cell，仍64次/125Hz、原坐标归一化式、marker flags2/CRC/位图、500ms最终ACK及原监督期限。所有64个标签、完整tuple＋ID、T3/正常T4、getter、DOM首次setter顺序、原生与DOM实际坐标相等、完整性及正常关闭门继续保留。它不是旧相邻像素轨迹，不能用旧数据为它计算收益；旧56/64失败见[覆盖与坐标诊断](sdk-preview4-input-coverage-20261106.md)。

R39源码先经独立审查，两个真实兼容问题在原生运行前修正：首次setter错序的拒绝，以及旧Host排序JSON对象键后对合法geometry的误拒。CPU原件为首轮8/8、随后两项定向2/2及geometry定向1/1，不是一次最终全组10/10。真实v14314.44/SDK19041局部构建64.359秒、退出0、仅两个输入器TU、自有Job自然清空。新诊断EXE为152,064 B、SHA `b9a4a2405527740c91b4ce2cf5ea74deb1d13d0952304270b47776fd0ba16ed3`；诊断工具大小不计为产品包增长。

## R39输入：零基线检查失败

run `cf97342591c64f891053b55800e18c27`，冻结请求SHA `e9cd23a9e97df56533cb07f7517548acf6f5f4d88d169b09a9df7030ea645a98`。Host PID50428/birth`134384260954654572`自然退出2、150.605秒；输入器PID86548/birth`134384261053566174`自然退出1、98ms；入口1、两个Job最终0、无强制结束。原早期异常路径遵守150秒活动边界，没有延长或伪造提前完成。

原生前台与几何检查已经经过，观察器取得本run nonce/CRC有效的初始ACK1。原零位图门以 `Persistent ACK baseline mask must be zero` 正确拒绝，预定64输入循环尚未开始，`burst-input.ndjson`没有创建。SDK/vendor详细trace为18,486/87,186点，完整、loss0、fault为空；完整trace并不能把本轮变成有效延迟样本。

实际因果链已定位：DOM callback2/getter3/inputID3、实际client577/125，置cell35；相同tuple2/1/2/2的SDK T1 `related_id=0`，正常T3→getter→T4严格包围。原生row3同点、extra_info0/tag0。T4 QPC `1805583490992` 早于初始捕获开始 `1805591567531`，证明基线捕获前已有非本轮标记输入改变画面，预定burst尚未发送。不能仅凭tag0认证具体人、物理鼠标或其他发送方，也不能把它算作本轮64项或SDK丢包。

独立实际收据为 `repair-r39-cell-feedback-r1/entry-r1/actual-cell-preflight-review-r1.json`，SHA `1ff498e136a93484515533e0c4246fc4e5022bac748da2dd1112a0e36d3ea92e`。原失败和非零保持。相同工具/页面/读取器的新入口只更换run和新实际显示记录，经事前绑定审查后等待用户明确暂停外部输入，没有自动重试或改变零位图门。

## R39：64项完整关联通过

用户回复“已暂停，现在采样”后，立即执行新run `538fa62cb96eed9d6b9efe12bcae9180`，请求SHA `51146c22677c0641e18a56a0db4a476d236e2c30e82aab57271130afa668b185`。Host PID10412/birth`134384268922010781`、11.017秒；输入器PID95344/birth`134384269020733019`、646ms；Host/Input/入口均自然退出0，自有Job最终0，无强制结束，窗口自动正常关闭。

64次SendInput返回1；64个原生标签、各自T1/T3/正常T4、同步getter及DOM首次setter均完整关联。原生实际XY与对应首次setter DOM实际XY全部严格相等、均落在本项预声明cell，首次setter顺序1..64，最终位图全64。原生71个鼠标回调、DOM70个、getter72次，含6个tag0的DOM回调，全部保留；cold0和真实独立Engine point12返回0。SDK/vendor详细trace分别1,570/8,389点，完整、loss0。原legacy相邻网格mismatch与summary仍为true，没有清除旧失败。

按同tuple＋inputID关联每项原始QPC，先逐项作差，再以nearest-rank统计：

| 阶段 | 样本 | P50 ms | P95 ms | P99/最大 ms |
|---|---:|---:|---:|---:|
| SDK接收T1→引擎开始T3 | 64 | 0.1527 | 0.6111 | 0.7367 |
| 引擎处理T3→正常返回T4 | 64 | 0.1326 | 0.3664 | 1.0287 |
| T1→首次有效正捕获结束上界 | 64 | 19.4572 | 35.0122 | 37.4360 |
| T4→首次有效正捕获结束上界 | 64 | 19.1264 | 34.7630 | 36.0191 |

这是一个轻页、带同步探针和完整trace的短诊断，只有64样本，尾分位仅供探索。36次GDI捕获中，64项首次正位分布在25个捕获样本；不是64个独立呈现帧。捕获begin/end是采集区间，end只提供软件观测时间上界，不是精确首次可见或物理T8。仍无同v5基线/候选性能A/B、测量税与trace-off收益资格，`fullCausalComplete=false` 保持。

请求中心的精准落点仍0/64：实际delta为(0,-1)36项、(-1,-1)24项、(+1,-1)4项；cell资格通过不能关闭旧精准点偏差或其根因。数据支持下一步区分handler返回后的渲染/呈现等待与采集开销，不能将35ms全归因SDK渲染，或据此直接改调度默认值。

独立实际收据 `repair-r39-cell-feedback-r1/entry-r2/actual-cell-full64-review-r1.json` SHA `6113b6b7bdf90e5f7272aa90bf16ce2d4dd88ddb7260d54f96a6bcedd72cc7bc`；Root逐项阶段收据 `entry-r2/run-once/exact64-phase-diagnostic-r1.json` SHA `4622da36e2bb569795d527bb244dd640bd08e75599287a1ba70d2ccf82ca5ee2`，均相对于 `repair-r39-cell-feedback-r1/`。没有为此再编译工具、Host、SDK或内核。

## 已完成的帧对照与资源范围

复用同一冻结产品的R16/R17八轮30秒固定Canvas240角色、同种子/工作量结果；没有为取得有利数字重跑。下表为GDI所采样的**桌面内容变化率下界**，不是精确唯一帧率、物理扫描输出或rAF：

| 后端 | A1基线 | B1候选 | B2候选 | A2基线 | 结论 |
|---|---:|---:|---:|---:|---|
| WebView2，DISPLAY5配置100Hz | 82.699 | 91.111 | 90.130 | 95.012 | 未测出超过冻结噪声的一致收益 |
| MiniBlink，DISPLAY1配置约74.986Hz | 74.607 | 74.693 | 73.073 | 70.863 | 基线漂移超门，收益不确定 |

两后端显示条件不同，不横向比较。MiniBlink的请求帧率及场景步进目标为100Hz，不代表每秒物理显示100帧。配置刷新率不是实测物理扫描率。八轮均有原自然关闭证据；R17机器报告SHA `2551de42f25cec51e91a2b6c6ff526b83215510fc86a1ea5beefbea30c871304`。

现有进程群采样、JS heap和阶段inclusive直方图只可按其观测范围描述，不能相加为完整应用/共享浏览器/GPU归属内存，也不认证整个生命周期CPU。`fullAppCpuQualified=false`、`fullAppMemoryQualified=false`。没有新增ETW/WPR/PresentMon轮次，缺失物理T8继续unknown。

## 开发可接手的结论

| 项目 | 当前判定 | 剩余动作 |
|---|---|---|
| I01 / F01 / W01 | 实现及定向正确性证据已有 | 对最终同载荷证明相应工作减少及无回退；不从实现推断宏观收益 |
| M01 / 输入延迟 | 固定v5轻页64项严格诊断通过；正式性能收益未验收 | 冻结现有夹具；只有可比基线、同负载及测量税资格满足后才计算收益，不从诊断成功推为SDK全场景通过 |
| I02 | 0/0噪声门失败，保持默认0 | 未测2000候选不称无收益或已完成对照 |
| W02 | 短诊断未建立扩大输出准备的动机，仍关闭 | 持续owner压力与公平性仍未认证 |
| I03 | 部分SDK queue/provider诊断已留存，未证明应重排 | 缺同输入前缀/长回调归因，不新增队列或线程 |
| F02 / F03 | 默认opaque复制计数为0；R21风险方案未采用 | 限定不采用不覆盖透明/DC/长期lease等特殊路径 |
| 焦点 / IME | 默认内核blur缺口；私有focus候选上的空composition误删反例已观察 | R31/R33补丁仍私有未采用，按原报告边界修复后复验；不贴给默认SDK4 |
| 交付 / 交互 | 已有匹配构建、消费者、部署、限定键盘/重载/关闭证据 | 完整IME、适用DPI/多屏与其余交互门仍开放 |

焦点/IME原件与补丁边界见[焦点报告](sdk-preview4-focus-20261106.md)及[输入法边界报告](sdk-preview4-ime-boundary-20261106.md)，软件阶段与条件采用见[阶段报告](sdk-preview4-lineage-native-phases-20261105.md)及[采用报告](sdk-preview4-adoption-20261106.md)。本轮没有自动采用私有内核或将它打包为默认候选。

体积沿用同源x64冻结载荷的已执行结果：Host EXE +34,304 B；MiniBlink DLL +7,680 B；MiniBlink完整应用runtime/ZIP +41,984/+19,502 B（约0.090%/0.115%）；WebView2应用runtime/ZIP +34,304/+16,636 B（约0.557%/0.636%）。没有新增DLL，WebView2外置浏览器Runtime不计入，x86缺同口径整应用增量。用户允许小量增长，但没有把原零增长失败重写为通过。详见[交付复验](sdk-preview4-delivery-20261105.md)。

开发可复核[历史R38源码快照](../../verification/performance/sdk-input-frame-20261105/diagnostic-source-r38/README.md)与[本轮v5源码快照](../../verification/performance/sdk-input-frame-20261105/diagnostic-source-r39/README.md)。后者含15个原字节源文件及元数据，共18文件、154,463 B；清单SHA `d0394ac076699be162eded2a9fadd8347f1a17129cb8c47425703d5ce0e5ddb3`。快照制作早于本次原生成功，在新地点未重新构建，不自动授予新二进制资格。

本轮提交前实际执行 `npm --prefix sdk/web audit --json --fetch-timeout=10000 --fetch-retries=0`，退出0、该范围漏洞0；仅覆盖SDK Web依赖，不代表全仓库或内核依赖审计通过。没有修改依赖或执行audit fix。

PR继续针对main、保持draft，不合并或公开发布。当前交付是可复核的部分验收报告与源码，`performanceVerified=false`、`inputLatencyVerified=false`、`releaseReady=false`、`wholeGoalComplete=false`；本轮诊断收口不表示整个性能Goal完成。
