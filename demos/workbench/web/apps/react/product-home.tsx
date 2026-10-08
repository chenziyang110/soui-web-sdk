import React, { useState } from 'react';

export type ProductPage = 'home' | 'converter' | 'video4k' | 'components' | 'motion' | 'data' | 'cad' | 'compat' | 'fileinsight';
type Props = { openMedia: (title: string, format: string, quality: number, operation?: string) => void; navigate: (page: ProductPage) => void; notify: (text: string) => void };
type Tool = { label: string; icon: string; tone?: string; page?: ProductPage };

// Small, local vector assets: no icon-font loading or enlarged screenshot sprites.
const paths: Record<string, React.ReactNode> = {
  video: <><rect x="3" y="5" width="13" height="14" rx="3" /><path d="m16 10 5-3v10l-5-3M7 8v1m0 3v1m0 3v1" /></>,
  compress: <><path d="M5 4h14M5 20h14M8 7l4 4 4-4M8 17l4-4 4 4" /></>,
  merge: <><path d="M4 5h5l6 7h5M4 19h5l6-7M17 9l3 3-3 3" /></>,
  split: <><path d="M4 12h5l6-7h5M9 12l6 7h5M17 2l3 3-3 3M17 16l3 3-3 3" /></>,
  text: <><path d="M4 5h14M11 5v14M8 19h6M4 5v3M18 5v3" /><path d="M17 13h4m-2-2v8" /></>,
  ratio: <><path d="M9 4H4v5M15 4h5v5M4 15v5h5M20 15v5h-5M8 12h8M12 8v8" /></>,
  music: <><path d="M10 17V6l10-2v11M10 9l10-2" /><ellipse cx="7" cy="18" rx="3" ry="2" /><ellipse cx="17" cy="16" rx="3" ry="2" /></>,
  drop: <path d="M12 3C10 7 5 11 5 15a7 7 0 0 0 14 0c0-4-5-8-7-12ZM9 15c0 2 1 3 3 3" />,
  subtitle: <><rect x="3" y="4" width="18" height="16" rx="3" /><path d="M6 15h4m4 0h4M7 11l2-4 2 4m-3-1h2M14 7h4m-2 0v4" /></>,
  record: <><rect x="3" y="6" width="18" height="14" rx="4" /><circle cx="8" cy="13" r="2" /><path d="M14 11h4m-4 4h3M7 3l7 1" /></>,
  screen: <><rect x="3" y="4" width="18" height="13" rx="3" /><path d="M8 21h8m-4-4v4" /><path d="m10 8 5 3-5 3Z" /></>,
  play: <><circle cx="12" cy="12" r="9" /><path d="m10 8 6 4-6 4Z" /></>,
  image: <><rect x="3" y="3" width="18" height="18" rx="3" /><circle cx="8" cy="8" r="1" /><path d="m4 17 5-5 4 3 3-4 5 5" /></>,
  audio: <path d="M4 10v4M8 6v12M12 3v18M16 7v10M20 10v4" />,
  extract: <><path d="M5 17v4h14v-4M12 16V3M7 8l5-5 5 5" /></>,
  gif: <><rect x="3" y="3" width="13" height="16" rx="2" /><rect x="8" y="7" width="13" height="15" rx="2" /><path d="M17 11h-4v6h4v-3h-2" /></>,
  loop: <><path d="M4 10a8 8 0 0 1 14-5l2 3M20 4v4h-4M20 14a8 8 0 0 1-14 5l-2-3M4 20v-4h4" /></>,
  shot: <><path d="M8 3H3v5M16 3h5v5M3 16v5h5M21 16v5h-5" /><path d="m10 8 6 4-6 4Z" /></>,
  reverse: <><circle cx="12" cy="12" r="9" /><path d="M15 7v10M11 7l-5 5 5 5Z" /></>,
  crop: <><path d="M7 3v14h14M3 7h14v14" /><path d="M2 14l4-1m5-1 4-1m5-1 2-1" stroke="#1684ff" /></>,
  add: <><rect x="3" y="3" width="18" height="18" rx="2" /><path d="M3 8h18M8 3v5M16 3v5M8 14h8M12 10v8" /></>,
  shape: <><rect x="3" y="3" width="13" height="14" rx="2" /><circle cx="16" cy="16" r="6" stroke="#1684ff" /></>,
  background: <><rect x="3" y="3" width="18" height="18" rx="2" /><path d="m7 7 10 10M7 13l4 4m2-10 4 4" stroke="#1684ff" /></>,
  noise: <><rect x="3" y="3" width="18" height="18" rx="2" /><path d="m7 8 5 7h5m0-4v4h-4" stroke="#1684ff" /></>,
  speed: <><path d="M5 19a9 9 0 1 1 14 0M12 12l4-5" /><path d="M5 8h1m5-5v1m7 4h1" stroke="#1684ff" /></>,
  voice: <><rect x="8" y="3" width="8" height="12" rx="4" /><path d="M5 11a7 7 0 0 0 14 0M12 18v3M9 21h6" /></>,
  arrow: <path d="M4 12h15m-6-6 6 6-6 6" />,
  check: <path d="m5 12 4 4L19 6" />,
  insight: <><path d="M4 5.5A2.5 2.5 0 0 1 6.5 3H20v16H6.5A2.5 2.5 0 0 0 4 21.5z" /><path d="M4 5.5v16M8 7h8M8 11h8M8 15h5" /></>,
};
export function ProductIcon({ name, className = '' }: { name: string; className?: string }) {
  return <svg className={`pd-icon ${className}`} viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1.65" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">{paths[name] || paths.video}</svg>;
}
function Jewel({ tool }: { tool: Tool }) {
  return <span className={`pd-jewel ${tool.tone || 'blue'}`}><span className="pd-jewel-face"><ProductIcon name={tool.icon} /></span><span className="pd-jewel-wing" /></span>;
}
function Scene({ id }: { id: string }) {
  return <svg viewBox="0 0 260 150" className="pd-scene" aria-hidden="true"><defs><linearGradient id={`${id}-sky`} x2="0" y2="1"><stop stopColor="#b7edff" /><stop offset="1" stopColor="#68beec" /></linearGradient><linearGradient id={`${id}-sea`} x2="0" y2="1"><stop stopColor="#06b0db" /><stop offset="1" stopColor="#1978cd" /></linearGradient></defs><rect width="260" height="150" fill={`url(#${id}-sky)`} /><circle cx="192" cy="35" r="16" fill="#fff5d6" /><path d="M0 79 44 32 83 76 120 49 174 108H0" fill="#8abcc2" /><path d="m0 109 75-52 50 32 50-51 85 72" fill="#47979d" /><path d="m58 72 17-15 14 10-8-1-6-5-7 8Z" fill="#e4faf6" /><path d="M0 98c80-12 126 16 260 0v52H0Z" fill={`url(#${id}-sea)`} /><path d="M0 125c55-24 87-8 119 25H0Z" fill="#155b67" /><path d="M0 126c55-24 87-8 119 24h-12c-29-21-62-35-107-18Z" fill="#ffdfac" /><path d="m187 112 18 0-5 4h-10Z" fill="white" /><path d="M196 111V93l-9 16h9" fill="#f0fcff" /><path d="M155 128h34m15-7h20m-92-6h21" stroke="white" strokeOpacity=".38" strokeWidth="1" /></svg>;
}
function HeroArtwork({ kind }: { kind: 'convert' | 'create' | 'edit' }) {
  if (kind === 'convert') return <div className="pd-art pd-art-convert" aria-hidden="true"><div className="pd-file-back"><span>VIDEO</span><i /><i /><i /></div><div className="pd-file-front"><div className="pd-mini-chrome"><i /><i /><i /><b>我的视频.mp4</b></div><Scene id="convert" /><div className="pd-film-footer"><span>1920 × 1080</span><b>HD</b></div></div><div className="pd-format-strip"><span>AVI</span><span>MOV</span><span>MPEG</span><b>MP4</b></div><span className="pd-art-round"><ProductIcon name="loop" /></span></div>;
  if (kind === 'create') return <div className="pd-art pd-art-create" aria-hidden="true"><div className="pd-story-note"><span>✦ 灵感，变成画面</span><i /><i /></div><div className="pd-story-frame"><Scene id="create" /><span className="pd-art-play"><ProductIcon name="play" /></span></div><span className="pd-spark spark-one">✦</span><span className="pd-spark spark-two">✧</span><div className="pd-caption-pill">写下故事 · 即刻出片</div></div>;
  return <div className="pd-art pd-art-edit" aria-hidden="true"><div className="pd-editor-window"><div className="pd-mini-chrome"><i /><i /><i /><b>创作工作台</b></div><Scene id="edit" /><div className="pd-timeline"><i /><i /><i /><i /><i /><b /></div></div><div className="pd-trim-badge"><ProductIcon name="crop" /></div><div className="pd-editor-pill">00:12 <span>━━━━</span> 00:36</div></div>;
}

