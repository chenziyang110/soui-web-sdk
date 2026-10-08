import { connectSouiNative, type SouiNativeClientImpl, type SouiCoreMethodMap } from '@soui-sdk';
export type AppMethods = SouiCoreMethodMap & {
  'app.greet': {params:{name:string};result:{message:string}};
};
export function connectNative():Promise<SouiNativeClientImpl<AppMethods>> {
  return connectSouiNative<AppMethods>();
}
