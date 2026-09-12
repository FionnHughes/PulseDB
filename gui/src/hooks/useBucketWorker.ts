import { useEffect, useRef } from "react";
import type { QueryPoint, ComboPoint } from "../lib/bucketing";

export function useBucketWorker() {
  const workerRef = useRef<Worker | null>(null);
  const pendingRef = useRef<Map<number, (v: any) => void>>(new Map());
  const idRef = useRef(0);

  useEffect(() => {
    const worker = new Worker(new URL("../workers/bucketWorker.ts", import.meta.url), { type: "module" });
    worker.onmessage = (e) => {
      const resolve = pendingRef.current.get(e.data.reqId);
      if (resolve) {
        resolve(e.data.result);
        pendingRef.current.delete(e.data.reqId);
      }
    };
    workerRef.current = worker;
    return () => worker.terminate();
  }, []);

  function bucketSingle(data: QueryPoint[], from: number, to: number, maxBuckets: number): Promise<QueryPoint[]> {
    return new Promise((resolve) => {
      const reqId = idRef.current++;
      pendingRef.current.set(reqId, resolve);
      workerRef.current?.postMessage({ reqId, kind: "single", data, from, to, maxBuckets });
    });
  }

  function bucketPair(readData: QueryPoint[], writeData: QueryPoint[], from: number, to: number, maxBuckets: number): Promise<ComboPoint[]> {
    return new Promise((resolve) => {
      const reqId = idRef.current++;
      pendingRef.current.set(reqId, resolve);
      workerRef.current?.postMessage({ reqId, kind: "pair", readData, writeData, from, to, maxBuckets });
    });
  }

  return { bucketSingle, bucketPair };
}