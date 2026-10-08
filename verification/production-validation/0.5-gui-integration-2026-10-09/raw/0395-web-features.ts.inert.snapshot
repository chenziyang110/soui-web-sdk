import { SouiBridgeError } from './protocol.js';

export type SouiWebFeature = 'promise' | 'proxy' | 'fetch' | 'abortController' |
  'resizeObserver' | 'intersectionObserver' | 'webAssembly' | 'webGpuInterface' |
  'cssGrid' | 'cssContainerQueries' | 'cssOklch' | 'cssRegisteredProperties';
export type SouiFeatureObservation = 'available' | 'missing' | 'unknown';
export interface SouiWebFeatureReport {
  protocol: 'soui.web-features/1';
  /** Interface/syntax detection only; this does not certify behavior, GPU hardware or permission. */
  scope: 'surface-detection';
  features: Record<SouiWebFeature, SouiFeatureObservation>;
}
export interface SouiWebProbeTarget {
  Promise?: unknown; Proxy?: unknown; fetch?: unknown; AbortController?: unknown;
  ResizeObserver?: unknown; IntersectionObserver?: unknown; WebAssembly?: unknown;
  navigator?: { gpu?: unknown };
  CSS?: { supports?: (property: string, value: string) => boolean; registerProperty?: unknown };
}
const names: SouiWebFeature[] = ['promise', 'proxy', 'fetch', 'abortController', 'resizeObserver',
  'intersectionObserver', 'webAssembly', 'webGpuInterface', 'cssGrid', 'cssContainerQueries',
  'cssOklch', 'cssRegisteredProperties'];

/** No UA guessing, GPU/device creation, permission requests or network traffic. */
export function inspectSouiWebFeatures(target?: SouiWebProbeTarget): SouiWebFeatureReport {
  const environment = target ?? (typeof window === 'undefined' ? undefined : window as SouiWebProbeTarget);
  const features = {} as Record<SouiWebFeature, SouiFeatureObservation>;
  function observe(name: SouiWebFeature, probe: () => boolean): void {
    if (!environment) { features[name] = 'unknown'; return; }
    try { features[name] = probe() ? 'available' : 'missing'; }
    catch { features[name] = 'unknown'; }
  }
  function css(property: string, value: string): boolean {
    return typeof environment?.CSS?.supports === 'function' && environment.CSS.supports(property, value);
  }
  observe('promise', () => typeof environment?.Promise === 'function');
  observe('proxy', () => typeof environment?.Proxy === 'function');
  observe('fetch', () => typeof environment?.fetch === 'function');
  observe('abortController', () => typeof environment?.AbortController === 'function');
  observe('resizeObserver', () => typeof environment?.ResizeObserver === 'function');
  observe('intersectionObserver', () => typeof environment?.IntersectionObserver === 'function');
  observe('webAssembly', () => !!environment?.WebAssembly);
  observe('webGpuInterface', () => !!environment?.navigator?.gpu);
  observe('cssGrid', () => css('display', 'grid'));
  observe('cssContainerQueries', () => css('container-type', 'inline-size'));
  observe('cssOklch', () => css('color', 'oklch(50% 0.1 120)'));
  observe('cssRegisteredProperties', () => typeof environment?.CSS?.registerProperty === 'function');
  return { protocol: 'soui.web-features/1', scope: 'surface-detection', features };
}

/** Validate the required surfaces before mounting the app; never grants native permissions. */
export function requireSouiWebFeatures(required: readonly SouiWebFeature[], target?: SouiWebProbeTarget): SouiWebFeatureReport {
  if (!Array.isArray(required) || required.length > names.length ||
      required.some(name => names.indexOf(name) < 0) || new Set(required).size !== required.length)
    throw new SouiBridgeError('E_INVALID_FEATURES', 'Expected a bounded list of unique supported feature names.');
  const report = inspectSouiWebFeatures(target);
  const missing = required.filter((name: SouiWebFeature) => report.features[name] !== 'available');
  if (missing.length) throw new SouiBridgeError('E_WEB_FEATURES_UNAVAILABLE',
    `This application requires unavailable or unverified Web features: ${missing.join(', ')}.`);
  return report;
}
