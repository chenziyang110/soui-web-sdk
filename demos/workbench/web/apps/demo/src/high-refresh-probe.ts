import type { SouiNativeClientImpl } from '../../../packages/sdk/soui_native_client';

export async function measureHighRefresh(client: SouiNativeClientImpl) {
  const element = document.querySelector<HTMLElement>('.action-card');
  if (!element) throw new Error('Benchmark action card missing');
  const mark = (window as Window & {__souiBenchmarkMarkPhase?: (phase:string)=>number}).__souiBenchmarkMarkPhase;
  if (!mark) throw new Error('Native phase clock missing');
  const initial = await client.call('desktop.rendering.getStats', {});
  const display = Number(initial.displayRefreshHz);
  if (!Number.isInteger(display) || display < 30 || display > 1000)
    throw new Error('Current display mode is unavailable');
  const candidates = [60, 75, 100, 120, 144, 165, 240, display];
  const rates = candidates.filter((rate, index) => rate <= display && candidates.indexOf(rate) === index).sort((a, b) => a - b);
  const readyTarget = rates[rates.length - 1];
  const sweep = import.meta.env.VITE_HIGH_REFRESH_SCENE === 'sweep';
  const cssDriver = import.meta.env.VITE_HIGH_REFRESH_DRIVER === 'css';
  if (cssDriver && !sweep) throw new Error('CSS comparison requires the same triangle sweep scene');
  const banner = document.createElement('div');
  banner.textContent = `高刷测量：请点击本窗口标题区域，并保持前台约 ${Math.ceil(rates.length * 14.4)} 秒。`;
  banner.style.cssText = 'position:fixed;left:240px;top:0;right:0;padding:12px;background:#fff1c2;color:#624800;z-index:9999;pointer-events:none';
  document.body.appendChild(banner);
  const results: unknown[] = [];
  const animationStyle = document.createElement('style');
  if (cssDriver) {
    animationStyle.textContent = '@keyframes soui-benchmark-sweep {0%,100% {transform:translateX(-40px)} 50% {transform:translateX(40px)}}';
    document.head.appendChild(animationStyle);
    element.style.animation = 'soui-benchmark-sweep 500ms linear infinite';
  }
  let animation = 0, alive = true;
  let raw = 0, duplicates = 0, previous = 0;
  const intervals: number[] = [];
  function move(time: number) {
    raw++;
    if (previous && time > previous) intervals.push(time-previous);
    else if (previous) duplicates++;
    previous = time;
    const phase = (performance.now()/250)%2;
    const x = sweep ? (phase < 1 ? -40+80*phase : 120-80*phase) : Math.sin(performance.now()/350)*12;
    if (!cssDriver) element!.style.transform = `translateX(${x}px)`;
    if (alive) animation = requestAnimationFrame(move);
  }
  animation = requestAnimationFrame(move);
  try {
    await client.call('desktop.rendering.setTargetRate',{targetHz:readyTarget});
    const waitStart = performance.now();
    while ((await client.call('desktop.rendering.getStats',{})).activePacingHz !== readyTarget) {
      if (performance.now()-waitStart > 90000) throw new Error('Foreground focus was not acquired within 90 seconds');
      await new Promise(resolve=>setTimeout(resolve,250));
    }
    // Reversed second cycle reduces a simple warm-up/order bias.
    for (const target of rates.concat(rates.slice().reverse())) {
      banner.textContent = `正在测量调度目标 ${target}Hz；请保持前台，完成后自动关闭。`;
      await client.call('desktop.rendering.setTargetRate',{targetHz:target});
      await new Promise(resolve=>setTimeout(resolve,1200));
      const first = await client.call('desktop.rendering.getStats',{});
      if (first.activePacingHz !== target) throw new Error(`Foreground pacing inactive: requested ${target}, active ${first.activePacingHz}`);
      raw=0; duplicates=0; previous=0; intervals.length=0;
      const startQpc = mark('animation');
      await new Promise(resolve=>setTimeout(resolve,6000));
      const endQpc = mark('idle');
      const last = await client.call('desktop.rendering.getStats',{});
      if (last.activePacingHz !== target) throw new Error('Foreground pacing changed during measurement');
      results.push({target,startQpc,endQpc,first,last,rawCallbacks:raw,duplicateTimestamps:duplicates,intervals:intervals.slice()});
    }
  } finally {
    alive=false; cancelAnimationFrame(animation); element.style.transform='';
    banner.remove();
    if (cssDriver) { element.style.animation = ''; animationStyle.remove(); }
    await client.call('desktop.rendering.setTargetRate',{targetHz:initial.requestedHz});
  }
  return {driver:cssDriver?'css':'raf',scene:sweep?'production action card, triangle +/-40px at 320px/s; 6s per window':'same production action card translated +/-12px by rAF; 6s per window',results};
}
