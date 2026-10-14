#include "BTC.h"
#include "xorfilter.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <sched.h>
#include <pthread.h>
int btc_load_idx(const char *idx_path, idx_table_t *table) {
    FILE *f = fopen(idx_path, "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size == 0 || size % IDX_RECORD_SIZE != 0) { fclose(f); return -1; }
    table->count = size / IDX_RECORD_SIZE;
    table->records = (idx_record_t*)malloc(size);
    if (!table->records) { fclose(f); return -1; }
    if (fread(table->records, 1, size, f) != (size_t)size) { free(table->records); fclose(f); return -1; }
    fclose(f);
    return 0;
}
void btc_free_idx(idx_table_t *table) {
    if (table->records) { free(table->records); table->records = NULL; }
    table->count = 0;
}
int btc_load_xor(const char *xor_path, xor_table_t *table) {
    FILE *f = fopen(xor_path, "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (file_size <= (long)sizeof(xor_header_t)) { fclose(f); return -1; }
    xor_header_t header;
    if (fread(&header, 1, sizeof(xor_header_t), f) != sizeof(xor_header_t)) { fclose(f); return -1; }
    if (header.magic != XOR_MAGIC || header.version != XOR_VERSION) { fclose(f); return -1; }
    table->seed = header.seed;
    table->block_length = header.block_length;
    table->fingerprint_bits = (uint8_t)header.fingerprint_bits;
    table->entry_count = header.entry_count;
    table->array_length = (uint64_t)(file_size - (long)sizeof(xor_header_t));
    table->fingerprints = (uint8_t*)malloc(table->array_length);
    if (!table->fingerprints) { fclose(f); return -1; }
    if (fread(table->fingerprints, 1, table->array_length, f) != table->array_length) {
        free(table->fingerprints);
        table->fingerprints = NULL;
        fclose(f);
        return -1;
    }
    fclose(f);
    return 0;
}
void btc_free_xor(xor_table_t *table) {
    if (table->fingerprints) { free(table->fingerprints); table->fingerprints = NULL; }
}
int btc_binary_search_idx(const idx_table_t *table, const uint8_t hash[HASH160_LEN], uint64_t *record_index) {
    int64_t left = 0, right = (int64_t)table->count - 1;
    while (left <= right) {
        int64_t mid = left + (right - left) / 2;
        int cmp = memcmp(table->records[mid].hash, hash, HASH160_LEN);
        if (cmp == 0) { *record_index = table->records[mid].record_index; return 1; }
        else if (cmp < 0) left = mid + 1;
        else right = mid - 1;
    }
    return 0;
}
int btc_xor_contains(const xor_table_t *table, const uint8_t *key, uint64_t key_len) {
    xorfilter_t xf;
    xf.fingerprints = table->fingerprints;
    xf.array_length = (uint32_t)table->array_length;
    xf.block_length = (uint32_t)table->block_length;
    xf.fingerprint_bits = table->fingerprint_bits;
    xf.seed = table->seed;
    xf.entry_count = table->entry_count;
    return xorfilter_contains(&xf, key, key_len);
}
int btc_confirm_bin(FILE *bin, uint64_t record_index, const uint8_t hash[HASH160_LEN]) {
    uint8_t buffer[HASH160_LEN];
    long offset = (long)(record_index * HASH160_LEN);
    if (fseek(bin, offset, SEEK_SET) != 0) return 0;
    if (fread(buffer, 1, HASH160_LEN, bin) != HASH160_LEN) return 0;
    return memcmp(buffer, hash, HASH160_LEN) == 0 ? 1 : 0;
}
int btc_detect_cores(int *total_cores, int *big_cores, int *little_cores) {
    long cores = sysconf(_SC_NPROCESSORS_ONLN);
    if (cores < 1) cores = 1;
    *total_cores = (int)cores;
    *big_cores = (int)(cores / 2);
    if (*big_cores < 1) *big_cores = 1;
    *little_cores = *total_cores - *big_cores;
    return 0;
}
int btc_pin_thread(int cpu_id) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(cpu_id, &cpuset);
    return pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
}
int btc_write_found(const char *priv_hex, const char *hash_hex, int range_bits, int subrange_id, const char *timestamp_utc) {
    FILE *f = fopen(FOUND_FILE, "a");
    if (!f) return -1;
    fprintf(f, "%s | %s | Range:%d | Sub:%d | %s\n", priv_hex, hash_hex, range_bits, subrange_id, timestamp_utc);
    fclose(f);
    return 0;
}
int btc_run_engine(config_t *cfg) { (void)cfg; return 0; }
