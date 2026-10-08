/** Internal declarations for direct source consumers; public types stay in action-contracts.ts. */
export class ActionContractError extends Error {
  readonly code: string;
  constructor(code: string, message: string);
}
export function copySouiJson(value: unknown, maximum: number, code: string): unknown;
export function readSouiActionContract(raw: unknown, code?: string): unknown;
export function validateSouiActionInput(contract: unknown, input: unknown): void;
export function validateSouiActionOutput(contract: unknown, output: unknown): void;