const features: { title: string; subtitle: string; action: string; kind: 'convert' | 'create' | 'edit'; tools: Tool[]; page?: ProductPage }[] = [
  { title: '视频转换', subtitle: '格式自由切换，精彩不受限', action: '立即转换', kind: 'convert', page: 'converter', tools: [{ label: '视频压缩', icon: 'compress' }, { label: '视频合并', icon: 'merge', tone: 'indigo' }, { label: '视频分割', icon: 'split', tone: 'purple' }] },
  { title: '文字转视频', subtitle: '把每一个灵感，变成好故事', action: '开始创作', kind: 'create', tools: [{ label: '视频转文字', icon: 'text' }, { label: '视频比例', icon: 'ratio', tone: 'purple' }, { label: '添加背景音', icon: 'music', tone: 'green' }] },
  { title: '视频剪辑', subtitle: '剪出节奏，留住高光时刻', action: '立即剪辑', kind: 'edit', tools: [{ label: '视频配乐', icon: 'music' }, { label: '视频水印', icon: 'drop', tone: 'slate' }, { label: '字幕 / 贴图', icon: 'subtitle', tone: 'indigo' }] },
];
const utilities: Tool[] = [{ label: '录音机', icon: 'record' }, { label: '屏幕录像', icon: 'screen', tone: 'slate' }, { label: '播放器', icon: 'play', tone: 'indigo', page: 'video4k' }, { label: '图片格式转换', icon: 'image' }, { label: '音频转换', icon: 'loop', tone: 'green' }, { label: '音频提取', icon: 'extract', tone: 'purple' }, { label: '文件洞察', icon: 'insight', tone: 'indigo', page: 'fileinsight' }];
const videoTools: Tool[] = [{ label: '视频转 GIF', icon: 'gif' }, { label: 'm3u8 转换', icon: 'loop' }, { label: '视频转 WebP', icon: 'image' }, { label: '视频截图', icon: 'shot' }, { label: '视频倒放', icon: 'reverse' }, { label: '视频旋转', icon: 'crop' }, { label: '视频变速', icon: 'speed' }, { label: '视频画面裁剪', icon: 'crop' }, { label: '添加片头片尾', icon: 'add' }, { label: '视频水印', icon: 'drop', tone: 'slate' }, { label: '形状视频', icon: 'shape' }, { label: '虚拟背景录像', icon: 'background' }];
const audioTools: Tool[] = [{ label: '音频剪辑', icon: 'audio' }, { label: '音频转文字', icon: 'text' }, { label: '音频分割', icon: 'split' }, { label: '音频合并', icon: 'merge' }, { label: '音频降噪', icon: 'noise' }, { label: '音频变速', icon: 'speed' }, { label: '音频倒放', icon: 'reverse' }, { label: '音频压缩', icon: 'compress' }, { label: '人声伴奏提取', icon: 'voice' }];

