import { connectSouiNative, type SouiNativeClientImpl, type SouiCoreMethodMap, type SouiClipboardMethodMap, type SouiDirectoryMethodMap, type SouiWindowMethodMap } from '@soui-sdk';
import type { ActionMethods } from './generated/actions';
export type AppMethods = SouiCoreMethodMap & SouiClipboardMethodMap & SouiDirectoryMethodMap & SouiWindowMethodMap & ActionMethods;
export function connectNative():Promise<SouiNativeClientImpl<AppMethods>> {
  return connectSouiNative<AppMethods>();
}
