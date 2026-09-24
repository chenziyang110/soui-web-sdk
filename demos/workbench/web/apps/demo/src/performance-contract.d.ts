// Type-only extension for an authenticated no-op registered in diagnostic C++ builds.
import '../../../../../../sdk/web/src/contracts';
declare module '../../../../../../sdk/web/src/contracts' {
  interface SouiMethodMap {
    'bridge.benchmark.ping': { params: Record<string, never>; result: Record<string, never> };
  }
}
