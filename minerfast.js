// miner-fast.js — Multi-threaded miner untuk hash256.org
// Memanfaatkan semua CPU core via worker_threads
require("dotenv").config();
const { ethers } = require("ethers");
const { Worker } = require("worker_threads");
const os = require("os");
const path = require("path");
const crypto = require("crypto");

// ─── Config ──────────────────────────────────────────────────────────────────
const RPC_URL      = process.env.RPC_URL;
const PRIVATE_KEY  = process.env.PRIVATE_KEY;
const NUM_WORKERS  = parseInt(process.env.WORKERS || os.cpus().length); // default semua core
const CONTRACT_ADDRESS = "0xAC7b5d06fa1e77D08aea40d46cB7C5923A87A0cc";
const ABI = [
  "function getChallenge(address miner) view returns (bytes32)",
  "function miningState() view returns (uint256 era,uint256 reward,uint256 difficulty,uint256 minted,uint256 remaining,uint256 epoch,uint256 epochBlocksLeft_)",
  "function mine(uint256 nonce)",
];

// ─── Validasi ENV ─────────────────────────────────────────────────────────────
function requireEnv() {
  if (!RPC_URL || !PRIVATE_KEY) {
    console.error("Isi RPC_URL dan PRIVATE_KEY di file .env dulu.");
    process.exit(1);
  }
  if (!PRIVATE_KEY.startsWith("0x")) {
    console.error("PRIVATE_KEY harus diawali 0x.");
    process.exit(1);
  }
}

// ─── Random start nonce (besar, 64-bit range) ────────────────────────────────
function randomStartNonce() {
  // Pakai crypto.randomBytes agar lebih acak dari Math.random()
  const buf = crypto.randomBytes(8);
  const hi = buf.readUInt32BE(0);
  const lo = buf.readUInt32BE(4);
  return BigInt(hi) * 4294967296n + BigInt(lo); // ~64-bit random
}

// ─── Spawn workers, return nonce saat ketemu ──────────────────────────────────
function findNonce(challenge, difficulty) {
  return new Promise((resolve, reject) => {
    const workers = [];
    let found = false;
    const startNonce = randomStartNonce();

    // Stats tracking
    const hashCounts = new Array(NUM_WORKERS).fill(0n);
    let statsInterval = setInterval(() => {
      const total = hashCounts.reduce((a, b) => a + b, 0n);
      const mhps = (Number(total) / 1e6).toFixed(2);
      process.stdout.write(`\r⛏  ${NUM_WORKERS} workers | ${mhps}M hashes checked...`);
    }, 2000);

    for (let i = 0; i < NUM_WORKERS; i++) {
      const worker = new Worker(path.join(__dirname, "worker.js"), {
        workerData: {
          challenge,
          difficulty: difficulty.toString(),
          startNonce: startNonce.toString(),
          workerId: i,
          totalWorkers: NUM_WORKERS,
        },
      });

      worker.on("message", (msg) => {
        if (msg.type === "found" && !found) {
          found = true;
          clearInterval(statsInterval);
          // Terminate semua worker lain
          workers.forEach((w) => w.terminate());
          process.stdout.write("\n");
          resolve({ nonce: BigInt(msg.nonce), hash: msg.hash });
        } else if (msg.type === "progress") {
          hashCounts[msg.workerId] = BigInt(msg.count);
        }
      });

      worker.on("error", (err) => {
        if (!found) {
          clearInterval(statsInterval);
          workers.forEach((w) => w.terminate());
          reject(err);
        }
      });

      workers.push(worker);
    }
  });
}

// ─── Main loop ────────────────────────────────────────────────────────────────
async function main() {
  requireEnv();

  const provider = new ethers.JsonRpcProvider(RPC_URL);
  const wallet   = new ethers.Wallet(PRIVATE_KEY, provider);
  const contract = new ethers.Contract(CONTRACT_ADDRESS, ABI, wallet);

  console.log("━".repeat(60));
  console.log("🚀 HASH256 Multi-threaded CLI Miner");
  console.log(`💻 CPU Cores dipakai : ${NUM_WORKERS}`);
  console.log(`👛 Wallet            : ${wallet.address}`);
  console.log(`📄 Contract          : ${CONTRACT_ADDRESS}`);
  console.log("━".repeat(60));

  let roundNum = 0;

  while (true) {
    roundNum++;
    console.log(`\n[Round #${roundNum}] Ambil state kontrak...`);

    const state      = await contract.miningState();
    const difficulty = BigInt(state.difficulty.toString());
    const challenge  = await contract.getChallenge(wallet.address);

    console.log(`Era        : ${state.era}`);
    console.log(`Reward     : ${ethers.formatUnits(state.reward, 18)} HASH`);
    console.log(`Epoch      : ${state.epoch}`);
    console.log(`Difficulty : ${difficulty.toString().substring(0, 20)}... (${difficulty.toString().length} digits)`);
    console.log(`Challenge  : ${challenge}`);
    console.log(`\n⛏  Mencari nonce dengan ${NUM_WORKERS} worker threads...`);

    const startTime = Date.now();

    try {
      const { nonce, hash } = await findNonce(challenge, difficulty);
      const elapsed = ((Date.now() - startTime) / 1000).toFixed(1);

      console.log(`✅ FOUND nonce : ${nonce}`);
      console.log(`   Hash        : ${hash}`);
      console.log(`   Waktu       : ${elapsed}s`);

      const tx = await contract.mine(nonce, {
        gasLimit: 500_000, // safety margin
      });
      console.log(`📡 TX sent     : ${tx.hash}`);

      const receipt = await tx.wait();
      console.log(`🎉 Success!    Block #${receipt.blockNumber}`);
    } catch (err) {
      console.error("❌ Error:", err.shortMessage || err.message);
      console.log("   Retry dalam 5 detik...");
      await new Promise((r) => setTimeout(r, 5000));
    }
  }
}

main().catch((err) => {
  console.error(err.shortMessage || err.message || err);
  process.exit(1);
});
