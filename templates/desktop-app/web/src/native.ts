import { SouiNativeClientImpl, type SouiNativePort } from '@soui-sdk';
export type AppMethods = {
  'app.greet': {params:{name:string};result:{message:string}};
  'bridge.probe.report': {params:{ready:boolean};result:{ready:boolean}};
};
type NativeWindow = Window & {__souiNativePostMessage?:(envelope:string)=>string};
export function connectNative():SouiNativeClientImpl<AppMethods> {
  const send=(window as NativeWindow).__souiNativePostMessage;
  if(!send)throw new Error('请从桌面程序启动，以连接 C++ 服务。');
  const requestId='starter:bootstrap';
  const bootstrap=JSON.parse(send(JSON.stringify({requestId,sessionId:'',origin:'',token:'',method:'bridge.bootstrap',params:{protocol:'soui.bridge/1'}})));
  if(bootstrap.requestId!==requestId || !bootstrap.ok || !bootstrap.result?.sessionId || !bootstrap.result?.origin || !bootstrap.result?.token)
    throw new Error('原生服务连接失败。');
  const port:SouiNativePort={request:async envelope=>send(envelope),onEvent:()=>()=>{}};
  return new SouiNativeClientImpl<AppMethods>(port,bootstrap.result);
}
