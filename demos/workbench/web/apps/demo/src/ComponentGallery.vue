<script setup lang="ts">
import { computed, ref, watch, onBeforeUnmount } from 'vue';
import { catalog, Example, ExampleBoundary, loadExampleComponents } from './component-examples';
const props = defineProps<{ active: boolean; architecture: string; experimentalAll?: boolean }>();
const experimentalAllowed = computed(() => import.meta.env.MODE === 'gallery-experiment' && props.experimentalAll);
const literalCardExperiment = import.meta.env.MODE === 'gallery-experiment' && import.meta.env.VITE_LITERAL_CARD === 'true';
// Both pinned architectures passed the 82-example switching sequence with the
// fingerprint-locked Vue CSS cache patch. This is preview access, not a claim
// that every component's input, popup, DPI and accessibility paths are verified.
const canRun = computed(() => props.architecture === 'x86' || props.architecture === 'x64' || experimentalAllowed.value);
const query = ref(''), category = ref('全部'), selected = ref('Button');
const failures = ref<Record<string,string>>({});
const groups = ['全部', ...new Set(catalog.map(item => item.group))];
const filtered = computed(() => catalog.filter(item => (category.value === '全部' || item.group === category.value) && `${item.name} ${item.title}`.toLowerCase().includes(query.value.toLowerCase())));
const item = computed(() => catalog.find(item => item.name === selected.value)!);
const readyName = ref(''), loading = ref(false);
const exampleArea = ref<HTMLElement | null>(null);
let revision = 0;
watch([selected, () => props.active, canRun], async ([name, active, allowed]) => {
  const current = ++revision;
  if(import.meta.env.MODE==='gallery-experiment' && import.meta.env.VITE_TRACE_DOM==='true') {
    (window as Window & {__souiTraceDom?:boolean}).__souiTraceDom=name==='Row';
  }
  if (experimentalAllowed.value && import.meta.env.VITE_SCROLL_CLEANUP === 'true' && exampleArea.value) {
    for (const element of Array.from(exampleArea.value.querySelectorAll<HTMLElement>('*'))) {
      if (!(element instanceof HTMLElement)) continue;
      const style = getComputedStyle(element);
      if (/(auto|scroll|overlay)/.test(`${style.overflow} ${style.overflowX} ${style.overflowY}`)) {
        element.style.overflow = 'visible';
      }
    }
    exampleArea.value.getBoundingClientRect();
  }
  readyName.value = ''; loading.value = false;
  if (!active || !allowed) return;
  loading.value = true;
  delete failures.value[name];
  try {
    await loadExampleComponents(name);
    if (current === revision) readyName.value = name;
  } catch (error) {
    if (current === revision) failures.value[name] = String(error);
  } finally { if (current === revision) loading.value = false; }
}, {immediate:true});
onBeforeUnmount(() => { revision++; });
function select(name: string) { selected.value = name; }
</script>
<template>
  <section id="gallery-top" class="component-gallery" :class="{'literal-card-experiment':literalCardExperiment}">
    <header><p class="eyebrow">ELEMENT PLUS / 2.11.1</p><h1>组件展示台</h1><p>共 {{ catalog.length }} 个组件族示例 · 子组件随所属组件展示 · 一次挂载一个示例</p></header>
    <p v-if="experimentalAllowed" role="status">兼容性实验构建 · 实际架构 {{architecture}} · 未验证组件可能导致进程退出，不用于正式交付。</p>
    <div class="gallery-toolbar"><input v-model="query" aria-label="搜索组件" placeholder="搜索组件名称 / Button / 表格"><button v-for="group in groups" :key="group" :class="{active:category===group}" @click="category=group">{{group}}</button></div>
    <div class="gallery-layout">
      <nav class="gallery-list" aria-label="组件列表"><button v-for="entry in filtered" :key="entry.name" :class="{active:selected===entry.name}" @click="select(entry.name)"><span>{{entry.title}}</span><small>{{entry.name}}</small><b v-if="failures[entry.name]">失败</b></button><p v-if="!filtered.length">没有匹配组件。</p></nav>
      <div class="gallery-detail"><div class="gallery-title"><h2>{{item.title}} <small>{{item.name}}</small></h2><span>本地交互预览</span></div>
        <p class="gallery-hint">请直接操作组件。能显示不等于全部兼容；日期、浮层、键盘、DPI 等仍需分别验证。</p>
        <p v-if="!canRun" class="gallery-error">正在确认原生宿主架构。连接 x86 或 x64 Demo 后开放全部组件预览。</p>
        <div ref="exampleArea" class="gallery-example" :aria-busy="loading"><p v-if="loading" role="status">正在加载本地组件 {{selected}}…</p><p v-else-if="canRun && failures[selected]" class="gallery-error" role="alert">{{failures[selected]}}</p><ExampleBoundary v-if="active && canRun && readyName === selected" :key="selected" @failure="failures[selected]=$event"><Example :name="selected" /></ExampleBoundary></div>
        <p class="gallery-scope">上传仅选文件，不上传；确认框、通知和示例数据不调用 C++。真实系统能力请进入“原生目录扫描”。</p>
      </div>
    </div>
  </section>
