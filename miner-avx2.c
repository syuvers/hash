/*
 * miner-avx2.c — AVX2 4-way parallel Keccak-256 miner
 * Processes 4 nonces simultaneously per thread using 256-bit SIMD.
 *
 * Compile: gcc -O3 -mavx2 -o miner-avx2 miner-avx2.c -lpthread
 * Usage:   ./miner-avx2 <challenge_hex> <difficulty_dec> <num_threads>
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>
#include <stdatomic.h>
#include <immintrin.h>

/* ============================================================
 * AVX2 Keccak-f[1600] — 4-way parallel (4 states at once)
 * Each __m256i holds 4 x uint64 lanes (one from each state)
 * ============================================================ */

static const uint64_t RC[24] = {
  0x0000000000000001ULL,0x0000000000008082ULL,0x800000000000808AULL,
  0x8000000080008000ULL,0x000000000000808BULL,0x0000000080000001ULL,
  0x8000000080008081ULL,0x8000000000008009ULL,0x000000000000008AULL,
  0x0000000000000088ULL,0x0000000080008009ULL,0x000000008000000AULL,
  0x000000008000808BULL,0x800000000000008BULL,0x8000000000008089ULL,
  0x8000000000008003ULL,0x8000000000008002ULL,0x8000000000000080ULL,
  0x000000000000800AULL,0x800000008000000AULL,0x8000000080008081ULL,
  0x8000000000008080ULL,0x0000000080000001ULL,0x8000000080008008ULL
};

/* Rotate left 64-bit within each lane of __m256i */
static inline __m256i rot64_avx2(__m256i x, int n) {
  return _mm256_or_si256(_mm256_slli_epi64(x, n), _mm256_srli_epi64(x, 64 - n));
}

/* NOT: ~x = XOR with all-ones */
static inline __m256i not256(__m256i x) {
  return _mm256_xor_si256(x, _mm256_set1_epi64x(-1LL));
}

/* ANDNOT: (~a) & b */
static inline __m256i andnot256(__m256i a, __m256i b) {
  return _mm256_andnot_si256(a, b);  /* ~a & b */
}

