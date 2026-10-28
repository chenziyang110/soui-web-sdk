// This classifies the Workbench probe's actual document URL; it grants no authority.
// Native trusted_entry and the current execution-context receipt remain mandatory.
export type WorkbenchResourceLocation = {
  readonly protocol: string;
  readonly href: string;
};

export function hasWorkbenchLocalResources(source: WorkbenchResourceLocation): boolean {
  // Preserve the existing file-URL probe. The Host still verifies its real entry.
  if (source.protocol === 'file:') return true;
  // The native provider issues exactly one lowercase 32-hex host and document
  // prefix. Raw matching also rejects credentials and explicit default ports.
  return source.protocol === 'https:' && !/[\r\n]/.test(source.href) &&
    /^https:\/\/[0-9a-f]{32}\.soui-local\.invalid\/[0-9a-f]{32}\/(?:index|react)\.html$/.test(source.href);
}
