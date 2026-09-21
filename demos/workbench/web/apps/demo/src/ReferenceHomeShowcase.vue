<script setup lang="ts">
import { ref } from 'vue';
import type { SouiNativeClientImpl } from '../../../packages/sdk/soui_native_client';
import { KitButton } from './kit-ui';
import ReferenceIcon from './ReferenceIcon.vue';

const props = defineProps<{
  nativeConnected: boolean;
  nativeClient?: SouiNativeClientImpl | null;
  rendererBackend?: string;
  architecture?: string;
}>();
const emit = defineEmits<{
  (event: 'converter', profile?: { format: string; codec: string }): void;
  (event: 'scan'): void;
  (event: 'gallery'): void;
}>();

const statusNote = ref('准备就绪 · 选择一个工具开始');
const selectedTool = ref('视频转换');
const isGpu = () => (props.rendererBackend || '').indexOf('gpu') === 0;
const utilityIcons: Record<string, string> = {
  录音机: 'recorder', 屏幕录像: 'screen', 播放器: 'player', 图片格式转换: 'image', 音频转换: 'audio', 音频提取: 'extract',
};
const videoIcons: Record<string, string> = {
  视频转GIF: 'gif', m3u8转换: 'm3u8', 视频转Webp: 'webp', 视频截图: 'screenshot', 视频倒放: 'reverse', 视频画面裁剪: 'crop', 添加片头片尾: 'intro', 形状视频: 'shape', 虚拟背景录像: 'virtual',
};
const audioIcons: Record<string, string> = {
  音频剪辑: 'audio', 音频转文字: 'text', 音频分割: 'split', 音频合并: 'merge', 音频降噪: 'noise', 音频变速: 'speed', 音频倒放: 'reverse', 音频压缩: 'compress', 人声伴奏提取: 'voice',
};

type WindowMethod =
  | 'desktop.window.beginDrag'
  | 'desktop.window.minimize'
  | 'desktop.window.toggleMaximize'
  | 'desktop.window.close';

async function invokeWindowAction(action: string, method: WindowMethod) {
  statusNote.value = `${action}操作已发送 · 原生窗口处理中`;
  if (!props.nativeClient) {
    statusNote.value = `${action}操作仅在连接 SOUI 宿主时生效`;
    return;
  }
  try {
    if (method === 'desktop.window.beginDrag') {
      await props.nativeClient.call(method, { userGesture: true });
    } else if (method === 'desktop.window.minimize') {
      await props.nativeClient.call(method, { userGesture: true });
    } else if (method === 'desktop.window.toggleMaximize') {
      const result = await props.nativeClient.call(method, { userGesture: true });
      statusNote.value = result.maximized ? '窗口已最大化' : '窗口已恢复窗口模式';
      return;
    } else {
      await props.nativeClient.call(method, { userGesture: true });
    }
    statusNote.value = `${action}操作已完成`;
  } catch (error) {
    statusNote.value = `${action}操作未执行 · ${error instanceof Error ? error.message : '原生窗口不可用'}`;
  }
}

function beginNativeDrag(event: MouseEvent) {
  if (event.button !== 0) return;
  void invokeWindowAction('拖动窗口', 'desktop.window.beginDrag');
}

const mediaProfiles: Record<string, { format: string; codec: string }> = {
  视频转换: { format: 'MP4', codec: 'H.264' },
  视频剪辑: { format: 'MP4', codec: 'H.264' },
  播放器: { format: 'MP4', codec: 'H.264' },
  图片格式转换: { format: 'WebP', codec: 'WebP' },
  音频转换: { format: 'MP3', codec: 'MP3' },
  音频提取: { format: 'MP3', codec: 'MP3' },
  视频转GIF: { format: 'GIF', codec: 'GIF' },
  视频转Webp: { format: 'WebP', codec: 'WebP' },
  视频截图: { format: 'PNG', codec: 'PNG' },
};

