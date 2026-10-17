#include "xorfilter.h"
#include <cstdlib>
#include <cstring>
#include <cstdint>

static inline uint64_t mix64(uint64_t x) {
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}

static inline uint64_t hash_key_seed(const uint8_t *key, uint64_t len, uint64_t seed) {
    uint64_t h = seed ^ 0x880355f21e6d1965ULL;
    for (uint64_t i = 0; i < len; i++) {
        h = (h ^ (uint64_t)key[i]) * 0x100000001b3ULL;
    }
    return mix64(h);
}

static inline void get_h0_h1_h2(const uint8_t *key, uint64_t seed, uint32_t block_len, uint32_t &h0, uint32_t &h1, uint32_t &h2, uint8_t &fp) {
    uint64_t h = hash_key_seed(key, 20, seed);
    uint64_t h1_val = mix64(h ^ 0x9e3779b97f4a7c15ULL);
    uint64_t h2_val = mix64(h ^ 0xbf58476d1ce4e5b9ULL);

    h0 = (uint32_t)(h % block_len);
    h1 = block_len + (uint32_t)(h1_val % block_len);
    h2 = 2 * block_len + (uint32_t)(h2_val % block_len);
    fp = (uint8_t)(mix64(h ^ h1_val ^ h2_val) & 0xFF);
}

int xorfilter_build(xorfilter_t *f, const uint8_t *keys, uint64_t n_keys, uint8_t fingerprint_bits) {
    if (!f || !keys || n_keys == 0) return -1;
    (void)fingerprint_bits;
    uint32_t block_len = (uint32_t)((1.23 * (double)n_keys + 32.0) / 3.0);
    if (block_len < 32) block_len = 32;
    uint32_t total_len = block_len * 3;

    uint8_t *counts = (uint8_t*)malloc(total_len * sizeof(uint8_t));
    uint64_t *xors = (uint64_t*)malloc(total_len * sizeof(uint64_t));
    uint32_t *queue = (uint32_t*)malloc(total_len * sizeof(uint32_t));
    uint64_t *stack_keys = (uint64_t*)malloc(n_keys * sizeof(uint64_t));
    uint32_t *stack_slots = (uint32_t*)malloc(n_keys * sizeof(uint32_t));

    if (!counts || !xors || !queue || !stack_keys || !stack_slots) {
        if (counts) free(counts);
        if (xors) free(xors);
        if (queue) free(queue);
        if (stack_keys) free(stack_keys);
        if (stack_slots) free(stack_slots);
        return -1;
    }

    uint64_t seed = 0x9e3779b97f4a7c15ULL;
    int success = 0;

    for (int attempt = 0; attempt < 1000; attempt++) {
        memset(counts, 0, total_len * sizeof(uint8_t));
        memset(xors, 0, total_len * sizeof(uint64_t));

        for (uint64_t i = 0; i < n_keys; i++) {
            const uint8_t *k = keys + i * 20;
            uint32_t h0, h1, h2;
            uint8_t fp;
            get_h0_h1_h2(k, seed, block_len, h0, h1, h2, fp);

            counts[h0]++;
            counts[h1]++;
            counts[h2]++;

            xors[h0] ^= i;
            xors[h1] ^= i;
            xors[h2] ^= i;
        }

        uint32_t q_head = 0, q_tail = 0;
        for (uint32_t s = 0; s < total_len; s++) {
            if (counts[s] == 1) {
                queue[q_tail++] = s;
            }
        }

        uint64_t stack_pos = 0;
        while (q_head < q_tail) {
            uint32_t s = queue[q_head++];
            if (counts[s] == 1) {
                uint64_t key_idx = xors[s];
                stack_keys[stack_pos] = key_idx;
                stack_slots[stack_pos] = s;
                stack_pos++;

                const uint8_t *k = keys + key_idx * 20;
                uint32_t h0, h1, h2;
                uint8_t fp;
                get_h0_h1_h2(k, seed, block_len, h0, h1, h2, fp);

                counts[h0]--; xors[h0] ^= key_idx; if (counts[h0] == 1) queue[q_tail++] = h0;
                counts[h1]--; xors[h1] ^= key_idx; if (counts[h1] == 1) queue[q_tail++] = h1;
                counts[h2]--; xors[h2] ^= key_idx; if (counts[h2] == 1) queue[q_tail++] = h2;
            }
        }

        if (stack_pos == n_keys) {
            success = 1;
            break;
        }

        seed += 0x9e3779b97f4a7c15ULL;
    }

    if (!success) {
        free(counts);
        free(xors);
        free(queue);
        free(stack_keys);
        free(stack_slots);
        return -1;
    }

    uint8_t *fp_array = (uint8_t*)calloc(total_len, sizeof(uint8_t));
    if (!fp_array) {
        free(counts);
        free(xors);
        free(queue);
        free(stack_keys);
        free(stack_slots);
        return -1;
    }

    for (int64_t i = (int64_t)n_keys - 1; i >= 0; i--) {
        uint64_t key_idx = stack_keys[i];
        uint32_t s = stack_slots[i];

        const uint8_t *k = keys + key_idx * 20;
        uint32_t h0, h1, h2;
        uint8_t target_fp;
        get_h0_h1_h2(k, seed, block_len, h0, h1, h2, target_fp);

        fp_array[s] = target_fp ^ fp_array[h0] ^ fp_array[h1] ^ fp_array[h2];
    }

    free(counts);
    free(xors);
    free(queue);
    free(stack_keys);
    free(stack_slots);

    f->fingerprints = fp_array;
    f->array_length = total_len;
    f->block_length = block_len;
    f->fingerprint_bits = 8;
    f->seed = seed;
    f->entry_count = n_keys;

    return 0;
}

int xorfilter_contains(const xorfilter_t *f, const uint8_t *key, uint64_t key_len) {
    if (!f || !f->fingerprints || f->block_length == 0) return 0;
    (void)key_len;
    uint32_t block_len = f->block_length;
    uint32_t h0, h1, h2;
    uint8_t target_fp;
    get_h0_h1_h2(key, f->seed, block_len, h0, h1, h2, target_fp);
    uint8_t res = f->fingerprints[h0] ^ f->fingerprints[h1] ^ f->fingerprints[h2];
    return (res == target_fp) ? 1 : 0;
}

void xorfilter_free(xorfilter_t *f) {
    if (!f) return;
    if (f->fingerprints) {
        free(f->fingerprints);
        f->fingerprints = nullptr;
    }
    f->array_length = 0;
    f->block_length = 0;
    f->entry_count = 0;
}