static void keccak_f1600_avx2(__m256i s[25]) {
  __m256i C0, C1, C2, C3, C4, D0, D1, D2, D3, D4, t, t0, t1;

  for (int r = 0; r < 24; r++) {
    /* Theta */
    C0 = _mm256_xor_si256(_mm256_xor_si256(_mm256_xor_si256(s[0], s[5]), _mm256_xor_si256(s[10], s[15])), s[20]);
    C1 = _mm256_xor_si256(_mm256_xor_si256(_mm256_xor_si256(s[1], s[6]), _mm256_xor_si256(s[11], s[16])), s[21]);
    C2 = _mm256_xor_si256(_mm256_xor_si256(_mm256_xor_si256(s[2], s[7]), _mm256_xor_si256(s[12], s[17])), s[22]);
    C3 = _mm256_xor_si256(_mm256_xor_si256(_mm256_xor_si256(s[3], s[8]), _mm256_xor_si256(s[13], s[18])), s[23]);
    C4 = _mm256_xor_si256(_mm256_xor_si256(_mm256_xor_si256(s[4], s[9]), _mm256_xor_si256(s[14], s[19])), s[24]);

    D0 = _mm256_xor_si256(C4, rot64_avx2(C1, 1));
    D1 = _mm256_xor_si256(C0, rot64_avx2(C2, 1));
    D2 = _mm256_xor_si256(C1, rot64_avx2(C3, 1));
    D3 = _mm256_xor_si256(C2, rot64_avx2(C4, 1));
    D4 = _mm256_xor_si256(C3, rot64_avx2(C0, 1));

    s[0]  = _mm256_xor_si256(s[0],  D0); s[5]  = _mm256_xor_si256(s[5],  D0);
    s[10] = _mm256_xor_si256(s[10], D0); s[15] = _mm256_xor_si256(s[15], D0);
    s[20] = _mm256_xor_si256(s[20], D0);
    s[1]  = _mm256_xor_si256(s[1],  D1); s[6]  = _mm256_xor_si256(s[6],  D1);
    s[11] = _mm256_xor_si256(s[11], D1); s[16] = _mm256_xor_si256(s[16], D1);
    s[21] = _mm256_xor_si256(s[21], D1);
    s[2]  = _mm256_xor_si256(s[2],  D2); s[7]  = _mm256_xor_si256(s[7],  D2);
    s[12] = _mm256_xor_si256(s[12], D2); s[17] = _mm256_xor_si256(s[17], D2);
    s[22] = _mm256_xor_si256(s[22], D2);
    s[3]  = _mm256_xor_si256(s[3],  D3); s[8]  = _mm256_xor_si256(s[8],  D3);
    s[13] = _mm256_xor_si256(s[13], D3); s[18] = _mm256_xor_si256(s[18], D3);
    s[23] = _mm256_xor_si256(s[23], D3);
    s[4]  = _mm256_xor_si256(s[4],  D4); s[9]  = _mm256_xor_si256(s[9],  D4);
    s[14] = _mm256_xor_si256(s[14], D4); s[19] = _mm256_xor_si256(s[19], D4);
    s[24] = _mm256_xor_si256(s[24], D4);

    /* Rho + Pi (in-place permutation chain) */
    t = s[1];
    s[1]  = rot64_avx2(s[6],  44); s[6]  = rot64_avx2(s[9],  20);
    s[9]  = rot64_avx2(s[22], 61); s[22] = rot64_avx2(s[14], 39);
    s[14] = rot64_avx2(s[20], 18); s[20] = rot64_avx2(s[2],  62);
    s[2]  = rot64_avx2(s[12], 43); s[12] = rot64_avx2(s[13], 25);
    s[13] = rot64_avx2(s[19], 8);  s[19] = rot64_avx2(s[23], 56);
    s[23] = rot64_avx2(s[15], 41); s[15] = rot64_avx2(s[4],  27);
    s[4]  = rot64_avx2(s[24], 14); s[24] = rot64_avx2(s[21], 2);
    s[21] = rot64_avx2(s[8],  55); s[8]  = rot64_avx2(s[16], 45);
    s[16] = rot64_avx2(s[5],  36); s[5]  = rot64_avx2(s[3],  28);
    s[3]  = rot64_avx2(s[18], 21); s[18] = rot64_avx2(s[17], 15);
    s[17] = rot64_avx2(s[11], 10); s[11] = rot64_avx2(s[7],  6);
    s[7]  = rot64_avx2(s[10], 3);  s[10] = rot64_avx2(t,     1);

    /* Chi — save first two of each row */
    t0=s[0]; t1=s[1];
    s[0] = _mm256_xor_si256(s[0], andnot256(s[1], s[2]));
    s[1] = _mm256_xor_si256(s[1], andnot256(s[2], s[3]));
    s[2] = _mm256_xor_si256(s[2], andnot256(s[3], s[4]));
    s[3] = _mm256_xor_si256(s[3], andnot256(s[4], t0));
    s[4] = _mm256_xor_si256(s[4], andnot256(t0, t1));

    t0=s[5]; t1=s[6];
    s[5] = _mm256_xor_si256(s[5], andnot256(s[6], s[7]));
    s[6] = _mm256_xor_si256(s[6], andnot256(s[7], s[8]));
    s[7] = _mm256_xor_si256(s[7], andnot256(s[8], s[9]));
    s[8] = _mm256_xor_si256(s[8], andnot256(s[9], t0));
    s[9] = _mm256_xor_si256(s[9], andnot256(t0, t1));

    t0=s[10]; t1=s[11];
    s[10] = _mm256_xor_si256(s[10], andnot256(s[11], s[12]));
    s[11] = _mm256_xor_si256(s[11], andnot256(s[12], s[13]));
    s[12] = _mm256_xor_si256(s[12], andnot256(s[13], s[14]));
    s[13] = _mm256_xor_si256(s[13], andnot256(s[14], t0));
    s[14] = _mm256_xor_si256(s[14], andnot256(t0, t1));

    t0=s[15]; t1=s[16];
    s[15] = _mm256_xor_si256(s[15], andnot256(s[16], s[17]));
    s[16] = _mm256_xor_si256(s[16], andnot256(s[17], s[18]));
    s[17] = _mm256_xor_si256(s[17], andnot256(s[18], s[19]));
    s[18] = _mm256_xor_si256(s[18], andnot256(s[19], t0));
    s[19] = _mm256_xor_si256(s[19], andnot256(t0, t1));

    t0=s[20]; t1=s[21];
    s[20] = _mm256_xor_si256(s[20], andnot256(s[21], s[22]));
    s[21] = _mm256_xor_si256(s[21], andnot256(s[22], s[23]));
    s[22] = _mm256_xor_si256(s[22], andnot256(s[23], s[24]));
    s[23] = _mm256_xor_si256(s[23], andnot256(s[24], t0));
    s[24] = _mm256_xor_si256(s[24], andnot256(t0, t1));

    /* Iota */
    s[0] = _mm256_xor_si256(s[0], _mm256_set1_epi64x((long long)RC[r]));
  }
}

