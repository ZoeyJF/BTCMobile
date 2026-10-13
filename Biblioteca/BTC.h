#ifndef BTC_H
#define BTC_H
#include <cstdint>
#include <cstdio>
#include <pthread.h>
#include <atomic>
#define HASH160_LEN        20
#define IDX_RECORD_SIZE    28
#define XOR_MAGIC          0x584F5246u
#define XOR_VERSION        1u
#define FOUND_FILE         "FOUND.txt"
#define BIN_FILE           "BTC.bin"
#define IDX_FILE           "BTC.idx"
#define XOR_FILE           "BTC.xor"
#define HEX_LINE_LEN       40
#pragma pack(push, 1)
typedef struct {
uint8_t  hash[HASH160_LEN];
uint64_t record_index;
} idx_record_t;
typedef struct {
uint32_t magic;
uint32_t version;
uint64_t seed;
uint32_t block_length;
uint32_t fingerprint_bits;
uint64_t entry_count;
} xor_header_t;
#pragma pack(pop)
typedef struct {
idx_record_t *records;
uint64_t      count;
} idx_table_t;
typedef struct {
uint8_t  *fingerprints;
uint64_t  array_length;
uint64_t  block_length;
uint8_t   fingerprint_bits;
uint64_t  seed;
uint64_t  entry_count;
} xor_table_t;
enum {
MODE_SEQ_FWD     = 1,
MODE_SEQ_BIDIR   = 2,
MODE_RANDOM      = 3,
MODE_PURE        = 4,
MODE_RANGE_BIDIR = 5
};
typedef int search_mode_t;
typedef struct {
search_mode_t mode;
int           range_bits;
int           subranges;
int           threads;
int           seconds_slot;
int           seconds_pure_switch;
int           pure_cycles_before_range_change;
uint64_t      range_start[4];
uint64_t      range_end[4];
} config_t;
typedef struct {
int               thread_id;
int               cpu_id;
config_t         *cfg;
idx_table_t      *idx;
xor_table_t      *xorf;
FILE             *bin;
pthread_mutex_t  *bin_mutex;
pthread_mutex_t  *found_mutex;
std::atomic<int> *stop;
} worker_ctx_t;
int  create_bix(const char *hex_path);
int  btc_load_idx(const char *idx_path, idx_table_t *table);
void btc_free_idx(idx_table_t *table);
int  btc_load_xor(const char *xor_path, xor_table_t *table);
void btc_free_xor(xor_table_t *table);
int  btc_binary_search_idx(const idx_table_t *table, const uint8_t hash[HASH160_LEN], uint64_t *record_index);
int  btc_xor_contains(const xor_table_t *table, const uint8_t *key, uint64_t key_len);
int  btc_confirm_bin(FILE *bin, uint64_t record_index, const uint8_t hash[HASH160_LEN]);
int  btc_detect_cores(int *total_cores, int *big_cores, int *little_cores);
int  btc_pin_thread(int cpu_id);
int  btc_write_found(const char *priv_hex, const char *hash_hex, int range_bits, int subrange_id, const char *timestamp_utc);
int  btc_run_engine(config_t *cfg);
#endif

