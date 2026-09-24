import { connectSouiNative, type SouiNativeClientImpl, type SouiCoreMethodMap, type SouiClipboardMethodMap } from '@soui-sdk';
export type AppMethods = SouiCoreMethodMap & SouiClipboardMethodMap & {
  'app.greet': {params:{name:string};result:{message:string}};
};
export function connectNative():Promise<SouiNativeClientImpl<AppMethods>> {
  return connectSouiNative<AppMethods>();
}