/* ============================================================
 * Try 4 nonces at once. Returns index (0-3) if found, -1 otherwise.
 * ============================================================ */
static inline int try_4nonces(const uint64_t ch[4], uint64_t nonces[4],
                              const uint64_t diff_be[4], uint8_t *hash_out) {
  __m256i s[25];

  /* Zero all state lanes */
  __m256i zero = _mm256_setzero_si256();
  for (int i = 0; i < 25; i++) s[i] = zero;

  /* Load challenge into s[0..3] — same for all 4 states */
  s[0] = _mm256_set1_epi64x((long long)ch[0]);
  s[1] = _mm256_set1_epi64x((long long)ch[1]);
  s[2] = _mm256_set1_epi64x((long long)ch[2]);
  s[3] = _mm256_set1_epi64x((long long)ch[3]);

  /* Load 4 different nonces into s[7] (each bswap64'd) */
  uint64_t n0 = __builtin_bswap64(nonces[0]);
  uint64_t n1 = __builtin_bswap64(nonces[1]);
  uint64_t n2 = __builtin_bswap64(nonces[2]);
  uint64_t n3 = __builtin_bswap64(nonces[3]);
  s[7] = _mm256_set_epi64x((long long)n3, (long long)n2, (long long)n1, (long long)n0);

  /* Padding */
  s[8]  = _mm256_set1_epi64x(0x0000000000000001LL);
  s[16] = _mm256_set1_epi64x((long long)0x8000000000000000ULL);

  /* Run Keccak-f[1600] */
  keccak_f1600_avx2(s);

  /* Extract and check each of the 4 results */
  uint64_t h0_arr[4], h1_arr[4], h2_arr[4], h3_arr[4];
  _mm256_storeu_si256((__m256i*)h0_arr, s[0]);
  _mm256_storeu_si256((__m256i*)h1_arr, s[1]);
  _mm256_storeu_si256((__m256i*)h2_arr, s[2]);
  _mm256_storeu_si256((__m256i*)h3_arr, s[3]);

  for (int i = 0; i < 4; i++) {
    uint64_t hh0 = __builtin_bswap64(h0_arr[i]);
    uint64_t hh1 = __builtin_bswap64(h1_arr[i]);
    uint64_t hh2 = __builtin_bswap64(h2_arr[i]);
    uint64_t hh3 = __builtin_bswap64(h3_arr[i]);

    int below = 0;
    if (hh0 < diff_be[0]) below = 1;
    else if (hh0 == diff_be[0]) {
      if (hh1 < diff_be[1]) below = 1;
      else if (hh1 == diff_be[1]) {
        if (hh2 < diff_be[2]) below = 1;
        else if (hh2 == diff_be[2]) {
          if (hh3 < diff_be[3]) below = 1;
        }
      }
    }

    if (below) {
      if (hash_out) {
        for (int b = 0; b < 8; b++) hash_out[   b] = (h0_arr[i] >> (b*8)) & 0xFF;
        for (int b = 0; b < 8; b++) hash_out[ 8+b] = (h1_arr[i] >> (b*8)) & 0xFF;
        for (int b = 0; b < 8; b++) hash_out[16+b] = (h2_arr[i] >> (b*8)) & 0xFF;
        for (int b = 0; b < 8; b++) hash_out[24+b] = (h3_arr[i] >> (b*8)) & 0xFF;
      }
      return i;
    }
  }
  return -1;
}

/* ============================================================
 * Worker threads and main
 * ============================================================ */
typedef struct {
  uint64_t ch[4];
  uint64_t diff_be[4];
  uint64_t start_nonce;
  int num_threads;
  int thread_id;
} WorkerArgs;

static atomic_int found_flag = 0;
static uint64_t found_nonce = 0;
static uint8_t found_hash[32];
static atomic_llong total_hashes = 0;

