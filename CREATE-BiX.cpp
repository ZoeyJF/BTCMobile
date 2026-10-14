#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <vector>
#include <algorithm>
#include <sys/stat.h>
#include <unistd.h>
#include "Secp256k1Tool/bloom.h"

static const char* BASE58_ALPHABET = "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";

static bool base58_to_hash160(const char* b58_str, uint8_t* hash160_out) {
    size_t len = strlen(b58_str);
    if (len == 0 || len > 50) return false;

    std::vector<uint8_t> bytes((len * 733) / 1000 + 1, 0);
    for (size_t i = 0; i < len; ++i) {
        const char* p = strchr(BASE58_ALPHABET, b58_str[i]);
        if (!p) return false;
        int carry = (int)(p - BASE58_ALPHABET);
        for (size_t j = 0; j < bytes.size(); ++j) {
            carry += (int)bytes[bytes.size() - 1 - j] * 58;
            bytes[bytes.size() - 1 - j] = (uint8_t)(carry % 256);
            carry /= 256;
        }
    }

    size_t leading_zeros = 0;
    while (leading_zeros < len && b58_str[leading_zeros] == '1') {
        leading_zeros++;
    }

    size_t first_non_zero = 0;
    while (first_non_zero < bytes.size() && bytes[first_non_zero] == 0) {
        first_non_zero++;
    }

    size_t total_payload = leading_zeros + (bytes.size() - first_non_zero);
    if (total_payload != 25) return false;

    std::vector<uint8_t> payload(25, 0);
    size_t write_pos = leading_zeros;
    for (size_t i = first_non_zero; i < bytes.size(); ++i) {
        payload[write_pos++] = bytes[i];
    }

    memcpy(hash160_out, payload.data() + 1, 20);
    return true;
}

static bool hex_to_hash160(const char* hex_str, uint8_t* hash160_out) {
    size_t len = strlen(hex_str);
    if (len != 40) return false;
    for (size_t i = 0; i < 40; ++i) {
        char c = hex_str[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) {
            return false;
        }
    }
    for (size_t i = 0; i < 20; ++i) {
        char buf[3] = { hex_str[i * 2], hex_str[i * 2 + 1], 0 };
        hash160_out[i] = (uint8_t)strtoul(buf, NULL, 16);
    }
    return true;
}

struct Hash20 {
    uint8_t data[20];
    bool operator<(const Hash20& other) const {
        return memcmp(data, other.data, 20) < 0;
    }
    bool operator==(const Hash20& other) const {
        return memcmp(data, other.data, 20) == 0;
    }
};

static void print_file_size_estimation(uint64_t count, int option) {
    uint64_t bin_size = count * 20;
    uint64_t idx_size = count * 4;
    uint64_t bloom_bits = (option == 1) ? count * 16 : count * 24;
    if (bloom_bits < 64) bloom_bits = 64;
    uint64_t xor_size = (bloom_bits + 7) / 8;

    printf("\nEstimated Output Sizes (%llu entries):\n", (unsigned long long)count);
    printf("  BTC.bin : %.2f MB (%llu bytes)\n", (double)bin_size / (1024.0 * 1024.0), (unsigned long long)bin_size);
    printf("  BTC.xor : %.2f MB (%llu bytes)\n", (double)xor_size / (1024.0 * 1024.0), (unsigned long long)xor_size);
    printf("  BTC.idx : %.2f MB (%llu bytes)\n", (double)idx_size / (1024.0 * 1024.0), (unsigned long long)idx_size);
    printf("  Total   : %.2f MB\n\n", (double)(bin_size + xor_size + idx_size) / (1024.0 * 1024.0));
}

int main(int argc, char** argv) {
    if (argc < 2) {
        printf("Usage: %s <input_file.txt | input_file.hex>\n", argv[0]);
        return 1;
    }

    const char* filename = argv[1];
    FILE* f = fopen(filename, "r");
    if (!f) {
        printf("Error: Cannot open input file %s\n", filename);
        return 1;
    }

    printf("CREATE-BiX Tool\n");
    printf("Parsing input file: %s\n", filename);

    std::vector<Hash20> hashes;
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        char* token = strtok(line, " \t\r\n");
        while (token) {
            Hash20 h;
            if (hex_to_hash160(token, h.data)) {
                hashes.push_back(h);
            } else if (base58_to_hash160(token, h.data)) {
                hashes.push_back(h);
            }
            token = strtok(NULL, " \t\r\n");
        }
    }
    fclose(f);

    if (hashes.empty()) {
        printf("Error: No valid 20-byte Hash160 entries found.\n");
        return 1;
    }

    std::sort(hashes.begin(), hashes.end());
    hashes.erase(std::unique(hashes.begin(), hashes.end()), hashes.end());
    uint64_t total_entries = hashes.size();

    printf("Extracted and sorted %llu unique 20-byte Hash160 addresses.\n\n", (unsigned long long)total_entries);
    printf("Select Creation Sizing Option:\n");
    printf("1. Standard Sizing (8+8+8+4 - High compression & low RAM overhead)\n");
    printf("2. Extended Sizing (High precision Bloom filter & fast search)\n");
    printf("Enter choice (1 or 2): ");

    int option = 1;
    if (scanf("%d", &option) != 1) {
        option = 1;
    }

    print_file_size_estimation(total_entries, option);

    FILE* fbin = fopen("BTC.bin", "wb");
    if (!fbin) {
        printf("Error: Cannot create BTC.bin\n");
        return 1;
    }

    struct bloom bm;
    double fp_rate = (option == 1) ? 0.001 : 0.0001;
    uint64_t bloom_entries = total_entries < 1000 ? 1000 : total_entries;
    if (bloom_init2(&bm, bloom_entries, fp_rate) != 0) {
        printf("Error initializing bloom filter.\n");
        fclose(fbin);
        return 1;
    }

    FILE* fidx = fopen("BTC.idx", "wb");
    if (!fidx) {
        printf("Error: Cannot create BTC.idx\n");
        fclose(fbin);
        bloom_free(&bm);
        return 1;
    }

    for (size_t i = 0; i < hashes.size(); ++i) {
        fwrite(hashes[i].data, 1, 20, fbin);
        bloom_add(&bm, hashes[i].data, 20);

        uint32_t idx_val = (uint32_t)i;
        fwrite(&idx_val, sizeof(uint32_t), 1, fidx);
    }

    fclose(fbin);
    fclose(fidx);

    FILE* fxor = fopen("BTC.xor", "wb");
    if (fxor) {
        uint64_t chunk_size = 8 * 1024 * 1024;
        uint64_t written = 0;
        while (written < bm.bytes) {
            uint64_t to_write = (bm.bytes - written > chunk_size) ? chunk_size : (bm.bytes - written);
            fwrite(bm.bf + written, 1, to_write, fxor);
            written += to_write;
        }
        fclose(fxor);
    }

    bloom_free(&bm);

    printf("Creation completed 100%% successfully.\n");
    printf("Generated BTC.bin, BTC.xor, and BTC.idx.\n");

#if defined(__linux__)
    sync();
#endif

    return 0;
}
