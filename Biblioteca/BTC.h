#ifndef BTC_H
#define BTC_H
#include <cstdint>
#include <cstdio>
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
int btc_binary_search_idx(const idx_table_t *table, const uint8_t hash[HASH160_LEN], uint64_t *record_index);
int btc_xor_contains(const xor_table_t *table, const uint8_t *key, uint64_t key_len);
#endif
