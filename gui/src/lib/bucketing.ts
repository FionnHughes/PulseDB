export type QueryPoint = { ts: number; value: number };
export type ComboPoint = { ts: number; read?: number; write?: number };

// groups points into time buckets and averages each one, keeps charts fast on big ranges
export function bucketByTime(data: QueryPoint[], from: number, to: number, maxBuckets: number): QueryPoint[] {
  if (data.length === 0) return [];
  const bucketMs = Math.max(1, Math.ceil((to - from) / maxBuckets));
  const buckets = new Map<number, number[]>();
  for (const p of data) {
    const idx = Math.floor((p.ts - from) / bucketMs);
    const arr = buckets.get(idx) ?? [];
    arr.push(p.value);
    buckets.set(idx, arr);
  }
  const out: QueryPoint[] = [];
  for (const [idx, vals] of Array.from(buckets.entries()).sort((a, b) => a[0] - b[0])) {
    const ts = from + idx * bucketMs + bucketMs / 2;
    out.push({ ts, value: vals.reduce((s, v) => s + v, 0) / vals.length });
  }
  return out;
}

// same but for two series at once so read/write line up on the same time buckets
export function bucketPairByTime(readData: QueryPoint[], writeData: QueryPoint[], from: number, to: number, maxBuckets: number): ComboPoint[] {
  const bucketMs = Math.max(1, Math.ceil((to - from) / maxBuckets));
  const map = new Map<number, { r: number[]; w: number[] }>();
  const add = (arr: QueryPoint[], key: "r" | "w") => {
    for (const p of arr) {
      const idx = Math.floor((p.ts - from) / bucketMs);
      const entry = map.get(idx) ?? { r: [], w: [] };
      entry[key].push(p.value);
      map.set(idx, entry);
    }
  };
  add(readData, "r");
  add(writeData, "w");
  const out: ComboPoint[] = [];
  for (const [idx, entry] of Array.from(map.entries()).sort((a, b) => a[0] - b[0])) {
    const ts = from + idx * bucketMs + bucketMs / 2;
    out.push({
      ts,
      read: entry.r.length ? entry.r.reduce((s, v) => s + v, 0) / entry.r.length : undefined,
      write: entry.w.length ? entry.w.reduce((s, v) => s + v, 0) / entry.w.length : undefined,
    });
  }
  return out;
}