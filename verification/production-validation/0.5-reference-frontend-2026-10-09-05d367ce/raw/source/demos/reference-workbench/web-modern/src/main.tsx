import React from 'react';
import {createRoot} from 'react-dom/client';
import {requireSouiWebFeatures} from '@soui-sdk';
import {ReferenceReact} from '../../common/react-view';
const root=document.getElementById('app')!;
try {
  requireSouiWebFeatures(['promise','proxy','resizeObserver','cssGrid']);
  createRoot(root).render(<ReferenceReact profile="modern-react19-webview2"/>);
} catch {root.textContent='E_MODERN_CAPABILITY: 当前宿主缺少 Modern 页面所需能力。';}
