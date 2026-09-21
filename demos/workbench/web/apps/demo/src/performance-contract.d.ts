// Type-only extension for an authenticated no-op registered in diagnostic C++ builds.
import '../../../../generated/contracts/soui_native.generated';
declare module '../../../../generated/contracts/soui_native.generated' {
  interface SouiMethodMap {
    'bridge.benchmark.ping': { params: Record<string, never>; result: Record<string, never> };
  }
}