function activateTool(name: string) {
  selectedTool.value = name;
  const profile = mediaProfiles[name];
  if (profile) {
    statusNote.value = `${name}进入 native 工作台 · ${profile.format}/${profile.codec}`;
    emit('converter', profile);
    return;
  }
  statusNote.value = `${name}当前只有入口展示；对应 native 能力尚未接入，不会伪造输出。`;
}

function openConverter(profile?: { format: string; codec: string }) {
  selectedTool.value = '视频转换';
  statusNote.value = profile ? `正在打开 native ${profile.format}/${profile.codec} 工作台` : '正在打开视频转换工作台';
  emit('converter', profile);
}
</script>

<template>
  <main class="reference-home" :class="{ 'reference-home-cpu': !isGpu() }" aria-labelledby="reference-home-title">
    <header class="reference-titlebar">
      <div class="reference-brand" title="拖动窗口" @mousedown.prevent="beginNativeDrag"><span class="reference-logo"><ReferenceIcon name="video" tone="line" :size="20" /></span><span><b>迅捷视频转换器</b><small>极速版 · SOUI + MINIBLINK</small></span></div>
      <span class="reference-promo"><ReferenceIcon name="sparkle" tone="line" :size="13" />领88红包</span>
      <nav class="reference-tabs" aria-label="产品模块"><button class="active" type="button">首页</button><button type="button" @click="openConverter">视频转换</button><button type="button" @click="emit('gallery')">组件能力</button></nav>
      <div class="reference-drag-region" title="拖动窗口" aria-label="拖动窗口" @mousedown.prevent="beginNativeDrag"></div>
      <div class="reference-account"><span><ReferenceIcon name="user" tone="line" :size="14" />注册/登录</span><span><ReferenceIcon name="headset" tone="line" :size="14" />客服中心</span></div>
      <button class="reference-menu" type="button" aria-label="打开组件菜单" title="打开组件菜单" @click="emit('gallery')"><ReferenceIcon name="menu" tone="line" :size="18" /></button>
      <div class="reference-window-actions"><button type="button" aria-label="最小化" @click="invokeWindowAction('最小化', 'desktop.window.minimize')"><ReferenceIcon name="minimize" tone="line" :size="16" /></button><button type="button" aria-label="最大化" @click="invokeWindowAction('最大化', 'desktop.window.toggleMaximize')"><ReferenceIcon name="maximize" tone="line" :size="16" /></button><button type="button" aria-label="关闭" @click="invokeWindowAction('关闭', 'desktop.window.close')"><ReferenceIcon name="close" tone="line" :size="16" /></button></div>
    </header>

    <section class="reference-hero" aria-label="视频工具首页">
      <article class="reference-card card-blue">
        <span class="reference-ribbon">HOT</span><div class="reference-card-copy"><p>VIDEO TOOLS</p><h1 id="reference-home-title">视频转换</h1><span>格式转换、视频提取音频</span><KitButton type="primary" @click="openConverter">立即转换</KitButton></div><div class="reference-card-art art-convert"><svg viewBox="0 0 320 180" aria-hidden="true"><rect x="18" y="22" width="284" height="132" rx="14" fill="#edf8ff" opacity=".96"/><rect x="32" y="36" width="256" height="27" rx="8" fill="#c6e9ff"/><circle cx="48" cy="49" r="7" fill="#43b8f2"/><path d="M45 49h6M48 46v6" stroke="#fff" stroke-width="2.4" stroke-linecap="round"/><rect x="68" y="43" width="55" height="13" rx="6.5" fill="#fff" opacity=".92"/><rect x="129" y="43" width="55" height="13" rx="6.5" fill="#fff" opacity=".74"/><rect x="190" y="43" width="42" height="13" rx="6.5" fill="#fff" opacity=".6"/><rect x="238" y="43" width="38" height="13" rx="6.5" fill="#3fa9ee" opacity=".8"/><rect x="42" y="79" width="92" height="45" rx="8" fill="#fff"/><rect x="146" y="79" width="126" height="45" rx="8" fill="#d7f0ff"/><path d="M56 102l14-12 12 10 12-9 25 19H56z" fill="#77c9f2"/><circle cx="80" cy="91" r="6" fill="#36aeea"/><path d="M164 101h90M164 111h68" stroke="#79bde2" stroke-width="4" stroke-linecap="round"/><path d="M244 135H76" stroke="#85cdef" stroke-width="4" stroke-linecap="round"/></svg><i></i><b>AVI · MOV · MPEG · MP4</b><strong>HD</strong></div>
      </article>
      <article class="reference-card card-purple">
        <span class="reference-ribbon ribbon-new">NEW</span><div class="reference-card-copy"><p>CREATIVE STUDIO</p><h2>文字转视频</h2><span>输入文案生成视频</span><button type="button" class="reference-light-button" @click="activateTool('文字转视频')">立即生成</button></div><div class="reference-card-art art-script"><svg viewBox="0 0 320 180" aria-hidden="true"><rect x="20" y="20" width="280" height="138" rx="16" fill="#eee8ff" opacity=".82"/><rect x="43" y="34" width="218" height="108" rx="12" fill="#a897f3" opacity=".92"/><rect x="57" y="48" width="190" height="10" rx="5" fill="#d9d0ff"/><path d="M73 82l13-13 13 13" stroke="#fff" stroke-width="4" stroke-linecap="round" stroke-linejoin="round"/><circle cx="105" cy="78" r="15" fill="#f9d1a9"/><path d="M87 125c3-24 11-34 24-34s21 10 24 34" fill="#5e74cf"/><path d="M138 111c16-13 28-21 42-24 8-2 15 5 13 13-3 13-17 23-31 28" fill="#5e74cf"/><path d="M185 72l7 13 14-6-7 14 14 7-16 3 1 15-11-11-12 10 4-15-15-4 15-4z" fill="#fff" opacity=".86"/><path d="M58 146h189" stroke="#fff" stroke-width="4" stroke-linecap="round" opacity=".7"/></svg><i></i><b>AI STORYBOARD</b><strong>文案</strong></div>
      </article>
      <article class="reference-card card-cyan">
        <div class="reference-card-copy"><p>EDITING SUITE</p><h2>视频剪辑</h2><span>视频综合剪辑处理</span><button type="button" class="reference-light-button" @click="openConverter">立即剪辑</button></div><div class="reference-card-art art-edit"><svg viewBox="0 0 320 180" aria-hidden="true"><rect x="18" y="22" width="284" height="132" rx="14" fill="#e7fbff" opacity=".88"/><rect x="35" y="37" width="250" height="99" rx="12" fill="#75c7e9"/><path d="M54 116c13-36 24-52 45-52 18 0 28 14 39 30 13-22 25-35 43-35 25 0 42 28 57 57H54z" fill="#e8fbff" opacity=".82"/><circle cx="111" cy="59" r="15" fill="#f6cba7"/><path d="M90 120c4-31 12-47 26-47 20 0 27 19 34 47" fill="#3b7ed4"/><path d="M146 105c14-20 25-31 38-31 12 0 24 12 34 35" fill="#3b7ed4"/><circle cx="224" cy="84" r="17" fill="#fff" opacity=".88"/><path d="M219 76l12 8-12 8z" fill="#2d9ddd"/><path d="M42 145h242" stroke="#fff" stroke-width="4" stroke-linecap="round" opacity=".82"/></svg><i></i><b>TRIM · MERGE · CUT</b><strong>剪</strong></div>
      </article>
      <aside class="reference-tools-card">
        <p class="reference-card-kicker">UTILITY TOOLS</p>
        <div class="reference-tool-list"><button v-for="tool in ['录音机','屏幕录像','播放器','图片格式转换','音频转换','音频提取']" :key="tool" type="button" :class="{ selected: selectedTool === tool }" @click="activateTool(tool)"><span class="reference-tool-icon"><ReferenceIcon :name="utilityIcons[tool]" tone="glossy" :size="40" /></span><strong>{{ tool }}</strong></button></div>
      </aside>
    </section>

    <section class="reference-service-row">
      <article class="reference-service-card"><div class="reference-service-heading"><p>VIDEO PROCESSING</p><h2>视频处理</h2><span>更多工具 →</span></div><div class="reference-service-list"><button v-for="tool in ['视频转GIF','m3u8转换','视频转Webp','视频截图','视频倒放','视频画面裁剪','添加片头片尾','形状视频','虚拟背景录像']" :key="tool" type="button" @click="activateTool(tool)"><span class="reference-service-icon"><ReferenceIcon :name="videoIcons[tool]" tone="line" :size="38" /></span><strong>{{ tool }}</strong></button></div></article>
      <article class="reference-service-card"><div class="reference-service-heading"><p>AUDIO PROCESSING</p><h2>音频处理</h2><span>更多工具 →</span></div><div class="reference-service-list"><button v-for="tool in ['音频剪辑','音频转文字','音频分割','音频合并','音频降噪','音频变速','音频倒放','音频压缩','人声伴奏提取']" :key="tool" type="button" @click="activateTool(tool)"><span class="reference-service-icon"><ReferenceIcon :name="audioIcons[tool]" tone="line" :size="38" /></span><strong>{{ tool }}</strong></button></div></article>
    </section>

    <footer class="reference-statusbar"><div><span class="reference-live"><i></i>{{ nativeConnected ? 'C++ BRIDGE CONNECTED' : 'PREVIEW BRIDGE' }}</span><span>SESSION / LOCAL-OFFLINE</span></div><div><span>渲染 {{ rendererBackend || 'software-skia-gdi' }}</span><span>{{ architecture || 'x86/x64' }}</span><span>Win7+ TARGET</span></div><div><span>{{ statusNote }}</span><b>{{ selectedTool }}</b></div></footer>
  </main>
