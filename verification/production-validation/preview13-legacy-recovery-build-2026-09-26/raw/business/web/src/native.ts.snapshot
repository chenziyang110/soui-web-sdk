import { connectSouiNative, type SouiNativeClientImpl, type SouiCoreMethodMap, type SouiClipboardMethodMap, type SouiDirectoryMethodMap, type SouiWindowMethodMap } from '@soui-sdk';
export type AppMethods = SouiCoreMethodMap & SouiClipboardMethodMap & SouiDirectoryMethodMap & SouiWindowMethodMap & {
  'app.greet': {params:{name:string};result:{message:string}};
};
export function connectNative():Promise<SouiNativeClientImpl<AppMethods>> {
  return connectSouiNative<AppMethods>();
}
