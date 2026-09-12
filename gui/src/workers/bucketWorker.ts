import { bucketByTime, bucketPairByTime, type QueryPoint } from "../lib/bucketing";

type SingleReq = { reqId: number; kind: "single"; data: QueryPoint[]; from: number; to: number; maxBuckets: number };
type PairReq = { reqId: number; kind: "pair"; readData: QueryPoint[]; writeData: QueryPoint[]; from: number; to: number; maxBuckets: number };

self.onmessage = (e: MessageEvent<SingleReq | PairReq>) => {
  const msg = e.data;
  if (msg.kind === "single") {
    const result = bucketByTime(msg.data, msg.from, msg.to, msg.maxBuckets);
    (self as any).postMessage({ reqId: msg.reqId, result });
  } else {
    const result = bucketPairByTime(msg.readData, msg.writeData, msg.from, msg.to, msg.maxBuckets);
    (self as any).postMessage({ reqId: msg.reqId, result });
  }
};