</template>

<style scoped>
.reference-home { min-width:0; flex:1; min-height:100vh; padding:22px 20px 72px; color:#40556d; background:#edf5ff; overflow:hidden; font-family:"Microsoft YaHei","Microsoft YaHei UI","Segoe UI",Arial,sans-serif; font-weight:500; text-rendering:geometricPrecision; -webkit-font-smoothing:antialiased; -webkit-text-size-adjust:100%; font-synthesis:none; }
.reference-titlebar,.reference-brand,.reference-brand>span:last-child,.reference-tabs,.reference-window-actions,.reference-account,.reference-menu,.reference-hero,.reference-service-row,.reference-service-heading,.reference-tool-list,.reference-tool-list button,.reference-service-list,.reference-service-list button,.reference-statusbar,.reference-statusbar>div { display:flex; align-items:center; }
.reference-titlebar { position:fixed; left:0; right:0; top:0; z-index:40; height:44px; padding:0 16px; color:#fff; background:linear-gradient(90deg,#087af2,#1594f4); box-shadow:0 2px 10px #236db52b; }
.reference-brand { min-width:226px; cursor:move; user-select:none; }.reference-brand>span:last-child { flex-direction:column; align-items:flex-start; }.reference-logo { display:inline-flex; align-items:center; justify-content:center; width:28px; height:28px; margin-right:10px; border:1px solid #fff; border-radius:8px; color:#1778e1; background:#fff; font-size:12px; }.reference-brand b { color:#fff; font-size:14px; font-weight:700; }.reference-brand small { margin-top:3px; color:#cde8ff; font-size:10px; font-weight:500; }.reference-promo { position:absolute; left:50%; display:inline-flex; align-items:center; transform:translateX(-50%); padding:5px 10px; border-radius:999px; color:#fff; background:linear-gradient(90deg,#ff7a4a,#ff3f70); box-shadow:0 3px 10px #d7355b3d; font-size:11px; font-weight:700; white-space:nowrap; }.reference-promo .reference-icon { margin-right:4px; color:#fff; stroke-width:3.1; }.reference-tabs { margin-left:18px; }.reference-tabs button { padding:6px 11px; border:0; color:#d9eeff; background:transparent; cursor:pointer; font-size:11px; font-weight:600; }.reference-tabs button+button { margin-left:5px; }.reference-tabs button.active,.reference-tabs button:hover { color:#fff; }.reference-tabs button.active { border-bottom:2px solid #fff; }.reference-account { margin-left:auto; color:#e4f3ff; font-size:11px; font-weight:600; white-space:nowrap; }.reference-account span { display:inline-flex; align-items:center; }.reference-account .reference-icon { margin-right:4px; color:currentColor; stroke-width:2.5; }.reference-account span+span { margin-left:14px; }.reference-menu { width:27px; height:27px; margin-left:14px; justify-content:center; border:0; border-radius:5px; color:#e8f5ff; background:transparent; cursor:pointer; }.reference-menu:hover { background:#ffffff1e; }.reference-drag-region { align-self:stretch; flex:1; min-width:18px; cursor:move; }.reference-window-actions button { width:30px; height:26px; margin-left:4px; border:1px solid #70b6f4; border-radius:5px; color:#e8f5ff; background:#0877e966; cursor:pointer; font-size:14px; font-weight:600; }.reference-window-actions button:last-child:hover { border-color:#fff; }
.reference-hero { max-width:1420px; margin:0 auto 18px; align-items:stretch; }.reference-card { position:relative; display:flex; flex:1; min-width:220px; min-height:250px; height:auto; align-self:stretch; margin-right:16px; overflow:hidden; border-radius:18px; color:#fff; box-shadow:0 9px 22px #4178ae24; }.reference-card:last-of-type { margin-right:16px; }.card-blue { background:linear-gradient(135deg,#0678ef,#3aabf3); }.card-purple { background:linear-gradient(135deg,#7150f3,#b091ff); }.card-cyan { background:linear-gradient(135deg,#12a2ee,#56d4ef); }.reference-card-copy { position:relative; z-index:2; display:flex; flex-direction:column; align-items:flex-start; padding:28px 18px; }.reference-card-copy p,.reference-card-kicker,.reference-service-heading p { margin:0; color:#d9f2ff; font-size:11px; font-weight:700; letter-spacing:.10em; }.reference-card-copy h1,.reference-card-copy h2 { margin:10px 0 5px; color:#fff; font-size:30px; line-height:1.1; font-weight:700; }.reference-card-copy>span { color:#eaf7ff; font-size:14px; font-weight:600; line-height:1.35; }.reference-card-copy .el-button,.reference-card-copy button { margin-top:20px; }.reference-light-button { padding:8px 15px; border:1px solid #fff; border-radius:999px; color:#fff; background:#ffffff20; cursor:pointer; font-size:13px; font-weight:600; line-height:1.2; }.reference-light-button:hover { background:#fff3; }.reference-ribbon { position:absolute; right:11px; top:0; z-index:3; padding:5px 9px 6px; border-radius:0 0 7px 7px; color:#fff; background:#ffb800; font-size:11px; line-height:1; font-weight:700; }.ribbon-new { background:#72dc22; }.reference-card-art { position:absolute; right:-4px; bottom:0; width:72%; height:59%; overflow:hidden; border-radius:50% 0 0 0; background:#ffffff23; }.reference-card-art::before,.reference-card-art::after { content:""; position:absolute; border-radius:50%; background:#fff5; }.reference-card-art::before { left:18%; top:13%; width:65%; height:65%; }.reference-card-art::after { right:7%; bottom:-22%; width:78%; height:78%; background:#164bdc3b; }.reference-card-art i { position:absolute; left:8%; right:8%; bottom:25%; height:4px; border-radius:2px; background:#fff8; transform:rotate(-11deg); }.reference-card-art b { position:absolute; left:13%; bottom:12%; color:#e9f8ff; font-size:9px; letter-spacing:.08em; }.reference-card-art strong { position:absolute; right:25%; top:26%; color:#fff; font-size:31px; font-weight:700; }.art-script { background:#d7c7ff33; }.art-script i { transform:rotate(9deg); bottom:35%; }.art-script strong { right:31%; top:25%; }.art-edit { background:#2cbbf433; }.art-edit i { transform:rotate(-5deg); bottom:31%; }.art-edit strong { right:28%; top:24%; }
.reference-tools-card { flex:0 0 210px; padding:19px 13px 14px; border:1px solid #d4e5f4; border-radius:18px; background:#fff; box-shadow:0 7px 18px #4178ae14; }.reference-tools-card>.reference-card-kicker { color:#6d91b2; }.reference-tool-list { flex-wrap:wrap; margin-top:10px; }.reference-tool-list button { flex:0 0 50%; flex-direction:column; justify-content:center; min-height:100px; padding:7px 3px; border:0; color:#3c5872; background:transparent; cursor:pointer; font-size:11px; }.reference-tool-list button:hover,.reference-tool-list button.selected { color:#237ddd; background:#f0f8ff; border-radius:9px; }.reference-tool-icon { position:relative; display:inline-flex; align-items:center; justify-content:center; width:44px; height:44px; margin-bottom:5px; border-radius:13px; color:#fff; background:linear-gradient(145deg,#2fc6f4 0%,#3384ef 68%,#5369de 100%); box-shadow:0 5px 10px #3989e633, inset 0 1px 0 #ffffff80; font-size:16px; }.reference-tool-icon::after { content:""; position:absolute; left:6px; right:6px; top:4px; height:10px; border-radius:999px; background:#fff5; pointer-events:none; }.reference-tool-icon :deep(svg) { transform:scale(1.12); }.reference-tool-list button:nth-child(2n) .reference-tool-icon { background:linear-gradient(145deg,#91b6f1 0%,#718fe4 68%,#7474d1 100%); }.reference-tool-list button:nth-child(3n) .reference-tool-icon { background:linear-gradient(145deg,#62d8a0 0%,#35b9dd 68%,#348bdb 100%); }.reference-tool-list strong { font-size:12px; font-weight:600; line-height:1.2; }
.reference-service-row { max-width:1420px; margin:36px auto 0; align-items:stretch; }.reference-service-card { flex:1; min-width:0; margin-right:18px; padding:18px 16px 15px; border:1px solid #d4e5f4; border-radius:16px; background:#fff; box-shadow:0 6px 18px #4178ae10; }.reference-service-card:last-child { margin-right:0; }.reference-service-heading { justify-content:space-between; }.reference-service-heading p { color:#7297b8; font-size:11px; }.reference-service-heading h2 { margin:5px 0 0; color:#3c5872; font-size:20px; font-weight:700; }.reference-service-heading span { margin-left:auto; color:#2581df; font-size:11px; }.reference-service-list { flex-wrap:wrap; margin-top:13px; }.reference-service-list button { flex:0 0 20%; flex-direction:column; justify-content:center; min-height:104px; padding:5px 2px; border:0; color:#304b68; background:transparent; cursor:pointer; font-size:10px; }.reference-service-list button:hover { color:#237ddd; background:#f1f8ff; border-radius:8px; }.reference-service-icon { display:inline-flex; align-items:center; justify-content:center; width:40px; height:38px; margin-bottom:2px; color:#3e5873; font-size:14px; }.reference-service-icon :deep(svg) { color:#3e5873; transform:none; }.reference-service-list button:hover .reference-service-icon,.reference-service-list button:hover .reference-service-icon :deep(svg) { color:#187fe4; }.reference-service-list strong { font-size:13px; font-weight:600; line-height:1.2; white-space:nowrap; }
.reference-statusbar { position:fixed; left:0; right:0; bottom:0; z-index:20; justify-content:space-between; min-height:35px; padding:9px 30px 10px; border-top:1px solid #cbdfee; color:#6c879f; background:#f8fbff; font-size:10px; letter-spacing:.05em; }.reference-statusbar>div { flex:1; min-width:0; }.reference-statusbar>div:nth-child(2) { justify-content:center; }.reference-statusbar>div:last-child { justify-content:flex-end; }.reference-statusbar span+span { margin-left:13px; }.reference-statusbar b { margin-left:9px; color:#2583df; font-weight:700; }.reference-live { color:#1681dd; }.reference-live i { display:inline-block; width:5px; height:5px; margin-right:5px; border-radius:50%; background:#39b187; }
.reference-home-cpu .reference-card-art { opacity:.7; }.reference-home-cpu .reference-card { box-shadow:0 7px 15px #4178ae18; }
.reference-tool-icon :deep(svg),.reference-service-icon :deep(svg) { transform:none; }
/* Miniblink's 1x text rasterizer benefits from a deliberate desktop minimum:
   small 9–10px labels and translucent artwork read as softness even when the
   backing store is correct. Keep the reference proportions while increasing
   contrast and using whole-pixel icon sizes. */
.reference-home { font-weight:500; text-rendering:geometricPrecision; -webkit-font-smoothing:antialiased; }
.reference-brand b { font-size:14px; font-weight:700; }
.reference-brand small { font-size:10px; font-weight:500; }
.reference-account,.reference-tabs button { font-size:11px; font-weight:600; }
.reference-promo { font-size:11px; }
.reference-card-copy p,.reference-card-kicker,.reference-service-heading p { font-size:12px; letter-spacing:.08em; }
.reference-card-copy>span { font-size:14px; font-weight:600; line-height:1.35; }
.reference-light-button { font-size:14px; font-weight:600; }
.reference-ribbon { font-size:11px; }
.reference-card-art { background:#ffffff3d; }
.reference-card-art::before { background:#ffffff7a; }
.reference-card-art::after { background:#164bdc50; }
.reference-card-art i { background:#ffffffe0; }
.reference-card-art b { color:#f7fcff; font-size:11px; font-weight:600; }
.reference-card-art strong { font-size:36px; }
.reference-card-art { border-radius:26px 0 0 0; background:#ffffff18; }
.reference-card-art::before,.reference-card-art::after { display:none; }
.reference-card-art > svg { position:absolute; inset:0; display:block; width:100%; height:100%; }
.reference-card-art > i { display:none; }
.reference-card-art > b { z-index:2; }
.reference-card-art > strong { z-index:2; }
.reference-tool-icon { width:48px; height:48px; }
.reference-tool-list button { font-size:12px; font-weight:600; line-height:1.2; }
.reference-tool-list strong { font-size:12px; font-weight:600; line-height:1.2; }
.reference-service-heading p,.reference-service-heading span { font-size:12px; }
.reference-service-heading h2 { font-size:21px; }
.reference-service-heading h2 { margin-left:8px; white-space:nowrap; }
.reference-service-list button { font-size:11px; font-weight:600; }
.reference-service-icon { width:44px; height:42px; }
.reference-service-list strong { font-size:13px; font-weight:600; }
.reference-statusbar { font-size:11px; font-weight:500; letter-spacing:.03em; }
.reference-home-cpu .reference-card-art { opacity:1; }
.reference-window-actions button { display:inline-flex; align-items:center; justify-content:center; }
.reference-window-actions button:last-child:hover { color:#fff; background:#e6475f; border-color:#ff9aaa; }
/* The supplied product reference uses a pale desktop chrome. Keep the native
   drag/close contract while matching that light surface and its dark, high
   contrast labels. */
.reference-titlebar { color:#284d72; background:#e7f2ff; box-shadow:0 1px 0 #cfe2f3; }
.reference-logo { border-color:#2b8ee7; color:#1f81dc; }
.reference-brand b { color:#294f73; }
.reference-brand small { color:#6e91ad; }
.reference-promo { box-shadow:0 3px 10px #d7355b2b; }
.reference-account { color:#294f73; }
.reference-menu { color:#4c6b86; }
.reference-menu:hover { background:#d8eaff; }
.reference-window-actions button { border-color:#9bb6d0; color:#54718c; background:#e7f2ff; }
.reference-window-actions button:last-child:hover { color:#fff; background:#e6475f; border-color:#ff9aaa; }
/* The old Skia text rasterizer benefits from a tiny outline on the desktop
   labels. It keeps the same font and layout while preventing light glyphs
   from dissolving into the card gradients at 1x. */
.reference-home .reference-brand b,
.reference-home .reference-brand small,
.reference-home .reference-card-copy p,
.reference-home .reference-card-copy h1,
.reference-home .reference-card-copy h2,
.reference-home .reference-card-copy > span,
.reference-home .reference-light-button,
.reference-home .reference-tool-list strong,
.reference-home .reference-service-list strong { -webkit-text-stroke:.14px currentColor; }
.reference-home .reference-tool-list strong,
.reference-home .reference-service-list strong { color:#173e66; }
.reference-card { transition:transform 220ms cubic-bezier(.2,.75,.25,1),box-shadow 220ms ease; }
.reference-card::after { content:""; position:absolute; z-index:1; top:-55%; left:-28%; width:22%; height:210%; pointer-events:none; background:linear-gradient(90deg,transparent,#ffffff22 48%,transparent); transform:rotate(18deg) translateX(-420%); animation:reference-card-sheen 8s cubic-bezier(.35,0,.2,1) infinite; }
.reference-card:hover { transform:translateY(-2px); box-shadow:0 13px 28px #4178ae35; }
.reference-tool-icon { transition:transform 180ms cubic-bezier(.2,.75,.25,1); }
.reference-tool-list button:hover .reference-tool-icon { transform:translateY(-2px); }
@keyframes reference-card-sheen { 0%,58% { transform:rotate(18deg) translateX(-420%); opacity:0; } 64% { opacity:.65; } 76%,100% { transform:rotate(18deg) translateX(840%); opacity:0; } }
.reference-home-cpu .reference-card::after { display:none; animation:none; }
@media (prefers-reduced-motion: reduce) { .reference-card::after,.reference-tool-icon { animation:none; transition:none; } }
@media (min-width:821px) { .reference-home .reference-tabs { display:none; } }
@media (max-width:1120px) { .reference-home { padding:22px 16px 22px; }.reference-card { min-width:190px; min-height:230px; height:auto; }.reference-tools-card { flex-basis:188px; }.reference-tool-list button { min-height:90px; }.reference-service-row { margin-top:44px; }.reference-service-list button { flex-basis:20%; min-height:90px; }.reference-home .reference-statusbar { display:none; } }
@media (max-width:820px) { .reference-home { overflow:auto; }.reference-hero,.reference-service-row { display:block; }.reference-card,.reference-card:last-of-type,.reference-tools-card,.reference-service-card { width:100%; margin:0 0 13px; }.reference-card { height:210px; }.reference-service-card:last-child { margin-top:13px; }.reference-tabs,.reference-account,.reference-promo { display:none; }.reference-statusbar { left:0; padding:9px 13px; }.reference-statusbar>div { display:block; line-height:1.8; }.reference-statusbar>div:nth-child(2),.reference-statusbar>div:last-child { justify-content:flex-start; }.reference-statusbar span+span { margin-left:9px; } }
</style>
