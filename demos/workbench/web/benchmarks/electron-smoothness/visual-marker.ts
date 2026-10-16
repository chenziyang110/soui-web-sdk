// Measurement-only pixels. This is an observable ACK sentinel, not a claim
// that arbitrary application content or a physical display has completed.
let canvas: HTMLCanvasElement | undefined;
let nonce = ((Date.now() ^ Math.floor(Math.random() * 0xffffffff)) >>> 0) || 1;
let ack = 0, frame = 0, scene = 1, motion = false, pending = false;
function crc(bytes: Uint8Array): number {
  let value = 0xffff;
  for (let n = 0; n < 12; ++n) {
    value ^= bytes[n] << 8;
    for (let b = 0; b < 8; ++b) value = ((value << 1) ^ ((value & 0x8000) ? 0x1021 : 0)) & 0xffff;
  }
  return value;
}
function draw(): void {
  pending = false;
  if (!canvas || window.devicePixelRatio !== 1 || ack >= 0x100000 || frame >= 0xffff) return;
  const context = canvas.getContext('2d');
  if (!context) return;
  const bytes = new Uint8Array(16);
  bytes[0] = 0xa7; bytes[1] = 0x53;
  for (let n = 0; n < 4; ++n) bytes[2+n] = (nonce >>> (n*8)) & 255;
  for (let n = 0; n < 3; ++n) bytes[6+n] = (ack >>> (n*8)) & 255;
  bytes[9] = frame & 255; bytes[10] = frame >>> 8; bytes[11] = scene | (motion ? 8 : 0);
  const sum = crc(bytes); bytes[12] = sum & 255; bytes[13] = sum >>> 8;
  for (let i = 0; i < 128; ++i) {
    context.fillStyle = bytes[i >> 3] & (1 << (i & 7)) ? '#ffffff' : '#000000';
    context.fillRect((i % 16)*4, Math.floor(i/16)*4, 4, 4);
  }
}
function schedule(): void {
  if (!pending) { pending = true; window.requestAnimationFrame(draw); }
}
export function initializeVisualMarker(): () => void {
  if (canvas) throw new Error('Visual marker already initialized');
  canvas = document.createElement('canvas');
  canvas.width = 64; canvas.height = 32;
  canvas.setAttribute('aria-hidden', 'true');
  canvas.style.cssText = 'position:fixed;left:8px;top:8px;width:64px;height:32px;z-index:2147483647;pointer-events:none;opacity:1;transform:none;';
  document.body.appendChild(canvas);
  const received = (event: Event) => {
    if (event.type !== 'resize') {
      let target = event.target instanceof Element ? event.target : null;
      while (target && !target.hasAttribute('data-benchmark-action')) target = target.parentElement;
      if (!target) return;
    }
    ++ack; schedule();
  };
  // One click per mouse down/up pair; wheel and window resize are independent.
  document.addEventListener('click', received);
  document.addEventListener('wheel', received, {passive: true});
  window.addEventListener('resize', received);
  draw();
  return () => {
    document.removeEventListener('click', received);
    document.removeEventListener('wheel', received);
    window.removeEventListener('resize', received);
    canvas?.remove(); canvas = undefined;
  };
}
export function visualScene(id: number): void { scene = id; motion = false; schedule(); }
export function visualMotionFrame(running: boolean): void { motion = running; if (running) ++frame; draw(); }
export function visualMarkerSnapshot() {
  return {schema: 'soui.visual-marker/v1', nonce, ack, frame, scene, motion,
    devicePixelRatio: window.devicePixelRatio, region: [8,8,64,32], scope: 'instrumented visual ACK and rAF marker frames'};
}
