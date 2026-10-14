#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>
#include <unistd.h>
#include <fcntl.h>
#include <malloc.h>

typedef struct {
    uint8_t data[20];
} Hash160;

static int compare_hash160(const void *a, const void *b) {
    return memcmp(((const Hash160 *)a)->data, ((const Hash160 *)b)->data, 20);
}

static int hex_val(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static int parse_hex20(const char *str, uint8_t *out) {
    int count = 0;
    while (*str && count < 20) {
        while (*str && !isxdigit((unsigned char)*str)) str++;
        if (!*str) break;
        int hi = hex_val(*str++);
        if (!*str || !isxdigit((unsigned char)*str)) break;
        int lo = hex_val(*str++);
        out[count++] = (uint8_t)((hi << 4) | lo);
    }
    return (count == 20);
}

static uint64_t fnv1a_64(const uint8_t *data, size_t len) {
    uint64_t hash = 14695981039346656037ULL;
    for (size_t i = 0; i < len; i++) {
        hash ^= data[i];
        hash *= 1099511628211ULL;
    }
    return hash;
}

int main(int argc, char **argv) {
    char input_filename[512];
    if (argc > 1) {
        strncpy(input_filename, argv[1], sizeof(input_filename) - 1);
        input_filename[sizeof(input_filename) - 1] = '\0';
    } else {
        printf("Enter input filename (.txt or .hex): ");
        if (scanf("%511s", input_filename) != 1) {
            printf("Error reading input filename.\n");
            return 1;
        }
    }

    FILE *fp = fopen(input_filename, "r");
    if (!fp) {
        printf("Error opening file %s\n", input_filename);
        return 1;
    }

    size_t capacity = 1024;
    size_t count = 0;
    Hash160 *hashes = (Hash160 *)malloc(capacity * sizeof(Hash160));
    if (!hashes) {
        fclose(fp);
        return 1;
    }

    char line[1024];
    while (fgets(line, sizeof(line), fp)) {
        Hash160 h;
        if (parse_hex20(line, h.data)) {
            if (count >= capacity) {
                capacity *= 2;
                Hash160 *tmp = (Hash160 *)realloc(hashes, capacity * sizeof(Hash160));
                if (!tmp) {
                    free(hashes);
                    fclose(fp);
                    return 1;
                }
                hashes = tmp;
            }
            hashes[count++] = h;
        }
    }
    fclose(fp);

    printf("\n=== CREATE-BiX Database Creation Menu ===\n");
    printf("Extracted valid Hash160 targets: %lu\n\n", (unsigned long)count);
    printf("Option 1: Compact Structure\n");
    printf("  - BTC.bin: 8-byte hash payload per entry\n");
    printf("  - BTC.idx: 8-byte offset index\n");
    printf("  - BTC.xor: 8-byte Bloom filter chunk\n");
    printf("  - Header: 4-byte metadata\n");
    uint64_t est1_bin = 4 + count * 8;
    uint64_t est1_idx = 4 + count * 8;
    uint64_t est1_xor = 4 + count * 8;
    printf("  Estimated sizes: BTC.bin=%lu bytes, BTC.idx=%lu bytes, BTC.xor=%lu bytes (Total: %lu bytes)\n\n",
           (unsigned long)est1_bin, (unsigned long)est1_idx, (unsigned long)est1_xor,
           (unsigned long)(est1_bin + est1_idx + est1_xor));

    printf("Option 2: Full Structure\n");
    printf("  - BTC.bin: Full 20-byte hash payload per entry\n");
    printf("  - BTC.idx: 8-byte offset index\n");
    printf("  - BTC.xor: 8-byte Bloom filter chunk\n");
    printf("  - Header: 4-byte metadata\n");
    uint64_t est2_bin = 4 + count * 20;
    uint64_t est2_idx = 4 + count * 8;
    uint64_t est2_xor = 4 + count * 8;
    printf("  Estimated sizes: BTC.bin=%lu bytes, BTC.idx=%lu bytes, BTC.xor=%lu bytes (Total: %lu bytes)\n\n",
           (unsigned long)est2_bin, (unsigned long)est2_idx, (unsigned long)est2_xor,
           (unsigned long)(est2_bin + est2_idx + est2_xor));

    int option = 1;
    if (argc > 2) {
        option = atoi(argv[2]);
    } else {
        printf("Select option (1 or 2): ");
        if (scanf("%d", &option) != 1 || (option != 1 && option != 2)) {
            option = 1;
        }
    }

    qsort(hashes, count, sizeof(Hash160), compare_hash160);

    size_t payload_size = (option == 1) ? 8 : 20;

    FILE *f_bin = fopen("BTC.bin", "wb");
    FILE *f_idx = fopen("BTC.idx", "wb");
    FILE *f_xor = fopen("BTC.xor", "wb");

    if (!f_bin || !f_idx || !f_xor) {
        printf("Error creating output binary files.\n");
        if (f_bin) fclose(f_bin);
        if (f_idx) fclose(f_idx);
        if (f_xor) fclose(f_xor);
        free(hashes);
        return 1;
    }

    uint32_t header_val = (uint32_t)option;
    fwrite(&header_val, sizeof(uint32_t), 1, f_bin);
    fwrite(&header_val, sizeof(uint32_t), 1, f_idx);
    fwrite(&header_val, sizeof(uint32_t), 1, f_xor);

    for (size_t i = 0; i < count; i++) {
        fwrite(hashes[i].data, 1, payload_size, f_bin);

        uint64_t idx_val = (uint64_t)i * payload_size;
        fwrite(&idx_val, sizeof(uint64_t), 1, f_idx);

        uint64_t xor_val = fnv1a_64(hashes[i].data, 20);
        fwrite(&xor_val, sizeof(uint64_t), 1, f_xor);
    }

    fflush(f_bin);
    fflush(f_idx);
    fflush(f_xor);

    int fd_bin = fileno(f_bin);
    int fd_idx = fileno(f_idx);
    int fd_xor = fileno(f_xor);

#ifdef POSIX_FADV_DONTNEED
    posix_fadvise(fd_bin, 0, 0, POSIX_FADV_DONTNEED);
    posix_fadvise(fd_idx, 0, 0, POSIX_FADV_DONTNEED);
    posix_fadvise(fd_xor, 0, 0, POSIX_FADV_DONTNEED);
#else
    (void)fd_bin; (void)fd_idx; (void)fd_xor;
#endif

    long act_bin = ftell(f_bin);
    long act_idx = ftell(f_idx);
    long act_xor = ftell(f_xor);

    fclose(f_bin);
    fclose(f_idx);
    fclose(f_xor);

    free(hashes);
    hashes = NULL;

#if defined(__GLIBC__)
    malloc_trim(0);
#endif

    printf("\n=== Database Creation Complete ===\n");
    printf("Actual File Sizes:\n");
    printf("  BTC.bin: %ld bytes\n", act_bin);
    printf("  BTC.idx: %ld bytes\n", act_idx);
    printf("  BTC.xor: %ld bytes\n", act_xor);
    printf("  Total Size: %ld bytes\n", act_bin + act_idx + act_xor);
    printf("Memory and cache successfully flushed.\n");

    return 0;
}
