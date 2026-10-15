#include "BTC.h"
#include "xorfilter.h"
#include <cstring>
int btc_binary_search_idx(const idx_table_t *table, const uint8_t hash[HASH160_LEN], uint64_t *record_index) {
    if (!table || !table->records) return 0;
    int64_t left = 0, right = (int64_t)table->count - 1;
    while (left <= right) {
        int64_t mid = left + (right - left) / 2;
        int cmp = memcmp(table->records[mid].hash, hash, HASH160_LEN);
        if (cmp == 0) {
            if (record_index) *record_index = table->records[mid].record_index;
            return 1;
        } else if (cmp < 0) {
            left = mid + 1;
        } else {
            right = mid - 1;
        }
    }
    return 0;
}
int btc_xor_contains(const xor_table_t *table, const uint8_t *key, uint64_t key_len) {
    if (!table || !table->fingerprints) return 0;
    xorfilter_t xf;
    xf.fingerprints = table->fingerprints;
    xf.array_length = (uint32_t)table->array_length;
    xf.block_length = (uint32_t)table->block_length;
    xf.fingerprint_bits = table->fingerprint_bits;
    xf.seed = table->seed;
    xf.entry_count = table->entry_count;
    return xorfilter_contains(&xf, key, key_len);
}
