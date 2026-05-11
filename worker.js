const { workerData, parentPort } = require("worker_threads");
const keccak = require("keccak");

const { challenge, difficulty, startNonce, workerId, totalWorkers } = workerData;

const diff = BigInt(difficulty);
let nonce = BigInt(startNonce) + BigInt(workerId);
const step = BigInt(totalWorkers);
let count = 0n;

const challengeHex = challenge.startsWith("0x") ? challenge.slice(2) : challenge;
const challengeBuf = Buffer.from(challengeHex, "hex");
const nonceBuf = Buffer.allocUnsafe(32);

while (true) {
  let n = nonce;
  for (let i = 31; i >= 0; i--) {
    nonceBuf[i] = Number(n & 0xffn);
    n >>= 8n;
  }

  const hash = keccak("keccak256").update(challengeBuf).update(nonceBuf).digest();

  let hashNum = 0n;
  for (let i = 0; i < 32; i++) {
    hashNum = (hashNum << 8n) | BigInt(hash[i]);
  }

  if (hashNum < diff) {
    parentPort.postMessage({ type: "found", nonce: nonce.toString(), hash: "0x" + hash.toString("hex") });
    break;
  }

  nonce += step;
  count++;

  if (count % 500_000n === 0n) {
    parentPort.postMessage({ type: "progress", workerId, count: count.toString() });
  }
}