export function ProductHome({ navigate, notify, openMedia }: Props) {
  const [query, setQuery] = useState('');
  const nativeProfiles: Record<string, [string, number]> = {
    '视频转换': ['MP4', 82], '视频压缩': ['MP4', 45], '音频转换': ['MP3', 82],
    '音频提取': ['WAV', 82], '音频压缩': ['MP3', 35], '视频转 GIF': ['GIF', 82],
    '视频截图': ['PNG', 82], '视频转 WebP': ['WebP', 82], '图片格式转换': ['PNG', 82],
  };
  const nativeTransforms: Record<string, [string, number, string]> = {
    '视频倒放': ['MP4', 70, 'reverse'],
    '音频倒放': ['MP3', 70, 'reverse'],
    '形状视频': ['MP4', 70, 'mirror'],
    '视频旋转': ['MP4', 70, 'rotate90'],
    '视频变速': ['MP4', 70, 'speed2x'],
    '音频变速': ['MP3', 70, 'speed2x'],
    '视频画面裁剪': ['MP4', 70, 'crop16x9'],
    '视频比例': ['MP4', 70, 'crop16x9'],
    '添加片头片尾': ['MP4', 70, 'fade'],
    '视频水印': ['MP4', 70, 'watermark'],
    '字幕 / 贴图': ['MP4', 70, 'watermark'],
    '音频剪辑': ['MP3', 70, 'trim10'],
    '音频分割': ['MP3', 70, 'trim10'],
    '音频降噪': ['MP3', 70, 'denoise'],
    '音频压缩': ['MP3', 70, 'normalize'],
  };
  const activate = (tool: Tool) => {
    const transform = nativeTransforms[tool.label];
    if (transform) { openMedia(tool.label, transform[0], transform[1], transform[2]); return; }
    const profile = nativeProfiles[tool.label];
    if (profile) openMedia(tool.label, profile[0], profile[1]);
    else if (tool.page) navigate(tool.page);
    else notify(`${tool.label}：所需专用处理能力尚未接入，当前不能执行。`);
  };
  const searching = query.trim().length > 0;
  const matches = [...utilities, ...features.reduce<Tool[]>((all, feature) => all.concat(feature.tools), []), ...videoTools, ...audioTools].filter((tool) => tool.label.toLowerCase().replace(/\s/g, '').includes(query.trim().toLowerCase().replace(/\s/g, '')));
  return <main className="pd-home">
    <div className="pd-home-toolbar"><div className="pd-location"><span className="pd-home-dot" />全部工具<span className="pd-toolbar-divider" />让每一次创作，都更简单</div><label className="pd-search"><svg viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1.6" aria-hidden="true"><circle cx="10" cy="10" r="6" /><path d="m15 15 5 5" /></svg><input value={query} onChange={(event) => setQuery(event.target.value)} placeholder="搜索视频、音频工具" aria-label="搜索工具" />{query && <button onClick={() => setQuery('')} aria-label="清空搜索" type="button">×</button>}</label></div>
    {searching ? <section className="pd-search-results"><h1>搜索结果 <small>{matches.length} 项</small></h1>{matches.length ? <div className="pd-search-grid">{matches.map((tool) => <button key={tool.label} onClick={() => activate(tool)}><Jewel tool={tool} /><span>{tool.label}</span><ProductIcon name="arrow" /></button>)}</div> : <div className="pd-empty"><ProductIcon name="image" /><h2>没有找到相关工具</h2><p>试试“视频”“音频”或“转换”</p><button onClick={() => setQuery('')}>返回全部工具</button></div>}</section> : <>
    <section className="pd-feature-row" aria-label="推荐工具">{features.map((feature) => <article className={`pd-feature pd-feature-${feature.kind}`} key={feature.kind}><div className="pd-feature-banner"><div className="pd-feature-copy"><h1>{feature.title}</h1><p>{feature.subtitle}</p><button onClick={() => activate({ label: feature.title, icon: 'video', page: feature.page })}>{feature.action}<ProductIcon name="arrow" /></button></div>{feature.kind === 'convert' && <span className="pd-feature-tag hot">热门</span>}{feature.kind === 'create' && <span className="pd-feature-tag new">新功能</span>}<HeroArtwork kind={feature.kind} /></div><div className="pd-feature-tools">{feature.tools.map((tool) => <button key={tool.label} onClick={() => activate(tool)}><Jewel tool={tool} /><span>{tool.label}</span></button>)}</div></article>)}<aside className="pd-utility" aria-label="快捷工具">{utilities.map((tool) => <button key={tool.label} onClick={() => activate(tool)}><Jewel tool={tool} /><span>{tool.label}</span></button>)}</aside></section>
    <div className="pd-group-row">{[{ title: '视频处理', detail: '每一帧，都有更多可能', tools: videoTools }, { title: '音频处理', detail: '让好声音，轻松出彩', tools: audioTools }].map((group) => <section className="pd-tool-group" key={group.title}><header><h2>{group.title}</h2><span>{group.detail}</span></header><div className="pd-tool-grid">{group.tools.map((tool) => <button key={tool.label} onClick={() => activate(tool)}><ProductIcon name={tool.icon} /><span>{tool.label}</span></button>)}</div></section>)}</div>
    </>}
    <div className="pd-discover"><span><ProductIcon name="check" />本地工作空间</span><div><button onClick={() => navigate('converter')}>原生能力 <ProductIcon name="arrow" /></button><button onClick={() => navigate('components')}>组件展厅 <ProductIcon name="arrow" /></button><button onClick={() => navigate('data')}>万条数据 <ProductIcon name="arrow" /></button><button onClick={() => navigate('motion')}>动效体验 <ProductIcon name="arrow" /></button></div></div>
  </main>;
}
