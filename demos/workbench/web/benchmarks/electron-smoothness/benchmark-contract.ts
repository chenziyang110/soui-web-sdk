export const SCENES = [
  { id: 'S01', label: '首页', detail: 'home / switch / expand' },
  { id: 'S02', label: '10k 滚动', detail: 'virtualized rows' },
  { id: 'S03', label: '输入', detail: 'menu / input / IME' },
  { id: 'S04', label: '窗口', detail: 'resize / move' },
  { id: 'S05', label: '动效', detail: '120 s motion' },
] as const;

export type SceneId = (typeof SCENES)[number]['id'];

export const FIXTURE = {
  fixtureId: 'electron-smoothness-10k-v1',
  seed: 20260915,
  rowCount: 10000,
  pageSize: 200,
  rowHeightPx: 32,
} as const;

export type BenchmarkRow = {
  id: number;
  name: string;
  group: string;
  sizeKb: number;
  status: 'ready' | 'indexed' | 'queued' | 'review';
};

function hash32(value: number): number {
  let next = value >>> 0;
  next ^= next >>> 16;
  next = Math.imul(next, 0x7feb352d) >>> 0;
  next ^= next >>> 15;
  next = Math.imul(next, 0x846ca68b) >>> 0;
  return (next ^ (next >>> 16)) >>> 0;
}

export function rowAt(index: number): BenchmarkRow {
  const value = hash32(FIXTURE.seed + index * 2654435761);
  const groups = ['素材', '项目', '缓存', '导出', '归档'];
  const statuses: BenchmarkRow['status'][] = ['ready', 'indexed', 'queued', 'review'];
  return {
    id: index + 1,
    name: `fixture-${String(index + 1).padStart(5, '0')}.asset`,
    group: groups[value % groups.length],
    sizeKb: 16 + (value % 4096),
    status: statuses[(value >>> 8) % statuses.length],
  };
}

export class AsyncMemoryAdapter {
  private readonly rows: BenchmarkRow[];

  constructor() {
    this.rows = Array.from({ length: FIXTURE.rowCount }, (_, index) => rowAt(index));
  }

  async getPage(offset: number, limit = FIXTURE.pageSize): Promise<BenchmarkRow[]> {
    await new Promise<void>((resolve) => window.setTimeout(resolve, 0));
    return this.rows.slice(offset, Math.min(offset + limit, this.rows.length));
  }

  async loadAll(onPage?: (loaded: number) => void): Promise<BenchmarkRow[]> {
    const result: BenchmarkRow[] = [];
    for (let offset = 0; offset < this.rows.length; offset += FIXTURE.pageSize) {
      const page = await this.getPage(offset, FIXTURE.pageSize);
      for (const row of page) result.push(row);
      onPage?.(result.length);
    }
    return result;
  }
}
