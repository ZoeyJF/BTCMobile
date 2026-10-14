#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <ctime>
#include <cctype>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <vector>
#include <string>
#include <queue>
#include "BTC.h"
#include "xorfilter.h"
#define IO_BUFFER_SIZE (4 * 1024 * 1024)
#define SORT_CHUNK_BYTES (64ULL * 1024 * 1024)
#define OUTPUT_BUF_BYTES (8 * 1024 * 1024)
#define IDX_MODE_FULL   0
#define IDX_MODE_SPARSE 1
#define IDX_FULL_ENTRY_SIZE   28
#define IDX_SPARSE_ENTRY_SIZE 24
#define IDX_SPARSE_STRIDE     64
static int g_idx_mode = IDX_MODE_SPARSE;
static uint8_t g_hex_table[256];
static int g_hex_table_ready = 0;
static void init_hex_table(void) {
    if (g_hex_table_ready) return;
    for (int i = 0; i < 256; i++) g_hex_table[i] = 0xFF;
    for (int i = '0'; i <= '9'; i++) g_hex_table[i] = (uint8_t)(i - '0');
    for (int i = 'a'; i <= 'f'; i++) g_hex_table[i] = (uint8_t)(i - 'a' + 10);
    for (int i = 'A'; i <= 'F'; i++) g_hex_table[i] = (uint8_t)(i - 'A' + 10);
    g_hex_table_ready = 1;
}
static int cmp_hash(const void *a, const void *b) {
    return memcmp(a, b, HASH160_LEN);
}
static double now_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}
static void print_size(const char *name, uint64_t bytes) {
    double kb = (double)bytes / 1024.0;
    double mb = kb / 1024.0;
    double gb = mb / 1024.0;
    if (gb >= 1.0) {
        fprintf(stderr, "[CREATE] %s : %llu bytes (%.2f GB)\n",
                name, (unsigned long long)bytes, gb);
    } else if (mb >= 1.0) {
        fprintf(stderr, "[CREATE] %s : %llu bytes (%.2f MB)\n",
                name, (unsigned long long)bytes, mb);
    } else {
        fprintf(stderr, "[CREATE] %s : %llu bytes (%.2f KB)\n",
                name, (unsigned long long)bytes, kb);
    }
}
static int show_menu_and_select(void) {
    fprintf(stderr, "\n");
    fprintf(stderr, "============================================================\n");
    fprintf(stderr, "CREATE-BIX  index format selection\n");
    fprintf(stderr, "============================================================\n\n");
    fprintf(stderr, "Two index formats are available for BTC.idx\n\n");
    fprintf(stderr, "Option A  Full index\n");
    fprintf(stderr, "  entry size   : 28 bytes\n");
    fprintf(stderr, "  layout       : 20 bytes hash160 plus 8 bytes uint64 offset\n");
    fprintf(stderr, "  entries      : one entry for every hash in BTC.bin\n");
    fprintf(stderr, "  per hash     : 28 bytes\n");
    fprintf(stderr, "  on 348M      : approximately 9.7 GB\n");
    fprintf(stderr, "  memory usage : mapped from disk, not loaded fully into RAM\n");
    fprintf(stderr, "  lookup       : binary search directly over mapped index\n");
    fprintf(stderr, "  best for     : servers with plenty of disk and RAM\n\n");
    fprintf(stderr, "Option B  Sparse index\n");
    fprintf(stderr, "  entry size   : 24 bytes\n");
    fprintf(stderr, "  layout       : 20 bytes hash160 plus 4 bytes uint32 offset\n");
    fprintf(stderr, "  entries      : one entry every 64 hashes in BTC.bin\n");
    fprintf(stderr, "  per hash     : 0.375 bytes approximately\n");
    fprintf(stderr, "  on 348M      : approximately 130 MB\n");
    fprintf(stderr, "  memory usage : loaded fully into RAM at engine startup\n");
    fprintf(stderr, "  lookup       : binary search on RAM index then linear scan of 64\n");
    fprintf(stderr, "  best for     : phones, fast lookup, low memory footprint\n\n");
    fprintf(stderr, "Enter option A or B: ");
    fflush(stderr);
    char buf[16];
    if (!fgets(buf, sizeof(buf), stdin)) return -1;
    for (int i = 0; buf[i]; i++) buf[i] = (char)toupper((unsigned char)buf[i]);
    if (buf[0] == 'A') return IDX_MODE_FULL;
    if (buf[0] == 'B') return IDX_MODE_SPARSE;
    return -1;
}
static int stage_hex_to_bin(const char *hex_path) {
    init_hex_table();
    int fd = open(hex_path, O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "[CREATE] cannot open %s\n", hex_path);
        return -1;
    }
    struct stat st;
    if (fstat(fd, &st) != 0) {
        fprintf(stderr, "[CREATE] fstat failed\n");
        close(fd);
        return -1;
    }
    size_t size = (size_t)st.st_size;
    if (size == 0) {
        fprintf(stderr, "[CREATE] %s is empty\n", hex_path);
        close(fd);
        return -1;
    }
    const uint8_t *map = (const uint8_t*)mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (map == MAP_FAILED) {
        fprintf(stderr, "[CREATE] mmap %s failed\n", hex_path);
        close(fd);
        return -1;
    }
    FILE *fbin = fopen(BIN_FILE, "wb");
    if (!fbin) {
        fprintf(stderr, "[CREATE] cannot create %s\n", BIN_FILE);
        munmap((void*)map, size);
        close(fd);
        return -1;
    }
    uint8_t *outbuf = (uint8_t*)malloc(OUTPUT_BUF_BYTES);
    if (!outbuf) {
        fprintf(stderr, "[CREATE] cannot allocate output buffer\n");
        fclose(fbin);
        munmap((void*)map, size);
        close(fd);
        return -1;
    }
    const uint8_t *p = map;
    const uint8_t *end = map + size;
    size_t outpos = 0;
    uint64_t count = 0;
    uint64_t skipped = 0;
    double t0 = now_sec();
    double last_print = t0;
    while (p < end) {
        const uint8_t *line_end = (const uint8_t*)memchr(p, '\n', (size_t)(end - p));
        if (!line_end) line_end = end;
        size_t linelen = (size_t)(line_end - p);
        if (linelen > 0 && p[linelen - 1] == '\r') linelen--;
        if (linelen != HEX_LINE_LEN) {
            skipped++;
        } else {
            uint8_t h[HASH160_LEN];
            int valid = 1;
            for (int i = 0; i < HASH160_LEN; i++) {
                uint8_t hi = g_hex_table[p[i * 2]];
                uint8_t lo = g_hex_table[p[i * 2 + 1]];
                if (hi == 0xFF || lo == 0xFF) { valid = 0; break; }
                h[i] = (uint8_t)((hi << 4) | lo);
            }
            if (valid) {
                memcpy(outbuf + outpos, h, HASH160_LEN);
                outpos += HASH160_LEN;
                count++;
                if (outpos + HASH160_LEN > OUTPUT_BUF_BYTES) {
                    if (fwrite(outbuf, 1, outpos, fbin) != outpos) {
                        fprintf(stderr, "\n[CREATE] write to %s failed\n", BIN_FILE);
                        free(outbuf);
                        fclose(fbin);
                        munmap((void*)map, size);
                        close(fd);
                        return -1;
                    }
                    outpos = 0;
                }
                if ((count & 0xFFFFF) == 0) {
                    double now = now_sec();
                    if (now - last_print >= 1.0) {
                        double elapsed = now - t0;
                        double rate = elapsed > 0 ? count / elapsed : 0;
                        double mb_written = (double)count * HASH160_LEN / (1024.0 * 1024.0);
                        fprintf(stderr, "\r[CREATE] stage1 %llu hashes  %.0f h/s  %.0f MB  %.0f s    ",
                                (unsigned long long)count, rate, mb_written, elapsed);
                        fflush(stderr);
                        last_print = now;
                    }
                }
            } else {
                skipped++;
            }
        }
        p = line_end;
        if (p < end && *p == '\n') p++;
    }
    if (outpos > 0) {
        if (fwrite(outbuf, 1, outpos, fbin) != outpos) {
            fprintf(stderr, "\n[CREATE] final write failed\n");
            free(outbuf);
            fclose(fbin);
            munmap((void*)map, size);
            close(fd);
            return -1;
        }
    }
    free(outbuf);
    fclose(fbin);
    munmap((void*)map, size);
    close(fd);
    if (count == 0) {
        fprintf(stderr, "[CREATE] no valid hashes\n");
        return -1;
    }
    double elapsed = now_sec() - t0;
    fprintf(stderr, "\r[CREATE] stage1 done %llu hashes  %llu skipped  %.1f s                \n",
            (unsigned long long)count, (unsigned long long)skipped, elapsed);
    print_size("BTC.bin", (uint64_t)count * HASH160_LEN);
    double full_gb = (double)count * IDX_FULL_ENTRY_SIZE / (1024.0 * 1024.0 * 1024.0);
    double sparse_entries = (double)((count + IDX_SPARSE_STRIDE - 1) / IDX_SPARSE_STRIDE);
    double sparse_mb = sparse_entries * IDX_SPARSE_ENTRY_SIZE / (1024.0 * 1024.0);
    fprintf(stderr, "[CREATE] estimated idx option A full   : %llu hashes * 28 bytes = %.2f GB\n",
            (unsigned long long)count, full_gb);
    fprintf(stderr, "[CREATE] estimated idx option B sparse : %llu entries * 24 bytes = %.2f MB\n",
            (unsigned long long)(count / IDX_SPARSE_STRIDE + 1), sparse_mb);
    return 0;
}
typedef struct {
    uint8_t hash[HASH160_LEN];
    int     idx;
} heap_entry_t;
typedef struct {
    bool operator()(const heap_entry_t &a, const heap_entry_t &b) const {
        return memcmp(a.hash, b.hash, HASH160_LEN) > 0;
    }
} heap_cmp_t;
static int stage_sort_bin(void) {
    struct stat st;
    if (stat(BIN_FILE, &st) != 0) {
        fprintf(stderr, "[CREATE] cannot stat %s\n", BIN_FILE);
        return -1;
    }
    size_t sz = (size_t)st.st_size;
    if (sz == 0 || (sz % HASH160_LEN) != 0) {
        fprintf(stderr, "[CREATE] %s size invalid\n", BIN_FILE);
        return -1;
    }
    uint64_t n = (uint64_t)(sz / HASH160_LEN);
    uint64_t chunk_records = SORT_CHUNK_BYTES / HASH160_LEN;
    if (chunk_records == 0) chunk_records = 1;
    uint64_t n_chunks = (n + chunk_records - 1) / chunk_records;
    fprintf(stderr, "[CREATE] stage2 sorting %llu hashes in %llu chunks of %llu records\n",
            (unsigned long long)n,
            (unsigned long long)n_chunks,
            (unsigned long long)chunk_records);
    double t0 = now_sec();
    FILE *fin = fopen(BIN_FILE, "rb");
    if (!fin) {
        fprintf(stderr, "[CREATE] cannot reopen %s\n", BIN_FILE);
        return -1;
    }
    char *rin = (char*)malloc(IO_BUFFER_SIZE);
    if (rin) setvbuf(fin, rin, _IOFBF, IO_BUFFER_SIZE);
    size_t chunk_bytes = (size_t)chunk_records * HASH160_LEN;
    uint8_t *chunk_buf = (uint8_t*)malloc(chunk_bytes);
    if (!chunk_buf) {
        fprintf(stderr, "[CREATE] cannot allocate chunk buffer\n");
        if (rin) free(rin);
        fclose(fin);
        return -1;
    }
    std::vector<std::string> temp_files;
    temp_files.reserve((size_t)n_chunks);
    for (uint64_t ci = 0; ci < n_chunks; ci++) {
        uint64_t start = ci * chunk_records;
        uint64_t cnt = n - start;
        if (cnt > chunk_records) cnt = chunk_records;
        size_t bytes = (size_t)cnt * HASH160_LEN;
        size_t got = fread(chunk_buf, 1, bytes, fin);
        if (got != bytes) {
            fprintf(stderr, "\n[CREATE] read chunk %llu failed\n", (unsigned long long)ci);
            free(chunk_buf);
            if (rin) free(rin);
            fclose(fin);
            for (size_t i = 0; i < temp_files.size(); i++) unlink(temp_files[i].c_str());
            return -1;
        }
        qsort(chunk_buf, (size_t)cnt, HASH160_LEN, cmp_hash);
        char tname[64];
        snprintf(tname, sizeof(tname), "BTC.sort.tmp.%llu", (unsigned long long)ci);
        FILE *ftmp = fopen(tname, "wb");
        if (!ftmp) {
            fprintf(stderr, "\n[CREATE] cannot create %s\n", tname);
            free(chunk_buf);
            if (rin) free(rin);
            fclose(fin);
            for (size_t i = 0; i < temp_files.size(); i++) unlink(temp_files[i].c_str());
            return -1;
        }
        char *wbuf = (char*)malloc(IO_BUFFER_SIZE);
        if (wbuf) setvbuf(ftmp, wbuf, _IOFBF, IO_BUFFER_SIZE);
        if (fwrite(chunk_buf, 1, bytes, ftmp) != bytes) {
            fprintf(stderr, "\n[CREATE] write %s failed\n", tname);
            free(wbuf);
            fclose(ftmp);
            free(chunk_buf);
            if (rin) free(rin);
            fclose(fin);
            unlink(tname);
            for (size_t i = 0; i < temp_files.size(); i++) unlink(temp_files[i].c_str());
            return -1;
        }
        fclose(ftmp);
        free(wbuf);
        temp_files.push_back(std::string(tname));
        double now = now_sec();
        fprintf(stderr, "\r[CREATE] stage2 chunk %llu / %llu sorted  %.1f s      ",
                (unsigned long long)(ci + 1), (unsigned long long)n_chunks, now - t0);
        fflush(stderr);
    }
    fclose(fin);
    free(chunk_buf);
    if (rin) free(rin);
    fprintf(stderr, "\n[CREATE] stage2 chunks sorted, starting k-way merge\n");
    std::vector<FILE*> files;
    std::vector<char*> rb_buffers;
    files.reserve(temp_files.size());
    rb_buffers.reserve(temp_files.size());
    for (size_t i = 0; i < temp_files.size(); i++) {
        FILE *f = fopen(temp_files[i].c_str(), "rb");
        if (!f) {
            fprintf(stderr, "[CREATE] cannot reopen %s\n", temp_files[i].c_str());
            for (size_t j = 0; j < files.size(); j++) fclose(files[j]);
            for (size_t j = 0; j < rb_buffers.size(); j++) free(rb_buffers[j]);
            for (size_t j = 0; j < temp_files.size(); j++) unlink(temp_files[j].c_str());
            return -1;
        }
        char *rb = (char*)malloc(IO_BUFFER_SIZE);
        if (rb) {
            setvbuf(f, rb, _IOFBF, IO_BUFFER_SIZE);
            rb_buffers.push_back(rb);
        }
        files.push_back(f);
    }
    FILE *fout = fopen(BIN_FILE, "wb");
    if (!fout) {
        fprintf(stderr, "[CREATE] cannot rewrite %s\n", BIN_FILE);
        for (size_t i = 0; i < files.size(); i++) fclose(files[i]);
        for (size_t i = 0; i < rb_buffers.size(); i++) free(rb_buffers[i]);
        for (size_t i = 0; i < temp_files.size(); i++) unlink(temp_files[i].c_str());
        return -1;
    }
    char *wout = (char*)malloc(IO_BUFFER_SIZE);
    if (wout) setvbuf(fout, wout, _IOFBF, IO_BUFFER_SIZE);
    std::priority_queue<heap_entry_t, std::vector<heap_entry_t>, heap_cmp_t> heap;
    for (size_t i = 0; i < files.size(); i++) {
        heap_entry_t e;
        if (fread(e.hash, 1, HASH160_LEN, files[i]) == HASH160_LEN) {
            e.idx = (int)i;
            heap.push(e);
        }
    }
    uint64_t written = 0;
    double last_print = now_sec();
    while (!heap.empty()) {
        heap_entry_t e = heap.top();
        heap.pop();
        if (fwrite(e.hash, 1, HASH160_LEN, fout) != HASH160_LEN) {
            fprintf(stderr, "\n[CREATE] merge write failed\n");
            fclose(fout);
            if (wout) free(wout);
            for (size_t i = 0; i < files.size(); i++) fclose(files[i]);
            for (size_t i = 0; i < rb_buffers.size(); i++) free(rb_buffers[i]);
            for (size_t i = 0; i < temp_files.size(); i++) unlink(temp_files[i].c_str());
            return -1;
        }
        written++;
        heap_entry_t next;
        if (fread(next.hash, 1, HASH160_LEN, files[e.idx]) == HASH160_LEN) {
            next.idx = e.idx;
            heap.push(next);
        }
        double now = now_sec();
        if (now - last_print >= 1.0) {
            fprintf(stderr, "\r[CREATE] stage2 merge %llu / %llu      ",
                    (unsigned long long)written, (unsigned long long)n);
            fflush(stderr);
            last_print = now;
        }
    }
    fclose(fout);
    if (wout) free(wout);
    for (size_t i = 0; i < files.size(); i++) fclose(files[i]);
    for (size_t i = 0; i < rb_buffers.size(); i++) free(rb_buffers[i]);
    for (size_t i = 0; i < temp_files.size(); i++) unlink(temp_files[i].c_str());
    double elapsed = now_sec() - t0;
    fprintf(stderr, "\n[CREATE] stage2 done sorted %llu hashes in %.1f s\n",
            (unsigned long long)written, elapsed);
    return 0;
}
static int stage_write_idx_full(uint8_t *map, uint64_t n, size_t sz) {
    FILE *fidx = fopen(IDX_FILE, "wb");
    if (!fidx) {
        fprintf(stderr, "[CREATE] cannot create %s\n", IDX_FILE);
        return -1;
    }
    char *iobuf = (char*)malloc(IO_BUFFER_SIZE);
    if (iobuf) setvbuf(fidx, iobuf, _IOFBF, IO_BUFFER_SIZE);
    fprintf(stderr, "[CREATE] stage3 mode FULL entry %d bytes one per hash\n",
            IDX_FULL_ENTRY_SIZE);
    uint8_t entry[IDX_FULL_ENTRY_SIZE];
    double t0 = now_sec();
    double last_print = t0;
    for (uint64_t i = 0; i < n; i++) {
        uint64_t off = i;
       memcpy(entry, map + i * HASH160_LEN, HASH160_LEN);
        memcpy(entry + HASH160_LEN, &off, 8);
        if (fwrite(entry, 1, IDX_FULL_ENTRY_SIZE, fidx) != IDX_FULL_ENTRY_SIZE) {
            fprintf(stderr, "\n[CREATE] write to %s failed at %llu\n",
                    IDX_FILE, (unsigned long long)i);
            free(iobuf);
            fclose(fidx);
            return -1;
        }
        if ((i & 0xFFFFF) == 0) {
            double now = now_sec();
            if (now - last_print >= 1.0) {
fprintf(stderr, "\r[CREATE] stage3 idx %llu / %llu      ",
                        (unsigned long long)i, (unsigned long long)n);
                fflush(stderr);
                last_print = now;
            }
        }
    }
    fflush(fidx);
    fclose(fidx);
    free(iobuf);
    double elapsed = now_sec() - t0;
    uint64_t idx_bytes = n * IDX_FULL_ENTRY_SIZE;
    fprintf(stderr, "\r[CREATE] stage3 done %llu full idx entries in %.1f s              \n",
            (unsigned long long)n, elapsed);
    print_size("BTC.idx", idx_bytes);
    (void)sz;
    return 0;
}
static int stage_write_idx_sparse(uint8_t *map, uint64_t n) {
    FILE *fidx = fopen(IDX_FILE, "wb");
    if (!fidx) {
        fprintf(stderr, "[CREATE] cannot create %s\n", IDX_FILE);
        return -1;
    }
    char *iobuf = (char*)malloc(IO_BUFFER_SIZE);
    if (iobuf) setvbuf(fidx, iobuf, _IOFBF, IO_BUFFER_SIZE);
    fprintf(stderr, "[CREATE] stage3 mode SPARSE entry %d bytes every %d hashes\n",
            IDX_SPARSE_ENTRY_SIZE, IDX_SPARSE_STRIDE);
    uint8_t entry[IDX_SPARSE_ENTRY_SIZE];
    uint64_t written = 0;
    uint64_t total = (n + IDX_SPARSE_STRIDE - 1) / IDX_SPARSE_STRIDE;
    double t0 = now_sec();
    double last_print = t0;
    for (uint64_t i = 0; i < n; i += IDX_SPARSE_STRIDE) {
        uint32_t off = (uint32_t)i;
        memcpy(entry, map + i * HASH160_LEN, HASH160_LEN);
        memcpy(entry + HASH160_LEN, &off, 4);
        if (fwrite(entry, 1, IDX_SPARSE_ENTRY_SIZE, fidx) != IDX_SPARSE_ENTRY_SIZE) {
            fprintf(stderr, "\n[CREATE] write to %s failed\n", IDX_FILE);
            free(iobuf);
            fclose(fidx);
            return -1;
        }
        written++;
        double now = now_sec();
        if (now - last_print >= 1.0) {
            fprintf(stderr, "\r[CREATE] stage3 idx %llu / %llu      ",
                    (unsigned long long)written, (unsigned long long)total);
            fflush(stderr);
            last_print = now;
        }
    }
    fflush(fidx);
    fclose(fidx);
    free(iobuf);
    double elapsed = now_sec() - t0;
    uint64_t idx_bytes = written * IDX_SPARSE_ENTRY_SIZE;
    fprintf(stderr, "\r[CREATE] stage3 done %llu sparse idx entries in %.1f s              \n",
            (unsigned long long)written, elapsed);
    print_size("BTC.idx", idx_bytes);
    return 0;
}
static int stage_write_idx(void) {
    int fd = open(BIN_FILE, O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "[CREATE] cannot open %s\n", BIN_FILE);
        return -1;
    }
    struct stat st;
    if (fstat(fd, &st) != 0) {
        close(fd);
        return -1;
    }
    size_t sz = (size_t)st.st_size;
    if (sz == 0 || (sz % HASH160_LEN) != 0) {
        close(fd);
        return -1;
    }
    uint8_t *map = (uint8_t*)mmap(nullptr, sz, PROT_READ, MAP_PRIVATE, fd, 0);
    if (map == MAP_FAILED) {
        fprintf(stderr, "[CREATE] mmap %s failed\n", BIN_FILE);
        close(fd);
        return -1;
    }
    uint64_t n = (uint64_t)(sz / HASH160_LEN);
    int r;
    if (g_idx_mode == IDX_MODE_FULL) {
        r = stage_write_idx_full(map, n, sz);
    } else {
        r = stage_write_idx_sparse(map, n);
    }
    munmap(map, sz);
    close(fd);
    return r;
}
static int stage_write_xor(void) {
    int fd_bin = open(BIN_FILE, O_RDONLY);
    if (fd_bin < 0) {
        fprintf(stderr, "[CREATE] cannot open %s\n", BIN_FILE);
        return -1;
    }
    struct stat st;
    if (fstat(fd_bin, &st) != 0) {
        close(fd_bin);
        return -1;
    }
    size_t sz = (size_t)st.st_size;
    if (sz == 0 || (sz % HASH160_LEN) != 0) {
        close(fd_bin);
        return -1;
    }
    uint8_t *map = (uint8_t*)mmap(nullptr, sz, PROT_READ, MAP_PRIVATE, fd_bin, 0);
    if (map == MAP_FAILED) {
        fprintf(stderr, "[CREATE] mmap %s failed\n", BIN_FILE);
        close(fd_bin);
        return -1;
    }
    uint64_t n = (uint64_t)(sz / HASH160_LEN);
    xorfilter_t xf;
    memset(&xf, 0, sizeof(xf));
    fprintf(stderr, "[CREATE] stage4 building xor filter for %llu hashes\n",
            (unsigned long long)n);
    double t0 = now_sec();
    int r = xorfilter_build(&xf, map, n, 16);
    munmap(map, sz);
    close(fd_bin);
    if (r != 0) {
        fprintf(stderr, "[CREATE] xor filter build failed code %d\n", r);
        return -1;
    }
    FILE *fxor = fopen(XOR_FILE, "wb");
    if (!fxor) {
        fprintf(stderr, "[CREATE] cannot create %s\n", XOR_FILE);
        xorfilter_free(&xf);
        return -1;
    }
    char *iobuf = (char*)malloc(IO_BUFFER_SIZE);
    if (iobuf) setvbuf(fxor, iobuf, _IOFBF, IO_BUFFER_SIZE);
    xor_header_t hdr;
    hdr.magic = XOR_MAGIC;
    hdr.version = XOR_VERSION;
    hdr.seed = xf.seed;
    hdr.block_length = xf.block_length;
    hdr.fingerprint_bits = xf.fingerprint_bits;
    hdr.entry_count = xf.entry_count;
    if (fwrite(&hdr, sizeof(hdr), 1, fxor) != 1) {
        fprintf(stderr, "[CREATE] write header failed\n");
        free(iobuf);
        fclose(fxor);
        xorfilter_free(&xf);
        return -1;
    }
    if (fwrite(xf.fingerprints, 1, xf.array_length, fxor) != xf.array_length) {
        fprintf(stderr, "[CREATE] write fingerprints failed\n");
        free(iobuf);
        fclose(fxor);
        xorfilter_free(&xf);
        return -1;
    }
    fclose(fxor);
    free(iobuf);
    uint64_t xor_bytes = (uint64_t)sizeof(xor_header_t) + xf.array_length;
    double elapsed = now_sec() - t0;
    fprintf(stderr, "[CREATE] stage4 done bits %u fp_bits %u entries %llu in %.1f s\n",
            (unsigned)xf.block_length,
            (unsigned)xf.fingerprint_bits,
            (unsigned long long)xf.entry_count,
            elapsed);
    print_size("BTC.xor", xor_bytes);
    xorfilter_free(&xf);
    return 0;
}
int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s -FILE.hex\n", argv[0]);
        return 1;
    }
    const char *name = argv[1];
    if (name[0] == '-') name++;
    if (name[0] == 0) {
        fprintf(stderr, "error: empty file name after dash\n");
        return 1;
    }
    if (access(name, R_OK) != 0) {
        fprintf(stderr, "error: file not readable: %s\n", name);
        return 1;
    }
    int sel = show_menu_and_select();
    if (sel == IDX_MODE_FULL) {
        g_idx_mode = IDX_MODE_FULL;
        fprintf(stderr, "[CREATE] option A selected: full index 28 bytes per hash\n\n");
    } else if (sel == IDX_MODE_SPARSE) {
        g_idx_mode = IDX_MODE_SPARSE;
        fprintf(stderr, "[CREATE] option B selected: sparse index 24 bytes every 64 hashes\n\n");
    } else {
        fprintf(stderr, "[CREATE] invalid option, aborting\n");
        return 1;
    }
    fprintf(stderr, "[CREATE] input %s\n", name);
    if (stage_hex_to_bin(name) != 0) return 1;
    if (stage_sort_bin() != 0) return 1;
    if (stage_write_idx() != 0) return 1;
    if (stage_write_xor() != 0) return 1;
    struct stat st;
    if (stat(BIN_FILE, &st) == 0) print_size("BTC.bin final", (uint64_t)st.st_size);
    if (stat(IDX_FILE, &st) == 0) print_size("BTC.idx final", (uint64_t)st.st_size);
    if (stat(XOR_FILE, &st) == 0) print_size("BTC.xor final", (uint64_t)st.st_size);
    fprintf(stderr, "[CREATE] all done\n");
    return 0;
}