static void *worker(void *arg) {
  WorkerArgs *a = (WorkerArgs*)arg;
  /* Each thread works on nonces: start + thread_id, start + thread_id + num_threads, ... */
  /* With 4-way, each iteration does 4 consecutive nonces in the thread's stride */
  uint64_t base = a->start_nonce + (uint64_t)a->thread_id * 4;
  uint64_t step = (uint64_t)a->num_threads * 4;
  long long count = 0;
  uint64_t nonces[4];
  uint8_t hash[32];

  while (!atomic_load_explicit(&found_flag, memory_order_relaxed)) {
    nonces[0] = base;
    nonces[1] = base + 1;
    nonces[2] = base + 2;
    nonces[3] = base + 3;

    int idx = try_4nonces(a->ch, nonces, a->diff_be, hash);
    if (idx >= 0) {
      if (!atomic_exchange(&found_flag, 1)) {
        found_nonce = nonces[idx];
        memcpy(found_hash, hash, 32);
      }
      break;
    }

    base += step;
    count += 4;
    if (count % 2000000 == 0) atomic_fetch_add(&total_hashes, 2000000);
  }
  return NULL;
}

static void *stats_thread(void *arg) {
  (void)arg;
  long long prev = 0;
  while (!atomic_load(&found_flag)) {
    sleep(2);
    long long cur = atomic_load(&total_hashes);
    double mhs = (cur - prev) / 2.0 / 1e6;
    prev = cur;
    printf("\r⛏  %.1fM hash/s | %.0fM total checked...", mhs, cur / 1e6);
    fflush(stdout);
  }
  return NULL;
}

static void hex_to_le_words(const char *hex, uint64_t out[4]) {
  const char *h = (hex[0]=='0' && hex[1]=='x') ? hex+2 : hex;
  uint8_t bytes[32];
  for (int i = 0; i < 32; i++) { unsigned v; sscanf(h+i*2, "%02x", &v); bytes[i] = (uint8_t)v; }
  for (int w = 0; w < 4; w++) { out[w] = 0; for (int b = 0; b < 8; b++) out[w] |= (uint64_t)bytes[w*8+b] << (b*8); }
}

static void dec_to_be_words(const char *dec, uint64_t out[4]) {
  uint8_t tmp[32] = {0};
  for (int i = 0; dec[i]; i++) {
    int carry = dec[i] - '0';
    for (int j = 31; j >= 0; j--) { int val = tmp[j]*10 + carry; tmp[j] = val & 0xFF; carry = val >> 8; }
  }
  for (int w = 0; w < 4; w++) { out[w] = 0; for (int b = 0; b < 8; b++) out[w] |= (uint64_t)tmp[w*8+b] << ((7-b)*8); }
}

int main(int argc, char *argv[]) {
  if (argc < 4) {
    fprintf(stderr, "Usage: %s <challenge_hex> <difficulty_dec> <num_threads>\n", argv[0]);
    return 1;
  }

  uint64_t ch[4], diff_be[4];
  hex_to_le_words(argv[1], ch);
  dec_to_be_words(argv[2], diff_be);
  int num_threads = atoi(argv[3]);

  srand((unsigned)time(NULL) ^ (unsigned)getpid());
  uint64_t start = ((uint64_t)rand() << 32) | (uint64_t)rand();

  printf("🚀 C Miner v3 AVX2 | %d threads × 4-way SIMD | nonce start: %llu\n",
         num_threads, (unsigned long long)start);
  fflush(stdout);

  pthread_t *thr = malloc(sizeof(pthread_t) * num_threads);
  WorkerArgs *args = malloc(sizeof(WorkerArgs) * num_threads);
  pthread_t stid;
  pthread_create(&stid, NULL, stats_thread, NULL);

  for (int i = 0; i < num_threads; i++) {
    memcpy(args[i].ch, ch, sizeof(ch));
    memcpy(args[i].diff_be, diff_be, sizeof(diff_be));
    args[i].start_nonce = start;
    args[i].num_threads = num_threads;
    args[i].thread_id = i;
    pthread_create(&thr[i], NULL, worker, &args[i]);
  }

  for (int i = 0; i < num_threads; i++) pthread_join(thr[i], NULL);
  atomic_store(&found_flag, 1);
  pthread_join(stid, NULL);

  printf("\n✅ FOUND nonce : %llu\n", (unsigned long long)found_nonce);
  printf("   Hash        : 0x");
  for (int i = 0; i < 32; i++) printf("%02x", found_hash[i]);
  printf("\n");
  fflush(stdout);

  free(thr);
  free(args);
  return 0;
}
