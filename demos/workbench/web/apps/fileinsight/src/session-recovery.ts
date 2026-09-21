export const sessionExpiredMessage = '授权已到期，需要重新连接并重新选择目录。';

export type NativeErrorLike = { code?: unknown; message?: unknown };

export function errorCode(error: unknown): string {
  if (error && typeof error === 'object' && 'code' in error) {
    const code = (error as NativeErrorLike).code;
    return typeof code === 'string' ? code : '';
  }
  return '';
}

export function isSessionExpired(error: unknown): boolean {
  return errorCode(error) === 'E_SESSION_EXPIRED';
}

export function errorText(error: unknown): string {
  const code = errorCode(error);
  const detail = error instanceof Error
    ? error.message
    : error && typeof error === 'object' && typeof (error as NativeErrorLike).message === 'string'
      ? String((error as NativeErrorLike).message)
      : '';
  if (code === 'E_UNAUTHORIZED') return detail ? `当前请求未获授权：${detail}` : '当前请求未获授权。';
  if (code === 'E_TIMEOUT') return detail ? `native 请求超时：${detail}` : 'native 请求超时，任务状态未改变。';
  return detail || 'native 文件服务暂不可用';
}
