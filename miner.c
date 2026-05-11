#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>
#include <stdatomic.h>
#define ROT64(x,n) (((x)<<(n))|((x)>>(64-(n))))
static const uint64_t RC[24]={
  0x0000000000000001ULL,0x0000000000008082ULL,0x800000000000808AULL,
  0x8000000080008000ULL,0x000000000000808BULL,0x0000000080000001ULL,
  0x8000000080008081ULL,0x8000000000008009ULL,0x000000000000008AULL,
  0x0000000000000088ULL,0x0000000080008009ULL,0x000000008000000AULL,
  0x000000008000808BULL,0x800000000000008BULL,0x8000000000008089ULL,
  0x8000000000008003ULL,0x8000000000008002ULL,0x8000000000000080ULL,
  0x000000000000800AULL,0x800000008000000AULL,0x8000000080008081ULL,
  0x8000000000008080ULL,0x0000000080000001ULL,0x8000000080008008ULL
};
static inline void keccak_f1600(uint64_t s[25]){
  uint64_t C0,C1,C2,C3,C4,D0,D1,D2,D3,D4,t,t0,t1;
  for(int r=0;r<24;r++){
    /* Theta */
    C0=s[0]^s[5]^s[10]^s[15]^s[20];
    C1=s[1]^s[6]^s[11]^s[16]^s[21];
    C2=s[2]^s[7]^s[12]^s[17]^s[22];
    C3=s[3]^s[8]^s[13]^s[18]^s[23];
    C4=s[4]^s[9]^s[14]^s[19]^s[24];
    D0=C4^ROT64(C1,1);D1=C0^ROT64(C2,1);D2=C1^ROT64(C3,1);
    D3=C2^ROT64(C4,1);D4=C3^ROT64(C0,1);
    s[0]^=D0;s[5]^=D0;s[10]^=D0;s[15]^=D0;s[20]^=D0;
    s[1]^=D1;s[6]^=D1;s[11]^=D1;s[16]^=D1;s[21]^=D1;
    s[2]^=D2;s[7]^=D2;s[12]^=D2;s[17]^=D2;s[22]^=D2;
    s[3]^=D3;s[8]^=D3;s[13]^=D3;s[18]^=D3;s[23]^=D3;
    s[4]^=D4;s[9]^=D4;s[14]^=D4;s[19]^=D4;s[24]^=D4;
    /* Rho + Pi (in-place permutation chain) */
    t=s[1];
    s[1]=ROT64(s[6],44);s[6]=ROT64(s[9],20);s[9]=ROT64(s[22],61);
    s[22]=ROT64(s[14],39);s[14]=ROT64(s[20],18);s[20]=ROT64(s[2],62);
    s[2]=ROT64(s[12],43);s[12]=ROT64(s[13],25);s[13]=ROT64(s[19],8);
    s[19]=ROT64(s[23],56);s[23]=ROT64(s[15],41);s[15]=ROT64(s[4],27);
    s[4]=ROT64(s[24],14);s[24]=ROT64(s[21],2);s[21]=ROT64(s[8],55);
    s[8]=ROT64(s[16],45);s[16]=ROT64(s[5],36);s[5]=ROT64(s[3],28);
    s[3]=ROT64(s[18],21);s[18]=ROT64(s[17],15);s[17]=ROT64(s[11],10);
    s[11]=ROT64(s[7],6);s[7]=ROT64(s[10],3);s[10]=ROT64(t,1);
    /* Chi - must preserve row values before modifying */
    t0=s[0];t1=s[1];s[0]^=(~s[1]&s[2]);s[1]^=(~s[2]&s[3]);s[2]^=(~s[3]&s[4]);s[3]^=(~s[4]&t0);s[4]^=(~t0&t1);
    t0=s[5];t1=s[6];s[5]^=(~s[6]&s[7]);s[6]^=(~s[7]&s[8]);s[7]^=(~s[8]&s[9]);s[8]^=(~s[9]&t0);s[9]^=(~t0&t1);
    t0=s[10];t1=s[11];s[10]^=(~s[11]&s[12]);s[11]^=(~s[12]&s[13]);s[12]^=(~s[13]&s[14]);s[13]^=(~s[14]&t0);s[14]^=(~t0&t1);
    t0=s[15];t1=s[16];s[15]^=(~s[16]&s[17]);s[16]^=(~s[17]&s[18]);s[17]^=(~s[18]&s[19]);s[18]^=(~s[19]&t0);s[19]^=(~t0&t1);
    t0=s[20];t1=s[21];s[20]^=(~s[21]&s[22]);s[21]^=(~s[22]&s[23]);s[22]^=(~s[23]&s[24]);s[23]^=(~s[24]&t0);s[24]^=(~t0&t1);
    /* Iota */
    s[0]^=RC[r];
  }
}
static inline int try_nonce(const uint64_t ch[4],uint64_t nonce,const uint64_t diff_be[4],uint8_t *hash_out){
  uint64_t s[25]={0};
  s[0]=ch[0];s[1]=ch[1];s[2]=ch[2];s[3]=ch[3];
  s[7]=__builtin_bswap64(nonce);
  s[8]=0x0000000000000001ULL;
  s[16]=0x8000000000000000ULL;
  keccak_f1600(s);
  uint64_t h0=__builtin_bswap64(s[0]);
  uint64_t h1=__builtin_bswap64(s[1]);
  uint64_t h2=__builtin_bswap64(s[2]);
  uint64_t h3=__builtin_bswap64(s[3]);
  if(hash_out){
    for(int b=0;b<8;b++) hash_out[   b]=(s[0]>>(b*8))&0xFF;
    for(int b=0;b<8;b++) hash_out[ 8+b]=(s[1]>>(b*8))&0xFF;
    for(int b=0;b<8;b++) hash_out[16+b]=(s[2]>>(b*8))&0xFF;
    for(int b=0;b<8;b++) hash_out[24+b]=(s[3]>>(b*8))&0xFF;
  }
  if(h0<diff_be[0]) return 1; if(h0>diff_be[0]) return 0;
  if(h1<diff_be[1]) return 1; if(h1>diff_be[1]) return 0;
  if(h2<diff_be[2]) return 1; if(h2>diff_be[2]) return 0;
  return h3<diff_be[3];
}
typedef struct{uint64_t ch[4];uint64_t diff_be[4];uint64_t start_nonce;int num_threads;int thread_id;}WorkerArgs;
static atomic_int found_flag=0;
static uint64_t found_nonce=0;
static uint8_t found_hash[32];
static atomic_llong total_hashes=0;
static void *worker(void *arg){
  WorkerArgs *a=(WorkerArgs*)arg;
  uint64_t nonce=a->start_nonce+(uint64_t)a->thread_id;
  uint64_t step=(uint64_t)a->num_threads;
  long long count=0;
  uint8_t hash[32];
  while(!atomic_load_explicit(&found_flag,memory_order_relaxed)){
    if(try_nonce(a->ch,nonce,a->diff_be,hash)){
      if(!atomic_exchange(&found_flag,1)){found_nonce=nonce;memcpy(found_hash,hash,32);}
      break;
    }
    nonce+=step;
    if(++count%2000000==0) atomic_fetch_add(&total_hashes,2000000);
  }
  return NULL;
}
static void *stats_thread(void *arg){
  (void)arg;
  long long prev=0;
  while(!atomic_load(&found_flag)){
    sleep(2);
    long long cur=atomic_load(&total_hashes);
    double mhs=(cur-prev)/2.0/1e6;
    prev=cur;
    printf("\r⛏  %.1fM hash/s | %.0fM total checked...",mhs,cur/1e6);
    fflush(stdout);
  }
  return NULL;
}
static void hex_to_le_words(const char *hex,uint64_t out[4]){
  const char *h=(hex[0]=='0'&&hex[1]=='x')?hex+2:hex;
  uint8_t bytes[32];
  for(int i=0;i<32;i++){unsigned v;sscanf(h+i*2,"%02x",&v);bytes[i]=(uint8_t)v;}
  for(int w=0;w<4;w++){out[w]=0;for(int b=0;b<8;b++)out[w]|=(uint64_t)bytes[w*8+b]<<(b*8);}
}
static void dec_to_be_words(const char *dec,uint64_t out[4]){
  uint8_t tmp[32]={0};
  for(int i=0;dec[i];i++){
    int carry=dec[i]-'0';
    for(int j=31;j>=0;j--){int val=tmp[j]*10+carry;tmp[j]=val&0xFF;carry=val>>8;}
  }
  for(int w=0;w<4;w++){out[w]=0;for(int b=0;b<8;b++)out[w]|=(uint64_t)tmp[w*8+b]<<((7-b)*8);}
}
int main(int argc,char *argv[]){
  if(argc<4){fprintf(stderr,"Usage: %s <challenge_hex> <difficulty_dec> <num_threads>\n",argv[0]);return 1;}
  uint64_t ch[4],diff_be[4];
  hex_to_le_words(argv[1],ch);
  dec_to_be_words(argv[2],diff_be);
  int num_threads=atoi(argv[3]);
  srand((unsigned)time(NULL)^(unsigned)getpid());
  uint64_t start=((uint64_t)rand()<<32)|(uint64_t)rand();
  printf("🚀 C Miner v2 | %d threads | nonce start: %llu\n",num_threads,(unsigned long long)start);
  fflush(stdout);
  pthread_t *thr=malloc(sizeof(pthread_t)*num_threads);
  WorkerArgs *args=malloc(sizeof(WorkerArgs)*num_threads);
  pthread_t stid;
  pthread_create(&stid,NULL,stats_thread,NULL);
  for(int i=0;i<num_threads;i++){
    memcpy(args[i].ch,ch,sizeof(ch));
    memcpy(args[i].diff_be,diff_be,sizeof(diff_be));
    args[i].start_nonce=start;args[i].num_threads=num_threads;args[i].thread_id=i;
    pthread_create(&thr[i],NULL,worker,&args[i]);
  }
  for(int i=0;i<num_threads;i++) pthread_join(thr[i],NULL);
  atomic_store(&found_flag,1);
  pthread_join(stid,NULL);
  printf("\n✅ FOUND nonce : %llu\n",(unsigned long long)found_nonce);
  printf("   Hash        : 0x");
  for(int i=0;i<32;i++) printf("%02x",found_hash[i]);
  printf("\n");
  fflush(stdout);
  free(thr);free(args);
  return 0;
}
