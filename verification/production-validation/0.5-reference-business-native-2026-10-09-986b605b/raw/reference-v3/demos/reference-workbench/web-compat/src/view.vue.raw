<script setup lang="ts">
import {computed,onMounted,onBeforeUnmount,ref,watch,nextTick} from 'vue';
import {ElButton} from 'element-plus';
import {createReferenceController} from '../../common/controller';
import {visibleRows} from '../../common/model.mjs';
import {runReferenceParity} from '../../common/parity-scenario';
import '../../common/style.css';
const controller=createReferenceController('compat-vue3-miniblink');
const revision=ref(0),scrollTop=ref(0),scroller=ref<HTMLDivElement>(),dialogClose=ref<HTMLButtonElement>();
const scenario=ref(''),scenarioBusy=ref(false);
const view=computed(()=>{revision.value;return controller.snapshot();});
const state=computed(()=>view.value.state);
const slice=computed(()=>visibleRows(view.value.rows,scrollTop.value,320));
const command=(id:string,action='activate',input:Record<string,unknown>={})=>{
  void controller.perform(id,action,input as never).catch(()=>undefined);
};
const value=(event:Event)=>(event.target as HTMLInputElement).value;
const checked=(event:Event)=>(event.target as HTMLInputElement).checked;
const remove=controller.observe(()=>revision.value++);
onMounted(()=>{void controller.initialize().catch(()=>scenario.value='E_REFERENCE_INITIALIZATION');});
onBeforeUnmount(()=>{remove();controller.shutdown();});
watch(()=>view.value.dialog,async open=>{if(open){await nextTick();dialogClose.value?.focus();}});
async function runScenario(){
  scenarioBusy.value=true;scenario.value='正在执行…';
  try {scenario.value=JSON.stringify(await runReferenceParity(controller,()=>{if(scroller.value){scroller.value.scrollTop=160000;scrollTop.value=160000;}}),null,2);}
  catch {scenario.value='E_PARITY_SCENARIO';}finally{scenarioBusy.value=false;}
}
</script>
<template>
<main :class="['reference',state.theme]">
  <header><div><p class="eyebrow">SOUI · REFERENCE</p><h1>万行资料工作台</h1><p>{{view.profile}} · 同一固定样本、同一 C++ 业务入口</p></div>
    <nav><ElButton id="reference-theme" @click="command('theme')">切换主题</ElButton>
      <ElButton id="reference-focus" @click="command('focus')">聚焦名称</ElButton>
      <ElButton id="reference-reload" @click="command('reload')">保存界面并刷新</ElButton></nav></header>
  <p id="reference-status" role="status" class="status">{{view.message}} <strong v-if="view.error"> · {{view.error}}</strong></p>
  <p class="note">刷新结果：{{view.restoreStatus}}；存储：{{view.storageStatus}}；旧任务：{{view.taskRecovery}}。
    中文输入框是业务示例；本页面不证明实际 IME、键盘或呈现性能。</p>
  <section><h2>真实原生摘要任务</h2><p>原生读取固定原始样本；当前表单草稿不会改变样本。任务运行期间仍可过滤、排序和选择表格。</p>
    <div class="controls"><ElButton id="reference-start" :disabled="!view.controls.start.enabled||view.controls.start.busy" @click="command('start')">开始 5 秒任务</ElButton>
      <ElButton id="reference-query" :disabled="!view.controls.query.enabled||view.controls.query.busy" @click="command('query')">查询真实状态</ElButton>
      <ElButton id="reference-cancel" :disabled="!view.controls.cancel.enabled||view.controls.cancel.busy" @click="command('cancel')">请求取消</ElButton>
      <progress max="100" :value="view.task.progress" aria-label="原生任务进度"/><span>{{view.task.state}} · {{view.task.progress}}%</span></div>
    <p class="task-detail">{{view.task.message}} {{view.task.observationError}} {{view.task.subscriptionError}}</p>
    <pre v-if="view.task.result" class="result">{{JSON.stringify(view.task.result)}}</pre></section>
  <div class="columns"><section><h2>固定 10,000 行</h2>
    <div id="reference-filter" class="controls"><label>过滤名称 <input :value="state.query" maxlength="128" @input="command('filter','set',{query:value($event)})"/></label>
      <label>分类 <select :value="state.category" @change="command('filter','set',{query:state.query,category:value($event)})"><option value="all">全部</option><option v-for="group in controller.fixture.groups" :key="group">{{group}}</option></select></label></div>
    <div id="reference-sort" class="controls"><label>排序 <select :value="state.sort" @change="command('sort','set',{sort:value($event),descending:state.descending})"><option value="id">编号</option><option value="name">名称</option><option value="sizeKb">大小</option></select></label>
      <label><input type="checkbox" :checked="state.descending" @change="command('sort','set',{sort:state.sort,descending:checked($event)})"/>降序</label><span>匹配 {{view.rows.length}}；已选 {{state.selectedIds.length}}/100</span></div>
    <div class="table-heading"><span>选择</span><span>名称</span><span>分类</span><span>KB</span></div>
    <div id="reference-selection" class="table-scroll" ref="scroller" role="region" aria-label="虚拟化资料表" @scroll="scrollTop=($event.target as HTMLDivElement).scrollTop">
      <div :style="{height:view.rows.length*32+'px',position:'relative'}"><div :style="{position:'absolute',top:slice.first*32+'px',left:0,right:0}">
        <div v-for="row in slice.rows" :key="row.id" class="table-row"><input type="checkbox" :aria-label="'选择 '+row.name" :checked="state.selectedIds.includes(row.id)" @change="command('selection','set',{id:row.id,selected:checked($event)})"/>
          <span>{{row.name}}</span><span>{{row.group}}</span><span>{{row.sizeKb}}</span></div></div></div></div>
  </section><section><h2>同一分类分布</h2><svg viewBox="0 0 340 210" role="img" aria-label="固定样本各分类行数">
    <g v-for="(item,index) in view.chart" :key="item.group"><text x="0" :y="24+index*38">{{item.group}}</text><rect x="50" :y="8+index*38" :width="item.count/10" height="24"/><text :x="60+item.count/10" :y="24+index*38">{{item.count}}</text></g></svg>
    <form id="reference-form" @submit.prevent="command('form','submit')"><h2>中文表单</h2><label for="reference-name">名称（64 UTF-8 字节）</label>
      <input id="reference-name" :value="state.draftName" maxlength="64" @input="command('form','set',{name:value($event),notes:state.draftNotes})"/>
      <label for="reference-notes">备注（256 UTF-8 字节）</label><textarea id="reference-notes" :value="state.draftNotes" maxlength="256" @input="command('form','set',{name:state.draftName,notes:value($event)})"/>
      <ElButton native-type="submit" :disabled="!view.ready||view.formBusy">由 C++ 确认</ElButton></form>
    <ElButton id="reference-dialog" @click="command('dialog','open')">查看业务说明</ElButton>
  </section></div>
  <section><h2>同流程自检</h2><p>通过同一公开 GUI 业务入口实际发起任务；这不是键盘、IME、外部 Agent 或性能资格。</p>
    <ElButton :disabled="!view.ready||scenarioBusy" @click="runScenario">运行相同业务流程</ElButton><pre id="reference-scenario-output">{{scenario}}</pre></section>
  <div v-if="view.dialog" class="dialog-backdrop"><div role="dialog" aria-modal="true" aria-labelledby="dialog-title"
    @keydown.esc.prevent="command('dialog','close')" @keydown.tab.prevent="dialogClose?.focus()">
    <h2 id="dialog-title">界面与原生的边界</h2><p>只保存有界界面草稿和偏好。旧文档任务引用不会恢复权限、不会自动重放。</p>
    <p>取消请求不等于取消完成；失败与未知状态会保留。此表单不写文件。</p><button ref="dialogClose" @click="command('dialog','close')">关闭说明</button></div></div>
</main>
</template>
