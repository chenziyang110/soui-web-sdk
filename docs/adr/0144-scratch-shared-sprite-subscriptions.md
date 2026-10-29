# ADR-0144：共享角色列表的全局状态订阅

状态：设计待实现，须完成下述语义与实际收益验证；未加入现有bundle。

## 背景与证据

可见页面的实际S诊断在约10秒产生607次VM step/draw/targetsUpdate。已识别的`_step`子树1473个包含样本中，targetsUpdate分支780、脚本线程433、绘制253；大量program样本未归因，不能称53%总CPU。证据与限制见[实际诊断](../performance/so-only-diagnostic-followup.md)。

当前每个`containers/sprite-selector-item.jsx`均有connect订阅。它只读取storage、vm、dragging及按id派生的receivedBlocks，不读取targets；targets action仍会经过这些订阅的selector检查。重负载每轮240个角色的坐标/方向/造型均改变，直接共享完整row预计命中低，且costume等对象可以原地编辑。

## 决定

先用一个Redux订阅提供角色列表共同状态，不改VM的23字段、事件、dispatch次数或targets reducer。新增`lib/sprite-selector-store-context.jsx`，提供storage、vm、dragging、hoveredSprite、receivedBlocks及两个dispatch函数。Provider value按这些依赖memoize，无关targets action不能产生新value。每个角色在consumer内按自己的id派生receivedBlocks。

`sprite-selector-item.jsx`保留现有default connected导出，新增named Context wrapper；缺Provider时回退原connected组件。合并props保持原connect的ownProps→stateProps→dispatchProps覆盖顺序。`sprite-list.jsx`只替换现有500ms asset HOC包裹对象，并把Provider放在现有SortableHOC外，不新增DOM层。

缺Provider使用独立sentinel，不能把false的dragging/receivedBlocks当作缺失。按own id保留原`receivedBlocks && hoveredSprite === id`结果，不额外强制Boolean。dispatch回调在无关targets action时稳定，并随实际store替换正确更新。

固定层次为Provider→SortableHOC(SpriteList)→既有asset节流器→Context consumer→既有PureComponent。consumer必须位于节流器内部：全局drag/hover仍可更新角色，但使用节流器最近一次实际render传入的asset。不得把全局props提升后穿过节流器，导致这些动作顺带提前刷新asset。

使用新的注册源码profile，例如`local-hull-cache-gui-shared-v1`。保留stock和local-hull-cache-v1的原始patch集合、三个renderer afterSHA及旧bundle身份；新增GUI替换、新文件、recipe/source成员、SourceVendor采用核验和demo白名单。不把新源码或新包挂到旧构建证据。

## 兼容、边界与备选

保留默认connected使用、无Provider fallback、选择/排序/rename/delete/恢复、SpriteInfo精度和既有500ms规则。沿用现有React、Redux及Scratch/WebView2兼容范围，不新增运行依赖。store/VM/storage替换行为必须与原实现对照，不能凭Context结构推定一致。

首期不缓存旧可变costume引用，不减少通知频率或角色/脚本工作量。后续字段投影与delta必须独立保存primitive值及明确的内容revision；本决策不批准用浅引用比较代替该设计。也不通过降低画质或修改内核预算获得成绩。

## 验证与回退

- 原实现与新实现对照：100ms替换asset、200ms变更drag/hover，仍显示节流器上次实际传入的asset；跨500ms按原规则刷新。非asset ownProps变化保留原立即更新行为。
- 原地替换costume.asset、SVG尺寸的第二次通知、hover/receivedBlocks、drag起止、选择、重排、rename、删除/恢复、VM/storage/store替换均保持输出与操作一致。
- 240项连续targets action下，事件/dispatch数量不变，DOM/leaf输出相同；无关状态下Provider value引用不变。测量实际selector/嵌套订阅调用，不能只数最外层store.subscribe，因为既有订阅可能挂在父connect下面。
- unmount释放订阅，无Provider默认用法仍工作。不要以mock订阅器的数字替代实际React/Redux通知行为。
- 完整新profile源码构建、对应源码/recipe闭包及bundle身份核验，再执行同逻辑工作量采样。没有实际前后收益前保持设计/候选状态，不更新性能资格。

回退使用原profile和匹配bundle，或撤销这三处GUI接线并对应重建；不撤销已有hull失效、真实计量、权限或关闭屏障。
