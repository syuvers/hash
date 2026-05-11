# HASH256 Miner

High-performance CPU miner for the HASH256 token on Ethereum. Uses a hybrid architecture: C for hashing (Keccak-256) and Node.js for blockchain interaction.

## Performance

| Mode | Speed (24 threads) | Description |
|------|-------------------|-------------|
| **AVX2 (default)** | ~132M hash/s | 4-way SIMD parallel, recommended |
| Scalar | ~56M hash/s | Fallback for older CPUs |

## Requirements

- Linux (Ubuntu recommended)
- GCC with AVX2 support
- Node.js >= 18
- CPU with AVX2 (Intel Haswell+ / AMD Zen+)

## Quick Start

```bash
# 1. Install dependencies
npm install

# 2. Configure
cp .env.example .env
# Edit .env with your RPC_URL and PRIVATE_KEY

# 3. Compile miner (pick one)
gcc -O3 -mavx2 -o miner-avx2 miner-avx2.c -lpthread   # AVX2 (faster)
gcc -O3 -o miner-c miner.c -lpthread                    # Scalar (fallback)

# 4. Run
node miner-fast.js
```

## Configuration

Environment variables (set in `.env` or inline):

| Variable | Default | Description |
|----------|---------|-------------|
| `RPC_URL` | - | Ethereum RPC endpoint (required) |
| `PRIVATE_KEY` | - | Wallet private key with 0x prefix (required) |
| `WORKERS` | CPU count | Number of mining threads |
| `MINER_BIN` | `miner-avx2` | Binary to use (`miner-avx2` or `miner-c`) |

### Examples

```bash
# Use 24 threads with AVX2
WORKERS=24 node miner-fast.js

# Use scalar miner (no AVX2 support)
MINER_BIN=miner-c WORKERS=24 node miner-fast.js

# Check contract state without mining
node check-state.js
```

## Architecture

```
miner-fast.js (Node.js)
    |
    |-- Fetches challenge + difficulty from contract
    |-- Spawns C miner binary
    |-- Parses found nonce from stdout
    |-- Submits mine() transaction
    |
    v
miner-avx2 / miner-c (C binary)
    |
    |-- Multi-threaded Keccak-256 hashing
    |-- AVX2: 4 nonces per thread per cycle
    |-- Outputs found nonce to stdout
```

## File Structure

```
miner-avx2.c    # AVX2 4-way parallel Keccak-256 miner (recommended)
miner.c         # Scalar Keccak-256 miner (fallback)
miner-fast.js   # Node.js orchestrator (TX submission)
check-state.js  # Utility: check contract mining state
.env.example    # Environment config template
package.json    # Node.js dependencies
```

## Contract

- Address: `0xAC7b5d06fa1e77D08aea40d46cB7C5923A87A0cc`
- Token: HASH256
- Reward: 100 HASH per valid nonce
- Algorithm: `keccak256(abi.encodePacked(challenge, nonce)) < difficulty`

## License

MIT
