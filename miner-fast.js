// miner-fast.js — Hybrid: Node.js untuk TX, C miner untuk hashing
require("dotenv").config();
const { ethers } = require("ethers");
const { spawn }  = require("child_process");
const os         = require("os");
const path       = require("path");

const RPC_URL      = process.env.RPC_URL;
const PRIVATE_KEY  = process.env.PRIVATE_KEY;
const NUM_THREADS  = parseInt(process.env.WORKERS || os.cpus().length);
const MINER_BIN    = path.join(__dirname, "miner-c");
const CONTRACT_ADDRESS = "0xAC7b5d06fa1e77D08aea40d46cB7C5923A87A0cc";
const ABI = [
  "function getChallenge(address miner) view returns (bytes32)",
  "function miningState() view returns (uint256 era,uint256 reward,uint256 difficulty,uint256 minted,uint256 remaining,uint256 epoch,uint256 epochBlocksLeft_)",
  "function mine(uint256 nonce)",
];

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

function findNonce(challenge, difficulty) {
  return new Promise((resolve, reject) => {
    const proc = spawn(MINER_BIN, [
      challenge,
      difficulty.toString(),
      NUM_THREADS.toString(),
    ]);

    let output = "";

    proc.stdout.on("data", (data) => {
      const text = data.toString();
      process.stdout.write(text);
      output += text;
      const m = output.match(/FOUND nonce\s*:\s*(\d+)/);
      if (m) {
        resolve(BigInt(m[1]));
        proc.kill();
      }
    });

    proc.stderr.on("data", (data) => process.stderr.write(data.toString()));

    proc.on("error", (err) => {
      reject(new Error("Gagal jalankan miner-c: " + err.message +
        "\nPastikan sudah compile: gcc -O3 -o miner-c miner.c -lpthread"));
    });

    proc.on("close", (code) => {
      if (code !== 0 && code !== null) reject(new Error("miner-c exit: " + code));
    });
  });
}

async function main() {
  requireEnv();

  const provider = new ethers.JsonRpcProvider(RPC_URL);
  const wallet   = new ethers.Wallet(PRIVATE_KEY, provider);
  const contract = new ethers.Contract(CONTRACT_ADDRESS, ABI, wallet);

  console.log("━".repeat(60));
  console.log("🚀 HASH256 Hybrid Miner (C + Node.js)");
  console.log(`💻 Threads : ${NUM_THREADS}`);
  console.log(`👛 Wallet  : ${wallet.address}`);
  console.log(`📄 Contract: ${CONTRACT_ADDRESS}`);
  console.log("━".repeat(60));

  let round = 0;

  while (true) {
    round++;
    console.log(`\n[Round #${round}] Ambil state kontrak...`);

    const state      = await contract.miningState();
    const difficulty = BigInt(state.difficulty.toString());
    const challenge  = await contract.getChallenge(wallet.address);

    console.log(`Era       : ${state.era}`);
    console.log(`Reward    : ${ethers.formatUnits(state.reward, 18)} HASH`);
    console.log(`Epoch     : ${state.epoch}`);
    console.log(`Difficulty: ${difficulty.toString().slice(0, 20)}... (${difficulty.toString().length} digits)`);
    console.log(`Challenge : ${challenge}`);
    console.log(`\n⛏  Menjalankan C miner dengan ${NUM_THREADS} threads...\n`);

    const startTime = Date.now();

    try {
      const nonce   = await findNonce(challenge, difficulty);
      const elapsed = ((Date.now() - startTime) / 1000).toFixed(1);
      console.log(`\n⏱  Waktu   : ${elapsed}s`);

      const tx = await contract.mine(nonce, { gasLimit: 500_000 });
      console.log(`📡 TX sent : ${tx.hash}`);

      const receipt = await tx.wait();
      console.log(`🎉 Success! Block #${receipt.blockNumber}`);
    } catch (err) {
      console.error("\n❌ Error:", err.shortMessage || err.message);
      console.log("   Retry dalam 5 detik...");
      await new Promise((r) => setTimeout(r, 5000));
    }
  }
}

main().catch((err) => {
  console.error(err.shortMessage || err.message || err);
  process.exit(1);
});