</template>
<style>
.example-collapse.el-collapse-transition-enter-active,
.example-collapse.el-collapse-transition-leave-active {
  transition: max-height 280ms ease-in-out, padding-top 280ms ease-in-out, padding-bottom 280ms ease-in-out;
}
.literal-card-experiment .el-card { box-shadow:0 0 12px rgba(0,0,0,.12) !important; border:1px solid #e4e7ed !important; border-radius:4px !important; background:#fff !important; color:#303133 !important; transition:none !important; }
.literal-card-experiment .el-card__body { padding:20px !important; }
.literal-card-experiment .el-card__header { padding:18px 20px !important; border-bottom:1px solid #e4e7ed !important; }
.gallery-toolbar input { border:1px solid #dfe4eb; border-radius:6px; background:#fff; color:#24344b; -webkit-appearance:none; appearance:none; }
.component-gallery { flex:1; min-width:0; padding:30px 32px; color:#24344b; }.component-gallery header>p:last-child { color:#7a8597; font-size:12px; }.gallery-toolbar { display:flex; align-items:center; margin:22px 0 16px; }.gallery-toolbar input { margin-right:12px; }.gallery-toolbar button,.gallery-list button { border:1px solid #dde5ef; background:white; color:#52637a; border-radius:5px; cursor:pointer; }.gallery-toolbar button { padding:8px 10px; margin-right:6px; white-space:nowrap; }.gallery-toolbar .active,.gallery-list .active { background:#eaf1ff; border-color:#96b5f0; color:#2f6fed; }.gallery-layout { display:flex; align-items:flex-start; }.gallery-list { flex:0 0 180px; max-height:calc(100vh - 245px); overflow:auto; padding-right:8px; }.gallery-list button { display:block; width:100%; text-align:left; padding:9px 10px; margin-bottom:6px; }.gallery-list small { display:block; margin-top:3px; font-size:10px; }.gallery-list b { color:#be4355; font-size:10px; }.gallery-detail { flex:1; min-width:0; margin-left:18px; border:1px solid #dfe5ed; border-radius:8px; padding:22px; background:white; }.gallery-title { display:flex; justify-content:space-between; align-items:center; }.gallery-title small,.gallery-title span { font-size:11px; color:#7e8b9e; }.gallery-hint,.gallery-scope { color:#7a8597; font-size:11px; line-height:1.7; }.gallery-example { min-height:300px; padding:22px 0; overflow:auto; }.gallery-error { color:#b53447; background:#fff0f2; padding:16px; }.gallery-error pre { white-space:pre-wrap; overflow-wrap:break-word; }.example-slide { display:flex; align-items:center; justify-content:center; height:180px; background:#eaf1ff; color:#2f6fed; font-size:26px; }
@media(max-width:1050px) { .component-gallery { padding:24px 18px; }.gallery-list { flex-basis:140px; }.gallery-toolbar { flex-wrap:wrap; }.gallery-toolbar input { flex-basis:100%; margin:0 0 10px; }.gallery-detail { padding:14px; margin-left:10px; } }
</style>
