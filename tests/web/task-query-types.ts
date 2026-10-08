import { SouiNativeClientImpl } from '../../sdk/web/dist/index.js';
import type { SouiCoreMethodMap, SouiMethodMap, SouiTaskQueryParams, SouiNativePort } from '../../sdk/web/dist/index.js';
declare const port: SouiNativePort;
const core = new SouiNativeClientImpl<SouiCoreMethodMap>(port, {sessionId:'s',origin:'app://types',token:'t'});
const legacy = new SouiNativeClientImpl<SouiMethodMap>(port, {sessionId:'s',origin:'app://types',token:'t'});
const id: SouiTaskQueryParams = {taskId:'task'};
const operation: SouiTaskQueryParams = {operationId:'operation'};
core.call('task.query', id); core.call('task.query', operation);
legacy.call('task.query', id); legacy.call('task.query', operation);
core.query('task'); core.queryOperation('operation'); legacy.query('task'); legacy.queryOperation('operation');
// @ts-expect-error both selectors are not a query
core.call('task.query', {taskId:'task',operationId:'operation'});
// @ts-expect-error neither selector is not a query
core.call('task.query', {});
// @ts-expect-error extra fields are not part of the exact public shape
core.call('task.query', {taskId:'task',extra:true});
// @ts-expect-error wrong selector type
legacy.call('task.query', {operationId:1});
// @ts-expect-error params cannot be an array
core.call('task.query', []);
// @ts-expect-error params cannot be null
core.call('task.query', null);
// @ts-expect-error the other selector cannot be explicitly undefined
const undefinedOther: SouiTaskQueryParams = {taskId:'task',operationId:undefined};
void undefinedOther;
