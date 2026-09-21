// A bounded *whole-window* sample set. Never silently replace early slow frames
// with a fast tail; overflow makes the quantile unavailable for that window.
export class FrameIntervalWindow {
  private readonly values: number[] = [];
  private total = 0;
  private invalid = false;
  record(intervalMs: number): void {
    this.total++;
    if (!Number.isFinite(intervalMs) || intervalMs <= 0) { this.invalid = true; return; }
    if (this.values.length < 4096) this.values.push(intervalMs);
  }
  summarize(targetHz: number): { state: 'ready'; p95: number; overBudgetPercent: number } | { state: 'empty' | 'invalid' | 'overflow' } {
    if (this.invalid || !Number.isFinite(targetHz) || targetHz <= 0) return { state: 'invalid' };
    if (this.total > 4096) return { state: 'overflow' };
    if (!this.values.length) return { state: 'empty' };
    const sorted = this.values.slice().sort((a, b) => a - b);
    return { state: 'ready', p95: sorted[Math.ceil(sorted.length * .95) - 1],
      overBudgetPercent: this.values.filter(value => value > 1000 / targetHz).length * 100 / this.values.length };
  }
  reset(): void { this.values.length = 0; this.total = 0; this.invalid = false; }
}
