<script setup lang="ts">
import { ref, onBeforeUnmount } from 'vue';
import { KitButton } from './kit-ui';
import type { SouiNativeClientImpl } from '../../../packages/sdk/soui_native_client';
const props = defineProps<{ client?: SouiNativeClientImpl | null }>();
const title = ref('SOUI Web Kit · 原生接口示例');
const text = ref('这段文字通过标准 SDK 写入 Windows 剪贴板。');
const busy = ref(false), result = ref('尚未调用原生接口。');
const reconnectConfirmed = ref(false), reconnecting = ref(false);
const reconnectAvailable = typeof (window as Window & { __souiNativePostMessage?: unknown }).__souiNativePostMessage === 'function';
let disposed = false;
onBeforeUnmount(() => { disposed = true; });
function reconnect() {
  if (disposed || busy.value || reconnecting.value || !reconnectAvailable || !reconnectConfirmed.value) return;
  reconnecting.value = true;
  result.value = '正在重新载入本地页面…';
  try { window.location.reload(); }
  catch (error) { reconnecting.value = false; result.value = `重新连接失败：${String(error)}`; }
}
async function apply(kind: 'title' | 'clipboard') {
  if (!props.client || busy.value) return;
  busy.value = true; result.value = '正在调用 C++…';
  try {
    // Invoke synchronously from the click handler: the host, not this boolean,
    // decides whether a genuine user gesture is in progress.
    const response = await (kind === 'title'
      ? props.client.call('desktop.window.setTitle', { title: title.value })
      : props.client.call('desktop.clipboard.writeText', { text: text.value, userGesture: true }));
    if (!disposed) result.value = response.applied ? (kind === 'title' ? 'C++ 已更新当前窗口标题。' : 'C++ 已写入剪贴板。') : '原生接口未确认操作成功。';
  } catch (error) { if (!disposed) result.value = String(error); }
  finally { if (!disposed) busy.value = false; }
}
</script>
<template>
  <section class="native-actions" aria-label="真实原生接口示例">
    <strong>试用真实 C++ 接口</strong>
    <p>只操作当前 Demo 窗口；复制会替换剪贴板内容，不读取剪贴板。</p>
    <label for="native-title">窗口标题</label>
    <div><input id="native-title" v-model="title" maxlength="120"><KitButton :disabled="!client || busy || !title.trim()" @click="apply('title')">应用标题</KitButton></div>
    <label for="native-copy">复制文字</label>
    <div><input id="native-copy" v-model="text" maxlength="2000"><KitButton :disabled="!client || busy" @click="apply('clipboard')">复制到剪贴板</KitButton></div>
    <p v-if="!client">未连接原生会话，标题与剪贴板按钮不可用。</p>
    <p role="status" aria-live="polite">{{result}}</p>
    <fieldset class="native-reconnect">
      <legend>重新连接原生宿主</legend>
      <p>重新载入本地页面，结束旧扫描并清空页面状态。随后需要重新选择目录，不会自动恢复旧任务。</p>
      <label class="reconnect-confirm"><input v-model="reconnectConfirmed" type="checkbox">我确认结束旧扫描并重新选择目录</label>
      <KitButton data-action="reconnect" :disabled="!reconnectAvailable || !reconnectConfirmed || busy || reconnecting" @click="reconnect">{{reconnecting ? '正在重新连接' : '重新连接'}}</KitButton>
    </fieldset>
  </section>
</template>
<style>
.native-actions { margin-top:16px; padding-top:14px; border-top:1px solid #dce4ef; }
.native-actions label { display:block; margin:10px 0 5px; }
.native-actions > div { display:flex; align-items:center; }
.native-actions input { width:100%; min-width:0; border:1px solid #dce4ef; background:#fff; color:#24344b; }
.native-actions .el-button { margin-left:8px; flex-shrink:0; }
.native-reconnect { margin:16px 0 0; padding:12px; border:1px solid #dce4ef; border-radius:6px; }
.native-reconnect .reconnect-confirm { display:flex; align-items:center; line-height:1.7; }
.native-actions .native-reconnect input { width:auto; flex:0 0 auto; margin:0 8px 0 0; }
.native-actions .native-reconnect .el-button { margin:8px 0 0; }
</style>
