import { connectSouiNative, type SouiNativeClientImpl, type SouiCoreMethodMap, type SouiClipboardMethodMap, type SouiWindowMethodMap } from '@soui-sdk';
export type AppMethods = SouiCoreMethodMap & SouiClipboardMethodMap & SouiWindowMethodMap & {
  'app.greet': {params:{name:string};result:{message:string}};
};
export function connectNative():Promise<SouiNativeClientImpl<AppMethods>> {
  return connectSouiNative<AppMethods>();
}
