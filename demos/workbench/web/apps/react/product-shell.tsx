import React, { useEffect, useRef, useState } from 'react';
import { ProductIcon, type ProductPage } from './product-home';

export type WindowAction = 'desktop.window.minimize' | 'desktop.window.toggleMaximize' | 'desktop.window.close' | 'desktop.window.beginDrag';
export const productPages: { id: ProductPage; label: string; detail: string; icon: string }[] = [
  { id: 'home', label: '首页', detail: '全部视频与音频工具', icon: 'video' },
  { id: 'converter', label: '视频转换', detail: '转换工作台', icon: 'loop' },
  { id: 'video4k', label: '4K 播放器', detail: '本地播放与预览', icon: 'play' },
  { id: 'components', label: '组件展厅', detail: '表单、表格与常用控件', icon: 'ratio' },
  { id: 'motion', label: '动效体验', detail: '交互与帧率观察', icon: 'speed' },
  { id: 'cad', label: 'CAD 工作台', detail: '图形视口', icon: 'shape' },
  { id: 'compat', label: '运行环境', detail: '兼容能力与版本', icon: 'record' },
  { id: 'fileinsight', label: '文件洞察', detail: '原生目录统计', icon: 'insight' },
];

export function ProductTitlebar({ page, navigate, action, notify }: { page: ProductPage; navigate: (page: ProductPage) => void; action: (method: WindowAction) => void; notify: (text: string) => void }) {
  const [menuOpen, setMenuOpen] = useState(false);
  const menu = useRef<HTMLDivElement>(null);
  const trigger = useRef<HTMLButtonElement>(null);
  useEffect(() => {
    if (!menuOpen) return;
    const outside = (event: MouseEvent) => { if (!menu.current?.contains(event.target as Node)) setMenuOpen(false); };
    const escape = (event: KeyboardEvent) => { if (event.key === 'Escape') { setMenuOpen(false); trigger.current?.focus(); } };
    document.addEventListener('mousedown', outside);
    document.addEventListener('keydown', escape);
    return () => { document.removeEventListener('mousedown', outside); document.removeEventListener('keydown', escape); };
  }, [menuOpen]);
  return <header className="pd-titlebar">
    <button className="pd-brand" onClick={() => navigate('home')} title="返回首页"><span className="pd-brand-symbol"><ProductIcon name="video" /></span><strong>迅捷视频转换器</strong><span className="pd-edition">极速版</span></button>
    <div className="pd-window-drag" data-soui-chrome="drag" onMouseDown={(event) => { if (event.button === 0) { event.preventDefault(); action('desktop.window.beginDrag'); } }} aria-hidden="true" />
    <span className="pd-demo-label">体验版</span>
    <button className="pd-title-link" onClick={() => notify('此演示在本地运行，无需登录账号。')}><span className="pd-avatar"><svg viewBox="0 0 24 24" aria-hidden="true"><circle cx="12" cy="9" r="4" fill="#4f78b8" /><path d="M4 23v-3a8 8 0 0 1 16 0v3" fill="#5b99ee" /></svg></span>注册 / 登录</button>
    <button className="pd-title-link pd-help" onClick={() => notify('从右侧菜单进入组件展厅、动效体验和运行环境。')}><svg className="pd-icon" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1.6" aria-hidden="true"><path d="M4 13v-2a8 8 0 0 1 16 0v5c0 3-3 4-6 4M4 11H2v6h4v-6Zm14 0h4v6h-4Z" /></svg>帮助中心</button>
    <div className="pd-menu-anchor" ref={menu}><button ref={trigger} className={`pd-caption-button ${menuOpen ? 'selected' : ''}`} aria-label="打开页面菜单" aria-expanded={menuOpen} aria-controls="product-pages" onClick={() => setMenuOpen(!menuOpen)}><svg viewBox="0 0 24 24" aria-hidden="true"><path d="M5 6h14M5 12h14M5 18h14" /></svg></button>{menuOpen && <nav id="product-pages" className="pd-page-menu" aria-label="页面导航"><div className="pd-menu-heading">工作空间</div>{productPages.map((item) => <button key={item.id} className={page === item.id ? 'active' : ''} onClick={() => { navigate(item.id); setMenuOpen(false); }}><ProductIcon name={item.icon} /><span><b>{item.label}</b><small>{item.detail}</small></span>{page === item.id && <span className="pd-menu-dot" />}</button>)}</nav>}</div>
    <span className="pd-caption-divider" />
    <button className="pd-caption-button" data-soui-chrome="minimize" title="最小化" aria-label="最小化" onClick={() => action('desktop.window.minimize')}><svg viewBox="0 0 24 24" aria-hidden="true"><path d="M6 12h12" /></svg></button>
    <button className="pd-caption-button" data-soui-chrome="maximize" title="最大化或还原" aria-label="最大化或还原" onClick={() => action('desktop.window.toggleMaximize')}><svg viewBox="0 0 24 24" aria-hidden="true"><rect x="6" y="6" width="12" height="12" rx="1" /></svg></button>
    <button className="pd-caption-button close" data-soui-chrome="close" title="关闭" aria-label="关闭" onClick={() => action('desktop.window.close')}><svg viewBox="0 0 24 24" aria-hidden="true"><path d="m6 6 12 12M6 18 18 6" /></svg></button>
  </header>;
}

export function ProductFeedback({ notice, clear }: { notice: string; clear: () => void }) {
  useEffect(() => { if (!notice) return; const timer = window.setTimeout(clear, 5000); return () => window.clearTimeout(timer); }, [notice, clear]);
  return <div className="pd-feedback-region" role="status" aria-live="polite" aria-atomic="true">{notice && <div className="pd-feedback"><span className="pd-feedback-info">i</span><span>{notice}</span><button onClick={clear} aria-label="关闭提示">×</button></div>}</div>;
}
