#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdarg>
#include <cstdint>
#include <ctime>
#include <cmath>
#include <pthread.h>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <set>
#include <vector>
#include <string>
#include <random>
#include <unistd.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sched.h>
#include "Biblioteca/BTC.h"
#include "Biblioteca/GMP256K1.h"
#include "Biblioteca/Point.h"
#include "Biblioteca/Int.h"
#include "Biblioteca/IntGroup.h"
#include "Biblioteca/sha3.h"
#include "Biblioteca/keccak.h"
#include "Biblioteca/util.h"
#include "Biblioteca/xorfilter.h"
#include "Biblioteca/AddressHelper.h"
#include "Biblioteca/Random.h"
#define THREAD_MAX 64
#define ORDER_HEX "FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFEBAAEDCE6AF48A03BBFD25E8CD0364141"
#define BETA_HEX  "7ae96a2b657c07106e64479eac3434e99cf0497512f58995c1396c28719501ee"
#define LAMBDA_HEX "5363ad4cc05c30e0a5261c028812645a122e22ea20816678df02967c1b23bd72"
static Secp256K1* g_secp = nullptr;
static std::mutex g_secp_init_mutex;
static xor_table_t g_xor;
static unsigned char* g_bin_map = nullptr;
static uint64_t g_bin_count = 0;
static size_t g_bin_size = 0;
static int g_bin_fd = -1;
static idx_table_t g_idx_table;
static uint64_t g_idx_count = 0;
static size_t g_idx_size = 0;
static int g_idx_fd = -1;
static int g_idx_mode = 0;
static std::atomic<uint64_t> g_checked{0};
static std::atomic<uint64_t> g_found{0};
static std::atomic<int> g_stop{0};
static std::atomic<uint64_t> g_derive_count{0};
static std::atomic<uint64_t> g_xor_queries{0};
static std::atomic<uint64_t> g_xor_hits{0};
static std::atomic<uint64_t> g_xor_fp{0};
static pthread_mutex_t g_hit_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t g_ui_mutex = PTHREAD_MUTEX_INITIALIZER;
static std::condition_variable g_ui_cv;
static std::mutex g_monitor_mutex;
static std::set<std::string> g_seen;
static std::vector<std::string> g_last_hits;
static std::atomic<int> g_range_bits{0};
static int g_mode = 0;
static int g_subranges = 1;
static int g_threads = 1;
static time_t g_start = 0;
static std::atomic<int> g_thread_subrange[THREAD_MAX];
static std::atomic<int> g_thread_active[THREAD_MAX];
static std::atomic<uint64_t> g_thread_keys[THREAD_MAX];
static bool g_use_ram_bin = false;
static bool g_use_ram_idx = false;
static bool g_use_ram_xor = false;
static bool g_ui_active = false;
static Int g_global_start;
static Int g_global_end;
static std::atomic<int> g_current_subrange{0};
static std::atomic<int> g_pure_cycle{0};
static std::atomic<uint64_t> g_fwd_checked{0};
static std::atomic<uint64_t> g_bwd_checked{0};
static int term_height(void) {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_row > 0) return ws.ws_row;
    return 24;
}
static void cprintf(const char *fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    printf("\033[2K%s\033[K\n", buf);
}
static void ui_enter(void) {
    printf("\033[?1049h");
    printf("\033[2J\033[H");
    printf("\033[?25l");
    fflush(stdout);
    g_ui_active = true;
}
static void ui_leave(void) {
    if (g_ui_active) {
        printf("\033[?25h");
        printf("\033[?1049l");
        fflush(stdout);
        g_ui_active = false;
    }
}
static void format_commas(uint64_t v, char *out, size_t n) {
    char tmp[32];
    snprintf(tmp, sizeof(tmp), "%llu", (unsigned long long)v);
    size_t len = strlen(tmp);
    size_t j = 0;
    for (size_t i = 0; i < len && j + 1 < n; i++) {
        if (i > 0 && ((len - i) % 3) == 0) out[j++] = ',';
        out[j++] = tmp[i];
    }
    out[j] = 0;
}
static void format_total_speed(double speed, char *out, size_t n) {
    char tmp[64];
    snprintf(tmp, sizeof(tmp), "%.2f", speed);
    size_t len = strlen(tmp);
    size_t dot_pos = len;
    for (size_t i = 0; i < len; i++) {
        if (tmp[i] == '.') { dot_pos = i; break; }
    }
    size_t j = 0;
    size_t count = 0;
    for (size_t i = 0; i < dot_pos && j + 1 < n; i++) {
        if (count > 0 && (dot_pos - i) % 3 == 0) out[j++] = '.';
        out[j++] = tmp[i];
        count++;
    }
    if (dot_pos < len && j + 1 < n) {
        out[j++] = ',';
        for (size_t i = dot_pos + 1; i < len && j + 1 < n; i++) {
            out[j++] = tmp[i];
        }
    }
    out[j] = 0;
}
static void draw_progress_bar(double pct, int width) {
    if (width < 5) width = 5;
    if (width > 128) width = 128;
    int filled = (int)(pct * (double)(width - 2));
    if (filled < 0) filled = 0;
    if (filled > width - 2) filled = width - 2;
    char bar[128];
    memset(bar, '-', (size_t)(width - 2));
    bar[width - 2] = 0;
    for (int i = 0; i < filled; i++) bar[i] = '=';
    if (filled < width - 2) bar[filled] = '>';
    cprintf("[%-*s] %6.2f%%", width - 2, bar, pct * 100.0);
}
static void draw_backward_bar(double pct, int width) {
    if (width < 5) width = 5;
    if (width > 128) width = 128;
    int filled = (int)(pct * (double)(width - 2));
    if (filled < 0) filled = 0;
    if (filled > width - 2) filled = width - 2;
    char bar[128];
    memset(bar, '-', (size_t)(width - 2));
    bar[width - 2] = 0;
    for (int i = 0; i < filled; i++) {
        int idx = (width - 3) - i;
        if (idx >= 0 && idx < width - 2) bar[idx] = '=';
    }
    int arrow_idx = (width - 3) - filled;
    if (arrow_idx >= 0 && arrow_idx < width - 2) bar[arrow_idx] = '<';
    cprintf("[%-*s] %6.2f%%", width - 2, bar, pct * 100.0);
}
static void draw_random_dots(void) {
    static thread_local std::mt19937 gen(42);
    const char *rows[] = {
        "       .        .              .       .",
        "   .       .          .   .                 .",
        "          .       .        .       .",
        "     .             .                .       .",
        "             .      ",
        "       .              *       .          .",
        "   .        .                 .     .",
        "          .       .       .             ."
    };
    for (int i = 0; i < 8; i++) {
        char line[128];
        strncpy(line, rows[i], sizeof(line) - 1);
        line[sizeof(line) - 1] = 0;
        for (int j = 0; line[j]; j++) {
            if (line[j] == '*') line[j] = '.';
        }
        int pos = 15 + (gen() % 10);
        if (pos < (int)strlen(line)) line[pos] = '*';
        cprintf("%s", line);
    }
}
static void draw_pure_dots(void) {
    static thread_local std::mt19937 gen(137);
    const char *rows[] = {
        "       .        .              .       .",
        "   .       .          .   .                      .        .",
        "   .       .          .   .                 .",
        "          .       .        .       .",
        "     .             .                .       .  .",
        "          .       .        .       .",
        "     .             .                .           .        .  ",
        "   .       .          .   .                 .",
        "          .       .        .       .",
        "     .                                 .",
        "       .              *       .        .              .",
        "   .       .          .   .       .                .       .   .",
        "       .        .              .       .",
        "   .       .          .   .                 .",
        "          .       .        .       .       .        .  ",
        "   .       .          .   .                 .",
        "          .       .        .       .",
        "     .             .                .       .",
        "     .             .                .       .",
        "",
        "       .        .              .       .",
        "   .       .          .   .               .        .",
        "   .       .          .   .                 .",
        "          .       .        .       .",
        "     .             .                .       .         ."
    };
    for (int i = 0; i < 25; i++) {
        char line[128];
        strncpy(line, rows[i], sizeof(line) - 1);
        line[sizeof(line) - 1] = 0;
        for (int j = 0; line[j]; j++) {
            if (line[j] == '*') line[j] = '.';
        }
        int pos = 10 + (gen() % 20);
        if (pos < (int)strlen(line)) line[pos] = '*';
        cprintf("%s", line);
    }
}
static void ui_draw(void) {
    pthread_mutex_lock(&g_ui_mutex);
    time_t now = time(nullptr);
    int elapsed = (int)(now - g_start);
    if (elapsed < 0) elapsed = 0;
    uint64_t ck = g_checked.load();
    uint64_t fd = g_found.load();
    uint64_t dv = g_derive_count.load();
    uint64_t xh = g_xor_hits.load();
    uint64_t xfp = g_xor_fp.load();
    double total_speed = 0.0;
    for (int i = 0; i < g_threads && i < THREAD_MAX; i++) {
        uint64_t tk = g_thread_keys[i].load();
        total_speed += elapsed > 0 ? (double)tk / (double)elapsed : 0.0;
    }
    const char *mode_str = "UNKNOWN";
    switch (g_mode) {
        case 1: mode_str = "SEQUENTIAL FORWARD"; break;
        case 2: mode_str = "BIDIRECTIONAL"; break;
        case 3: mode_str = "RANDOM"; break;
        case 4: mode_str = "PURE RANDOM"; break;
        case 5: mode_str = "Subrango + Random"; break;
    }
    int max_rows = term_height();
    int row = 0;
    printf("\033[H");
    cprintf("================================================================================"); row++;
    cprintf("<x> BTC-MOBILE <x> By Zoey Alejandroo JF <x>"); row++;
    cprintf("================================================================================"); row++;
    cprintf("<x> MODE : %s", mode_str); row++;
    cprintf("================================================================================"); row++;
    if (g_mode == 4) {
        cprintf("<x> GLOBAL SEARCH SPACE"); row++;
        cprintf("-------------------------------------"); row++;
        cprintf("<x> RANGE Selection : RANDOM"); row++;
        cprintf("-------------------------------------"); row++;
        cprintf("<x> RANGE %d/256", g_range_bits.load()); row++;
        cprintf("-------------------------------------"); row++;
        char shex[80];
        char ehex[80];
        char *sp = g_global_start.GetBase16();
        char *ep = g_global_end.GetBase16();
        snprintf(shex, sizeof(shex), "0x%s", sp ? sp : "0");
        snprintf(ehex, sizeof(ehex), "0x%s", ep ? ep : "0");
        if (sp) free(sp);
        if (ep) free(ep);
        cprintf("<x> %s", shex); row++;
        cprintf("-------------------------------------"); row++;
        cprintf("<x> %s", ehex); row++;
        cprintf("-------------------------------------"); row++;
        cprintf("<x> SUBRSNGOS Selection : RANDOM"); row++;
        cprintf("Random Search Distribution"); row++;
        if (row + 25 < max_rows) {
            draw_pure_dots();
            row += 25;
        }
    } else if (g_mode == 5) {
        cprintf("<x> SPLIT the selected range : 100 Subranges"); row++;
        cprintf("================================================================================"); row++;
        cprintf("<x> PARENT RANGE : %d", g_range_bits.load()); row++;
        cprintf("-------------------------------------"); row++;
        cprintf("CURRENT SUBRANGE : %d / 100", g_current_subrange.load() + 1); row++;
        cprintf("-------------------------------------"); row++;
        Int span;
        span.Sub(&g_global_end, &g_global_start);
        span.Add((uint32_t)1);
        Int divisor((uint32_t)100);
        Int chunk;
        chunk.Set(&span);
        chunk.Div(&divisor);
        Int remainder;
        remainder.Set(&span);
        remainder.Mod(&divisor);
        int cur = g_current_subrange.load();
        Int completed_keys;
        completed_keys.Set(&chunk);
        completed_keys.Mult((uint64_t)cur);
        if (cur < (int)remainder.GetInt64()) {
            completed_keys.Add((uint64_t)cur);
        } else {
            completed_keys.Add((uint64_t)remainder.GetInt64());
        }
        double pct = 0.0;
        if (!span.IsZero()) {
            int span_b = span.GetBitLength();
            int comp_b = completed_keys.GetBitLength();
            if (span_b - comp_b < 53) {
                double d_completed = (double)completed_keys.GetInt64();
                double d_span = (double)span.GetInt64();
                if (d_span > 0.0) pct = d_completed / d_span;
            }
        }
        if (pct > 1.0) pct = 1.0;
        draw_progress_bar(pct, 60); row++;
        cprintf("-------------------------------------"); row++;
        cprintf("SUBRANGE COVERAGE"); row++;
        cprintf("-------------------------------------"); row++;
        char ckbuf[32];
        format_commas(ck, ckbuf, sizeof(ckbuf));
        cprintf("Random Positions Checked : %s", ckbuf); row++;
        cprintf("-------------------------------------"); row++;
        cprintf("Unique Positions          : %s", ckbuf); row++;
        cprintf("================================================================================"); row++;
        cprintf("--------------------------------------------------------------------------------"); row++;
        cprintf("<x> SUBRANGE STATUS :"); row++;
        for (int r = 0; r < 4 && row < max_rows - 10; r++) {
            char line[256];
            line[0] = 0;
            for (int col = 0; col < 4; col++) {
                int idx = r * 4 + col;
                if (idx >= 100) break;
                const char *status = "WAITING";
                if (idx < cur) status = "COMPLETE";
                else if (idx == cur) status = "ACTIVE";
                char part[64];
                snprintf(part, sizeof(part), "%03d  %s ", idx + 1, status);
                strncat(line, part, sizeof(line) - strlen(line) - 1);
            }
            cprintf("%s", line); row++;
        }
        if (row < max_rows - 8) { cprintf("..."); row++; }
        char curline[64];
        snprintf(curline, sizeof(curline), "%03d  ACTIVE", cur + 1);
        if (row < max_rows - 7) { cprintf("%s", curline); row++; }
        if (row < max_rows - 6) { cprintf("..."); row++; }
        if (row < max_rows - 5) { cprintf("100  WAITING"); row++; }
    } else {
        cprintf("<x> RANGE : %d/256", g_range_bits.load()); row++;
        cprintf("-------------------------------------"); row++;
        cprintf("<x> BITS"); row++;
        char shex[80];
        char ehex[80];
        char *sp = g_global_start.GetBase16();
        char *ep = g_global_end.GetBase16();
        snprintf(shex, sizeof(shex), "0x%s", sp ? sp : "0");
        snprintf(ehex, sizeof(ehex), "0x%s", ep ? ep : "0");
        if (sp) free(sp);
        if (ep) free(ep);
        cprintf("<x> %s", shex); row++;
        cprintf("-------------------------------------"); row++;
        cprintf("<x> %s", ehex); row++;
        cprintf("-------------------------------------"); row++;
        if (g_mode == 2) {
            cprintf("100%% =>                  100%%     100%%                 <= 100%%"); row++;
            cprintf("START >=======================<>=======================< END"); row++;
            cprintf("          BACKWARD                         FORWARD"); row++;
            cprintf("-------------------------------------"); row++;
            cprintf("<x> Backward Progress"); row++;
            uint64_t bck = g_bwd_checked.load();
            uint64_t fck = g_fwd_checked.load();
            uint64_t tck = bck + fck;
            double bpct = 0.0;
            double fpct = 0.0;
            if (tck > 0) {
                bpct = (double)bck / (double)tck;
                fpct = (double)fck / (double)tck;
            }
            draw_backward_bar(bpct, 60); row++;
            cprintf("-------------------------------------"); row++;
            cprintf("<x> Forward Progress"); row++;
            draw_progress_bar(fpct, 60); row++;
        } else if (g_mode == 1) {
            cprintf("<x> RANGE PROGRESES/s"); row++;
            double pct = 0.0;
            if (!g_global_start.IsEqual(&g_global_end)) {
                Int span;
                span.Sub(&g_global_end, &g_global_start);
                span.Add((uint32_t)1);
                if (!span.IsZero()) {
                    int span_b = span.GetBitLength();
                    int ck_b = 64;
                    if (span_b - ck_b < 53) {
                        double d_ck = (double)ck;
                        double d_span = (double)span.GetInt64();
                        if (d_span > 0.0) pct = d_ck / d_span;
                        if (pct > 1.0) pct = 1.0;
                    }
                }
            }
            draw_progress_bar(pct, 60); row++;
            cprintf("................................................................................"); row++;
        } else if (g_mode == 3) {
            cprintf("Search Type   : RANDOM POSITIONS"); row++;
            cprintf(""); row++;
            cprintf("Random Search Distribution"); row++;
            cprintf(""); row++;
            if (row + 8 < max_rows - 10) {
                draw_random_dots();
                row += 8;
            }
            cprintf(""); row++;
            char ckbuf[32];
            format_commas(ck, ckbuf, sizeof(ckbuf));
            cprintf("Positions Checked : %s", ckbuf); row++;
            cprintf("Unique Positions  : %s", ckbuf); row++;
        }
    }
    if (row < max_rows - 8) { cprintf("================================================================================"); row++; }
    if (row < max_rows - 7) { cprintf("--------------------------------------------------------------------------------"); row++; }
    if (row < max_rows - 6) { cprintf("================================================================================"); row++; }
    for (int i = 0; i < g_threads && row < max_rows - 5; i += 2) {
        char lbuf[128];
        char rbuf[128];
        if (g_thread_active[i].load()) {
            uint64_t lk = g_thread_keys[i].load();
            double lspeed = elapsed > 0 ? (double)lk / (double)elapsed : 0.0;
            char lspd[32];
            format_commas((uint64_t)lspeed, lspd, sizeof(lspd));
            snprintf(lbuf, sizeof(lbuf), "[Thread %02d  : %s keys/s ]", i + 1, lspd);
        } else {
            snprintf(lbuf, sizeof(lbuf), "[Thread %02d  : INACTIVE ]", i + 1);
        }
        if (i + 1 < g_threads) {
            if (g_thread_active[i + 1].load()) {
                uint64_t rk = g_thread_keys[i + 1].load();
                double rspeed = elapsed > 0 ? (double)rk / (double)elapsed : 0.0;
                char rspd[32];
                format_commas((uint64_t)rspeed, rspd, sizeof(rspd));
                snprintf(rbuf, sizeof(rbuf), "[Thread %02d  : %s keys/s ]", i + 2, rspd);
            } else {
                snprintf(rbuf, sizeof(rbuf), "[Thread %02d  : INACTIVE ]", i + 2);
            }
            cprintf("%-40s %s", lbuf, rbuf); row++;
        } else {
            cprintf("....%s", lbuf); row++;
        }
        if (row < max_rows - 4) { cprintf("-------------------------------------"); row++; }
    }
    if (row < max_rows - 3) { cprintf("--------------------------------------------------------------------------------"); row++; }
    if (row < max_rows - 2) { cprintf("================================================================================"); row++; }
    char tspd[64];
    format_total_speed(total_speed, tspd, sizeof(tspd));
    if (row < max_rows - 1) { cprintf("<x> [TOTAL : %s keys/s ]", tspd); row++; }
    if (row < max_rows) { cprintf("--------------------------------------------------------------------------------"); row++; }
    if (row < max_rows) { cprintf("================================================================================"); row++; }
    double fp_rate = xh > 0 ? (double)xfp / (double)xh : 0.0;
    char dvbuf[32];
    format_commas(dv, dvbuf, sizeof(dvbuf));
    if (row < max_rows) { cprintf("<x> [Derivations : %s]", dvbuf); row++; }
    if (row < max_rows) { cprintf("<x> [Xor-Filter : %.2f]  False Positives", fp_rate); row++; }
    if (row < max_rows) { cprintf("--------------------------------------------------------------------------------"); row++; }
    if (row < max_rows) { cprintf("================================================================================"); row++; }
    if (row < max_rows) { cprintf("<x> FOUND : %llu", (unsigned long long)fd); row++; }
    if (row < max_rows) { cprintf("================================================================================"); row++; }
    std::vector<std::string> local_hits;
    pthread_mutex_lock(&g_hit_mutex);
    local_hits = g_last_hits;
    pthread_mutex_unlock(&g_hit_mutex);
    size_t shown = local_hits.size() < 5 ? local_hits.size() : 5;
    for (size_t i = 0; i < shown && row < max_rows; i++) {
        const std::string &hit = local_hits[local_hits.size() - 1 - i];
        cprintf("FOUND.txt : "); row++;
        if (row < max_rows) { cprintf("%s", hit.c_str()); row++; }
        if (i + 1 < shown && row < max_rows) { cprintf("."); row++; }
    }
    printf("\033[J");
    fflush(stdout);
    pthread_mutex_unlock(&g_ui_mutex);
}
static void hash_to_hex(const uint8_t *h, char *out) {
    char *tmp = tohex((char*)h, HASH160_LEN);
    if (tmp) {
        strncpy(out, tmp, 40);
        out[40] = 0;
        free(tmp);
    } else {
        out[0] = 0;
    }
}
static void priv_to_hex(Int &priv, char *out) {
    memset(out, '0', 64);
    out[64] = 0;
    char *p = priv.GetBase16();
    if (p) {
        int len = (int)strlen(p);
        if (len > 0 && len <= 64) memcpy(out + (64 - len), p, (size_t)len);
        free(p);
    }
}
static void timestamp_utc(char *buf, size_t n) {
    time_t t = time(nullptr);
    struct tm tm_utc;
    gmtime_r(&t, &tm_utc);
    strftime(buf, n, "%Y-%m-%dT%H:%M:%SZ", &tm_utc);
}
static int load_xor_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    xor_header_t hdr;
    if (fread(&hdr, sizeof(hdr), 1, f) != 1) { fclose(f); return -1; }
    if (hdr.magic != XOR_MAGIC || hdr.version != XOR_VERSION) { fclose(f); return -1; }
    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, sizeof(xor_header_t), SEEK_SET);
    uint64_t bytes = (uint64_t)(file_size - (long)sizeof(xor_header_t));
    uint8_t *fp = (uint8_t*)malloc(bytes ? bytes : 1);
    if (!fp) { fclose(f); return -1; }
    if (bytes > 0 && fread(fp, 1, bytes, f) != bytes) { free(fp); fclose(f); return -1; }
    fclose(f);
    g_xor.fingerprints = fp;
    g_xor.array_length = bytes;
    g_xor.block_length = hdr.block_length;
    g_xor.fingerprint_bits = (uint8_t)hdr.fingerprint_bits;
    g_xor.seed = hdr.seed;
    g_xor.entry_count = hdr.entry_count;
    return 0;
}
static void free_all(void) {
    if (g_bin_map) {
        if (g_use_ram_bin) {
            free(g_bin_map);
        } else {
            if (g_bin_map != MAP_FAILED) {
                munmap(g_bin_map, g_bin_size);
            }
        }
        g_bin_map = nullptr;
    }
    if (g_idx_table.records) {
        if (g_use_ram_idx) {
            free(g_idx_table.records);
        } else {
            if (g_idx_table.records != MAP_FAILED) {
                munmap(g_idx_table.records, g_idx_size);
            }
        }
        g_idx_table.records = nullptr;
    }
    if (g_bin_fd >= 0) { close(g_bin_fd); g_bin_fd = -1; }
    if (g_idx_fd >= 0) { close(g_idx_fd); g_idx_fd = -1; }
    if (g_xor.fingerprints) { free(g_xor.fingerprints); g_xor.fingerprints = nullptr; }
}
static int validate_and_load_bin(void) {
    struct stat st;
    if (stat("BTC.bin", &st) != 0) {
        fprintf(stderr, "error: BTC.bin not found\n");
        return -1;
    }
    if (st.st_size <= 0 || (st.st_size % HASH160_LEN) != 0) {
        fprintf(stderr, "error: BTC.bin size %lld is not a multiple of %d bytes\n",
                (long long)st.st_size, HASH160_LEN);
        return -1;
    }
    g_bin_fd = open("BTC.bin", O_RDONLY);
    if (g_bin_fd < 0) {
        fprintf(stderr, "error: cannot open BTC.bin\n");
        return -1;
    }
    g_bin_size = (size_t)st.st_size;
    if (g_use_ram_bin) {
        g_bin_map = (unsigned char*)malloc(g_bin_size);
        if (!g_bin_map) {
            fprintf(stderr, "error: malloc BTC.bin failed\n");
            close(g_bin_fd); g_bin_fd = -1;
            return -1;
        }
        if (read(g_bin_fd, g_bin_map, g_bin_size) != (ssize_t)g_bin_size) {
            fprintf(stderr, "error: read BTC.bin failed\n");
            free(g_bin_map); g_bin_map = nullptr;
            close(g_bin_fd); g_bin_fd = -1;
            return -1;
        }
    } else {
        g_bin_map = (unsigned char*)mmap(nullptr, g_bin_size, PROT_READ, MAP_PRIVATE, g_bin_fd, 0);
        if (g_bin_map == MAP_FAILED) {
            fprintf(stderr, "error: mmap BTC.bin failed\n");
            close(g_bin_fd); g_bin_fd = -1;
            g_bin_map = nullptr; g_bin_size = 0;
            return -1;
        }
    }
    g_bin_count = (uint64_t)(g_bin_size / HASH160_LEN);
    return 0;
}
static int validate_and_load_idx(void) {
    struct stat st;
    if (stat("BTC.idx", &st) != 0) {
        fprintf(stderr, "error: BTC.idx not found\n");
        return -1;
    }
    if (st.st_size <= 0) {
        fprintf(stderr, "error: BTC.idx is empty\n");
        return -1;
    }
    g_idx_fd = open("BTC.idx", O_RDONLY);
    if (g_idx_fd < 0) {
        fprintf(stderr, "error: cannot open BTC.idx\n");
        return -1;
    }
    g_idx_size = (size_t)st.st_size;
    uint64_t full_expected = g_bin_count * 28ULL;
    uint64_t sparse_entries = (g_bin_count + 63ULL) / 64ULL;
    uint64_t sparse_expected = sparse_entries * 24ULL;
    if (g_idx_size == full_expected) {
        g_idx_mode = 1;
        g_idx_count = g_bin_count;
    } else if (g_idx_size == sparse_expected) {
        g_idx_mode = 2;
        g_idx_count = sparse_entries;
    } else {
        fprintf(stderr, "error: BTC.idx size %llu does not match full %llu or sparse %llu\n",
                (unsigned long long)g_idx_size,
                (unsigned long long)full_expected,
                (unsigned long long)sparse_expected);
        close(g_idx_fd); g_idx_fd = -1;
        return -1;
    }
    if (g_use_ram_idx) {
        g_idx_table.records = (idx_record_t*)malloc(g_idx_size);
        if (!g_idx_table.records) {
            fprintf(stderr, "error: malloc BTC.idx failed\n");
            close(g_idx_fd); g_idx_fd = -1;
            return -1;
        }
        if (read(g_idx_fd, g_idx_table.records, g_idx_size) != (ssize_t)g_idx_size) {
            fprintf(stderr, "error: read BTC.idx failed\n");
            free(g_idx_table.records); g_idx_table.records = nullptr;
            close(g_idx_fd); g_idx_fd = -1;
            return -1;
        }
    } else {
        g_idx_table.records = (idx_record_t*)mmap(nullptr, g_idx_size, PROT_READ, MAP_PRIVATE, g_idx_fd, 0);
        if (g_idx_table.records == MAP_FAILED) {
            fprintf(stderr, "error: mmap BTC.idx failed\n");
            close(g_idx_fd); g_idx_fd = -1;
            g_idx_table.records = nullptr; g_idx_size = 0;
            return -1;
        }
    }
    g_idx_table.count = g_idx_count;
    return 0;
}
static int validate_and_load_xor(void) {
    struct stat st;
    if (stat("BTC.xor", &st) != 0) {
        fprintf(stderr, "error: BTC.xor not found\n");
        return -1;
    }
    if (st.st_size < (off_t)sizeof(xor_header_t)) {
        fprintf(stderr, "error: BTC.xor header truncated\n");
        return -1;
    }
    if (load_xor_file("BTC.xor") != 0) {
        fprintf(stderr, "error: BTC.xor cannot be loaded or header invalid\n");
        return -1;
    }
    uint64_t expected_bytes = (uint64_t)st.st_size - (uint64_t)sizeof(xor_header_t);
    if (expected_bytes != g_xor.array_length) {
        fprintf(stderr, "error: BTC.xor array length %llu does not match file %llu\n",
                (unsigned long long)g_xor.array_length,
                (unsigned long long)expected_bytes);
        return -1;
    }
    if (g_xor.entry_count != g_bin_count) {
        fprintf(stderr, "error: BTC.xor entry_count %llu does not match BTC.bin hashes %llu\n",
                (unsigned long long)g_xor.entry_count,
                (unsigned long long)g_bin_count);
        return -1;
    }
    return 0;
}
static int xor_query(const uint8_t *h160) {
    g_xor_queries.fetch_add(1);
    int r = btc_xor_contains(&g_xor, h160, HASH160_LEN);
    if (r) g_xor_hits.fetch_add(1);
    return r;
}
static int scan_block(const uint8_t *h160, uint64_t start, uint64_t count) {
    if (start >= g_bin_count) return 0;
    if (start + count > g_bin_count) count = g_bin_count - start;
    for (uint64_t i = 0; i < count; i++) {
        const uint8_t *entry = g_bin_map + (size_t)(start + i) * HASH160_LEN;
        int c = memcmp(entry, h160, HASH160_LEN);
        if (c == 0) return 1;
        if (c > 0) return 0;
    }
    return 0;
}
static int bin_search_full(const uint8_t *h160) {
    uint64_t record_index = 0;
    return btc_binary_search_idx(&g_idx_table, h160, &record_index);
}
static int bin_search_sparse(const uint8_t *h160) {
    int64_t lo = 0;
    int64_t hi = (int64_t)g_idx_count - 1;
    int64_t best = -1;
    const uint8_t *base = (const uint8_t*)g_idx_table.records;
    while (lo <= hi) {
        int64_t mid = lo + (hi - lo) / 2;
        const uint8_t *entry = base + (uint64_t)mid * 24;
        int c = memcmp(entry, h160, HASH160_LEN);
        if (c == 0) {
            uint64_t off = ((uint64_t)entry[20])
                           | ((uint64_t)entry[21] << 8)
                           | ((uint64_t)entry[22] << 16)
                           | ((uint64_t)entry[23] << 24);
            return scan_block(h160, off, 64);
        }
        if (c < 0) { best = mid; lo = mid + 1; }
        else hi = mid - 1;
    }
    uint64_t start = 0;
    uint64_t next_start = g_bin_count;
    if (best >= 0) {
        const uint8_t *entry = base + (uint64_t)best * 24;
        start = ((uint64_t)entry[20])
                | ((uint64_t)entry[21] << 8)
                | ((uint64_t)entry[22] << 16)
                | ((uint64_t)entry[23] << 24);
        if (best + 1 < (int64_t)g_idx_count) {
            const uint8_t *next_entry = base + (uint64_t)(best + 1) * 24;
            next_start = ((uint64_t)next_entry[20])
                         | ((uint64_t)next_entry[21] << 8)
                         | ((uint64_t)next_entry[22] << 16)
                         | ((uint64_t)next_entry[23] << 24);
        }
        if (next_start < start) return 0;
        uint64_t range = next_start - start;
        if (scan_block(h160, start, range)) return 1;
    }
    if (best > 0) {
        const uint8_t *prev_entry = base + (uint64_t)(best - 1) * 24;
        uint64_t prev_off = ((uint64_t)prev_entry[20])
                            | ((uint64_t)prev_entry[21] << 8)
                            | ((uint64_t)prev_entry[22] << 16)
                            | ((uint64_t)prev_entry[23] << 24);
        if (start < prev_off) return 0;
        uint64_t prev_range = start - prev_off;
        if (scan_block(h160, prev_off, prev_range)) return 1;
    }
    return 0;
}
static int bin_search(const uint8_t *h160) {
    if (!g_bin_map || g_bin_count == 0 || !g_idx_table.records || g_idx_count == 0) return 0;
    if (g_idx_mode == 1) return bin_search_full(h160);
    return bin_search_sparse(h160);
}
static void extended_write_found(const char *hash_hex, Int &priv, Point &pub,
                                 const char *currency, int subrange_id,
                                 int thread_id, const char *ts) {
    DerivedInfo dinfo;
    derive_all_info(priv, pub, dinfo);
    char entry[2048];
    format_extended_entry(dinfo, g_range_bits.load(), subrange_id, thread_id, ts, entry, sizeof(entry));
    FILE *fe = fopen(FOUND_FILE, "a");
    if (fe) {
        fputs(entry, fe);
        fclose(fe);
    }
    printf("\n[HIT %s]\n", currency);
    printf("  Matched Hash : %s\n", hash_hex);
    printf("  Private Key  : %s\n", dinfo.privkey_hex.c_str());
    printf("  Pubkey Comp  : %s\n", dinfo.pubkey_compressed_hex.c_str());
    printf("  Pubkey Uncomp: %s\n", dinfo.pubkey_uncompressed_hex.c_str());
    printf("  Hash160      : %s\n", dinfo.hash160_hex.c_str());
    printf("  BTC P2PKH C  : %s\n", dinfo.btc_p2pkh_compressed.c_str());
    printf("  BTC P2PKH U  : %s\n", dinfo.btc_p2pkh_uncompressed.c_str());
    printf("  BTC P2SH-WPKH: %s\n", dinfo.btc_p2sh_p2wpkh.c_str());
    printf("  BTC Bech32   : %s\n", dinfo.btc_p2wpkh_bech32.c_str());
    printf("  ETH Address  : %s\n", dinfo.eth_address.c_str());
    char rich[1024];
    snprintf(rich, sizeof(rich),
             "[%s] priv=%s BTC-1=%s BTC-3=%s BTC-bc1=%s ETH=%s",
             currency,
             dinfo.privkey_hex.c_str(),
             dinfo.btc_p2pkh_compressed.c_str(),
             dinfo.btc_p2sh_p2wpkh.c_str(),
             dinfo.btc_p2wpkh_bech32.c_str(),
             dinfo.eth_address.c_str());
    pthread_mutex_lock(&g_hit_mutex);
    g_last_hits.push_back(std::string(rich));
    if (g_last_hits.size() > 100) g_last_hits.erase(g_last_hits.begin());
    pthread_mutex_unlock(&g_hit_mutex);
}
static void process_pub(Point &pub, Int &priv, int subrange_id, int thread_id) {
    if (!g_secp) return;
    uint8_t h160_btc[HASH160_LEN];
    uint8_t h160_eth[HASH160_LEN];
    g_secp->GetHash160(P2PKH, true, pub, h160_btc);
    unsigned char pub_bytes[64];
    pub.x.Get32Bytes(pub_bytes);
    pub.y.Get32Bytes(pub_bytes + 32);
    SHA3_256_CTX ctx_keccak;
    KECCAK_256_Init(&ctx_keccak);
    KECCAK_256_Update(&ctx_keccak, pub_bytes, 64);
    unsigned char keccak_out[32];
    KECCAK_256_Final(keccak_out, &ctx_keccak);
    memcpy(h160_eth, keccak_out + 12, 20);
    g_derive_count.fetch_add(2);
    auto check_and_report = [&](const uint8_t* hash, const char* currency) {
        if (!xor_query(hash)) return;
        if (!bin_search(hash)) {
            g_xor_fp.fetch_add(1);
            return;
        }
        char priv_hex[80];
        priv_to_hex(priv, priv_hex);
        char hash_hex[48];
        hash_to_hex(hash, hash_hex);
        pthread_mutex_lock(&g_hit_mutex);
        std::string key = std::string(currency) + ":" + hash_hex;
        if (g_seen.find(key) == g_seen.end()) {
            g_seen.insert(key);
            char ts[32];
            timestamp_utc(ts, sizeof(ts));
            extended_write_found(hash_hex, priv, pub, currency, subrange_id, thread_id, ts);
            (void)priv_hex;
            g_found.fetch_add(1);
        }
        pthread_mutex_unlock(&g_hit_mutex);
    };
    check_and_report(h160_btc, "BTC");
    check_and_report(h160_eth, "ETH");
}
typedef struct {
    Int lambda;
    Int lambda2mod;
    Int beta;
    Int beta2;
    Int order;
} curve_constants_t;
static void init_curve_constants(curve_constants_t *c) {
    c->order.SetBase16(ORDER_HEX);
    c->beta.SetBase16(BETA_HEX);
    c->beta2.Set(&c->beta);
    c->beta2.ModMulK1(&c->beta2, &c->beta);
    c->lambda.SetBase16(LAMBDA_HEX);
    Int temp;
    temp.Set(&c->lambda);
    temp.Mult(&c->lambda);
    c->lambda2mod.Set(&temp);
    c->lambda2mod.Mod(&c->order);
}
static void scalar_add_mod(Int &r, Int *a, Int *n) {
    r.Add(a);
    while (r.IsGreaterOrEqual(n)) {
        r.Sub(n);
    }
}
static void scalar_sub_mod(Int &r, Int *a, Int *n) {
    if (r.IsLower(a)) {
        Int tmp(n);
        tmp.Sub(a);
        r.Add(&tmp);
    } else {
        r.Sub(a);
    }
}
static void subrange_bounds(int bits, int sub_id, int sub_total, Int *out_start, Int *out_end) {
    Int base_start, base_end, span, divisor, chunk, s, e;
    base_start.SetInt32(1);
    base_start.ShiftL(bits - 1);
    base_end.SetInt32(1);
    base_end.ShiftL(bits);
    base_end.Sub((uint32_t)1);
    span.Sub(&base_end, &base_start);
    span.Add((uint32_t)1);
    divisor.SetInt32(sub_total);
    chunk.Set(&span);
    chunk.Div(&divisor);
    s.Set(&chunk);
    s.Mult((uint64_t)sub_id);
    s.Add(&base_start);
    e.Set(&chunk);
    e.Mult((uint64_t)(sub_id + 1));
    e.Add(&base_start);
    e.Sub((uint32_t)1);
    if (sub_id == sub_total - 1) e.Set(&base_end);
    out_start->Set(&s);
    out_end->Set(&e);
}
static void derive_triplet(Int &priv, Point &p1, Point &p2, Point &p3,
                           Int &priv2, Int &priv3,
                           curve_constants_t *c) {
    p1 = g_secp->ComputePublicKey(&priv);
    p1.Reduce();
    p2.x.ModMulK1(&p1.x, &c->beta);
    p2.y.Set(&p1.y);
    p2.z.Set(&p1.z);
    p3.x.ModMulK1(&p1.x, &c->beta2);
    p3.y.Set(&p1.y);
    p3.z.Set(&p1.z);
    priv2.Set(&priv);
    priv2.ModMulK1order(&c->lambda);
    priv3.Set(&priv);
    priv3.ModMulK1order(&c->lambda2mod);
}
static void update_endomorphisms(Point &p1, Point &p2, Point &p3, curve_constants_t &c) {
    p2.x.ModMulK1(&p1.x, &c.beta);
    p2.y.Set(&p1.y);
    p2.z.Set(&p1.z);
    p3.x.ModMulK1(&p1.x, &c.beta2);
    p3.y.Set(&p1.y);
    p3.z.Set(&p1.z);
}
typedef struct {
    int thread_id;
    int subrange_id;
    int mode;
    int range_bits;
    int subranges;
    Int start;
    Int end;
} thread_arg_t;
static void apply_triplet_batch_inversion(IntGroup &batch_group, Int *z_values, Point *p1_batch, Point *p2_batch, Point *p3_batch, int count) {
    if (count <= 0) return;
    batch_group.Set(z_values);
    batch_group.ModInv(count);
    for (int i = 0; i < count; i++) {
        p1_batch[i].x.ModMulK1(&p1_batch[i].x, &z_values[i]);
        p1_batch[i].y.ModMulK1(&p1_batch[i].y, &z_values[i]);
        p1_batch[i].z.SetInt32(1);
        p2_batch[i].x.ModMulK1(&p2_batch[i].x, &z_values[i]);
        p2_batch[i].y.ModMulK1(&p2_batch[i].y, &z_values[i]);
        p2_batch[i].z.SetInt32(1);
        p3_batch[i].x.ModMulK1(&p3_batch[i].x, &z_values[i]);
        p3_batch[i].y.ModMulK1(&p3_batch[i].y, &z_values[i]);
        p3_batch[i].z.SetInt32(1);
    }
}
static void run_seq_fwd(thread_arg_t *a) {
    Int one((uint32_t)1);
    curve_constants_t c;
    init_curve_constants(&c);
    Int cur;
    cur.Set(&a->start);
    Point p1, p2, p3;
    Int priv2, priv3;
    derive_triplet(cur, p1, p2, p3, priv2, priv3, &c);
    g_thread_subrange[a->thread_id].store(a->subrange_id);
    IntGroup batch_group(32);
    Int z_p1[32];
    Point p1_batch[32];
    Point p2_batch[32];
    Point p3_batch[32];
    Int priv1_batch[32];
    Int priv2_batch[32];
    Int priv3_batch[32];
    int batch_count = 0;
    while (!g_stop.load()) {
        p1_batch[batch_count] = p1;
        p2_batch[batch_count] = p2;
        p3_batch[batch_count] = p3;
        priv1_batch[batch_count].Set(&cur);
        priv2_batch[batch_count].Set(&priv2);
        priv3_batch[batch_count].Set(&priv3);
        z_p1[batch_count].Set(&p1.z);
        batch_count++;
        if (cur.IsEqual(&a->end)) {
            apply_triplet_batch_inversion(batch_group, z_p1, p1_batch, p2_batch, p3_batch, batch_count);
            for (int i = 0; i < batch_count; i++) {
                process_pub(p1_batch[i], priv1_batch[i], a->subrange_id, a->thread_id);
                process_pub(p2_batch[i], priv2_batch[i], a->subrange_id, a->thread_id);
                process_pub(p3_batch[i], priv3_batch[i], a->subrange_id, a->thread_id);
            }
            g_thread_keys[a->thread_id].fetch_add((uint64_t)batch_count * 3);
            g_checked.fetch_add((uint64_t)batch_count * 3);
            g_fwd_checked.fetch_add((uint64_t)batch_count * 3);
            batch_count = 0;
            break;
        } else {
            if (batch_count >= 32) {
                apply_triplet_batch_inversion(batch_group, z_p1, p1_batch, p2_batch, p3_batch, batch_count);
                for (int i = 0; i < batch_count; i++) {
                    process_pub(p1_batch[i], priv1_batch[i], a->subrange_id, a->thread_id);
                    process_pub(p2_batch[i], priv2_batch[i], a->subrange_id, a->thread_id);
                    process_pub(p3_batch[i], priv3_batch[i], a->subrange_id, a->thread_id);
                }
                g_thread_keys[a->thread_id].fetch_add((uint64_t)batch_count * 3);
                g_checked.fetch_add((uint64_t)batch_count * 3);
                g_fwd_checked.fetch_add((uint64_t)batch_count * 3);
                batch_count = 0;
            }
            cur.Add(&one);
            p1 = g_secp->NextKey(p1);
            update_endomorphisms(p1, p2, p3, c);
            scalar_add_mod(priv2, &c.lambda, &c.order);
            scalar_add_mod(priv3, &c.lambda2mod, &c.order);
        }
    }
    if (batch_count > 0) {
        apply_triplet_batch_inversion(batch_group, z_p1, p1_batch, p2_batch, p3_batch, batch_count);
        for (int i = 0; i < batch_count; i++) {
            process_pub(p1_batch[i], priv1_batch[i], a->subrange_id, a->thread_id);
            process_pub(p2_batch[i], priv2_batch[i], a->subrange_id, a->thread_id);
            process_pub(p3_batch[i], priv3_batch[i], a->subrange_id, a->thread_id);
        }
        g_thread_keys[a->thread_id].fetch_add((uint64_t)batch_count * 3);
        g_checked.fetch_add((uint64_t)batch_count * 3);
        g_fwd_checked.fetch_add((uint64_t)batch_count * 3);
        usleep(1000);
        batch_count = 0;
    }
}
static void run_bidir_quad(thread_arg_t *a) {
    Int one((uint32_t)1);
    Point negG = g_secp->Negation(g_secp->G);
    curve_constants_t c;
    init_curve_constants(&c);

    int num_frentes = (g_threads >= 8) ? (g_threads / 2) : 2;
    if (g_threads >= 8 && num_frentes < 4) num_frentes = 4;
    if (num_frentes > 12) num_frentes = 12;
    if (num_frentes % 2 != 0) num_frentes--;
    if (num_frentes < 2) num_frentes = 2;

    int frente_id = a->thread_id % num_frentes;
    Int span_bidir;
    span_bidir.Sub(&a->end, &a->start);
    Int divisor_frentes((uint32_t)num_frentes);
    Int paso_frente;
    paso_frente.Set(&span_bidir);
    paso_frente.Div(&divisor_frentes);
    Int offset_frente;
    offset_frente.Set(&paso_frente);
    offset_frente.Mult((uint64_t)frente_id);

    Int cur;
    cur.Set(&a->start);
    cur.Add(&offset_frente);
    int dir = (frente_id % 2 == 0) ? 1 : -1;
    Point p1, p2, p3;
    Int priv2, priv3;
    derive_triplet(cur, p1, p2, p3, priv2, priv3, &c);
    g_thread_subrange[a->thread_id].store(a->subrange_id);
    IntGroup batch_group(32);
    Int z_p1[32];
    Point p1_batch[32];
    Point p2_batch[32];
    Point p3_batch[32];
    Int priv1_batch[32];
    Int priv2_batch[32];
    Int priv3_batch[32];
    int batch_count = 0;
    while (!g_stop.load()) {
        p1_batch[batch_count] = p1;
        p2_batch[batch_count] = p2;
        p3_batch[batch_count] = p3;
        priv1_batch[batch_count].Set(&cur);
        priv2_batch[batch_count].Set(&priv2);
        priv3_batch[batch_count].Set(&priv3);
        z_p1[batch_count].Set(&p1.z);
        batch_count++;
        if (dir == 1) {
            if (cur.IsGreaterOrEqual(&a->end)) {
                break;
            } else {
                cur.Add(&one);
                p1 = g_secp->NextKey(p1);
                update_endomorphisms(p1, p2, p3, c);
                scalar_add_mod(priv2, &c.lambda, &c.order);
                scalar_add_mod(priv3, &c.lambda2mod, &c.order);
            }
        } else {
            if (cur.IsLowerOrEqual(&a->start)) {
                break;
            } else {
                cur.Sub((uint32_t)1);
                p1 = g_secp->Add(p1, negG);
                update_endomorphisms(p1, p2, p3, c);
                scalar_sub_mod(priv2, &c.lambda, &c.order);
                scalar_sub_mod(priv3, &c.lambda2mod, &c.order);
            }
        }
        if (batch_count >= 32) {
            apply_triplet_batch_inversion(batch_group, z_p1, p1_batch, p2_batch, p3_batch, batch_count);
            for (int i = 0; i < batch_count; i++) {
                process_pub(p1_batch[i], priv1_batch[i], a->subrange_id, a->thread_id);
                process_pub(p2_batch[i], priv2_batch[i], a->subrange_id, a->thread_id);
                process_pub(p3_batch[i], priv3_batch[i], a->subrange_id, a->thread_id);
            }
            g_thread_keys[a->thread_id].fetch_add((uint64_t)batch_count * 3);
            g_checked.fetch_add((uint64_t)batch_count * 3);
            if (dir == 1) g_fwd_checked.fetch_add((uint64_t)batch_count * 3);
            else g_bwd_checked.fetch_add((uint64_t)batch_count * 3);
            batch_count = 0;
        }
    }
    if (batch_count > 0) {
        apply_triplet_batch_inversion(batch_group, z_p1, p1_batch, p2_batch, p3_batch, batch_count);
        for (int i = 0; i < batch_count; i++) {
            process_pub(p1_batch[i], priv1_batch[i], a->subrange_id, a->thread_id);
            process_pub(p2_batch[i], priv2_batch[i], a->subrange_id, a->thread_id);
            process_pub(p3_batch[i], priv3_batch[i], a->subrange_id, a->thread_id);
        }
        g_thread_keys[a->thread_id].fetch_add((uint64_t)batch_count * 3);
        g_checked.fetch_add((uint64_t)batch_count * 3);
        usleep(1000);
        batch_count = 0;
    }
}
static void run_randoms(thread_arg_t *a) {
    Int span;
    span.Sub(&a->end, &a->start);
    if (span.IsZero()) { run_bidir_quad(a); return; }
    Point negG = g_secp->Negation(g_secp->G);
    Int one((uint32_t)1);
    curve_constants_t c;
    init_curve_constants(&c);
    g_thread_subrange[a->thread_id].store(a->subrange_id);
    Point p1_up, p2_up, p3_up;
    Point p1_dn, p2_dn, p3_dn;
    Int priv2_up, priv3_up;
    Int priv2_dn, priv3_dn;
    IntGroup batch_group(32);
    Int z_up[32];
    Int z_dn[32];
    Point p1_up_batch[32], p2_up_batch[32], p3_up_batch[32];
    Int priv1_up_batch[32], priv2_up_batch[32], priv3_up_batch[32];
    Point p1_dn_batch[32], p2_dn_batch[32], p3_dn_batch[32];
    Int priv1_dn_batch[32], priv2_dn_batch[32], priv3_dn_batch[32];
    while (!g_stop.load()) {
        time_t slot_start = time(nullptr);
        Int center;
        center.Rand(&a->start, &a->end);
        Int up; up.Set(&center);
        Int dn; dn.Set(&center);
        derive_triplet(up, p1_up, p2_up, p3_up, priv2_up, priv3_up, &c);
        p1_dn.Set(p1_up);
        p2_dn.Set(p2_up);
        p3_dn.Set(p3_up);
        priv2_dn.Set(&priv2_up);
        priv3_dn.Set(&priv3_up);
        int up_count = 0;
        int dn_count = 0;
        bool up_active = true;
        bool dn_active = true;
        while (!g_stop.load()) {
            if ((int)(time(nullptr) - slot_start) >= 25) break;
            if (!up_active && !dn_active) break;
            if (up_active) {
                p1_up_batch[up_count] = p1_up;
                p2_up_batch[up_count] = p2_up;
                p3_up_batch[up_count] = p3_up;
                priv1_up_batch[up_count].Set(&up);
                priv2_up_batch[up_count].Set(&priv2_up);
                priv3_up_batch[up_count].Set(&priv3_up);
                z_up[up_count].Set(&p1_up.z);
                up_count++;
                if (up.IsEqual(&a->end)) {
                    up_active = false;
                } else {
                    up.Add(&one);
                    p1_up = g_secp->NextKey(p1_up);
                    update_endomorphisms(p1_up, p2_up, p3_up, c);
                    scalar_add_mod(priv2_up, &c.lambda, &c.order);
                    scalar_add_mod(priv3_up, &c.lambda2mod, &c.order);
                }
            }
            if (dn_active) {
                p1_dn_batch[dn_count] = p1_dn;
                p2_dn_batch[dn_count] = p2_dn;
                p3_dn_batch[dn_count] = p3_dn;
                priv1_dn_batch[dn_count].Set(&dn);
                priv2_dn_batch[dn_count].Set(&priv2_dn);
                priv3_dn_batch[dn_count].Set(&priv3_dn);
                z_dn[dn_count].Set(&p1_dn.z);
                dn_count++;
                if (dn.IsEqual(&a->start)) {
                    dn_active = false;
                } else {
                    dn.Sub((uint32_t)1);
                    p1_dn = g_secp->Add(p1_dn, negG);
                    update_endomorphisms(p1_dn, p2_dn, p3_dn, c);
                    scalar_sub_mod(priv2_dn, &c.lambda, &c.order);
                    scalar_sub_mod(priv3_dn, &c.lambda2mod, &c.order);
                }
            }
            if (up_count >= 32 || dn_count >= 32 || (!up_active && !dn_active)) {
                if (up_count > 0) {
                    apply_triplet_batch_inversion(batch_group, z_up, p1_up_batch, p2_up_batch, p3_up_batch, up_count);
                    for (int i = 0; i < up_count; i++) {
                        process_pub(p1_up_batch[i], priv1_up_batch[i], a->subrange_id, a->thread_id);
                        process_pub(p2_up_batch[i], priv2_up_batch[i], a->subrange_id, a->thread_id);
                        process_pub(p3_up_batch[i], priv3_up_batch[i], a->subrange_id, a->thread_id);
                    }
                    g_thread_keys[a->thread_id].fetch_add((uint64_t)up_count * 3);
                    g_checked.fetch_add((uint64_t)up_count * 3);
                    up_count = 0;
                }
                if (dn_count > 0) {
                    apply_triplet_batch_inversion(batch_group, z_dn, p1_dn_batch, p2_dn_batch, p3_dn_batch, dn_count);
                    for (int i = 0; i < dn_count; i++) {
                        process_pub(p1_dn_batch[i], priv1_dn_batch[i], a->subrange_id, a->thread_id);
                        process_pub(p2_dn_batch[i], priv2_dn_batch[i], a->subrange_id, a->thread_id);
                        process_pub(p3_dn_batch[i], priv3_dn_batch[i], a->subrange_id, a->thread_id);
                    }
                    g_thread_keys[a->thread_id].fetch_add((uint64_t)dn_count * 3);
                    g_checked.fetch_add((uint64_t)dn_count * 3);
                    dn_count = 0;
                }
            }
        }
        if (up_count > 0) {
            apply_triplet_batch_inversion(batch_group, z_up, p1_up_batch, p2_up_batch, p3_up_batch, up_count);
            for (int i = 0; i < up_count; i++) {
                process_pub(p1_up_batch[i], priv1_up_batch[i], a->subrange_id, a->thread_id);
                process_pub(p2_up_batch[i], priv2_up_batch[i], a->subrange_id, a->thread_id);
                process_pub(p3_up_batch[i], priv3_up_batch[i], a->subrange_id, a->thread_id);
            }
            g_thread_keys[a->thread_id].fetch_add((uint64_t)up_count * 3);
            g_checked.fetch_add((uint64_t)up_count * 3);
            up_count = 0;
        }
        if (dn_count > 0) {
            apply_triplet_batch_inversion(batch_group, z_dn, p1_dn_batch, p2_dn_batch, p3_dn_batch, dn_count);
            for (int i = 0; i < dn_count; i++) {
                process_pub(p1_dn_batch[i], priv1_dn_batch[i], a->subrange_id, a->thread_id);
                process_pub(p2_dn_batch[i], priv2_dn_batch[i], a->subrange_id, a->thread_id);
                process_pub(p3_dn_batch[i], priv3_dn_batch[i], a->subrange_id, a->thread_id);
            }
            g_thread_keys[a->thread_id].fetch_add((uint64_t)dn_count * 3);
            g_checked.fetch_add((uint64_t)dn_count * 3);
            dn_count = 0;
        }
    }
}
static void run_pure(thread_arg_t *a) {
    Int one((uint32_t)1);
    Point negG = g_secp->Negation(g_secp->G);
    curve_constants_t c;
    init_curve_constants(&c);
    g_thread_subrange[a->thread_id].store(a->subrange_id);
    Point p1_up, p2_up, p3_up;
    Point p1_dn, p2_dn, p3_dn;
    Int priv2_up, priv3_up;
    Int priv2_dn, priv3_dn;
    IntGroup batch_group(32);
    Int z_up[32];
    Int z_dn[32];
    Point p1_up_batch[32], p2_up_batch[32], p3_up_batch[32];
    Int priv1_up_batch[32], priv2_up_batch[32], priv3_up_batch[32];
    Point p1_dn_batch[32], p2_dn_batch[32], p3_dn_batch[32];
    Int priv1_dn_batch[32], priv2_dn_batch[32], priv3_dn_batch[32];
    while (!g_stop.load()) {
        time_t pure_slot_start = time(nullptr);
        Int s, e;
        Int rand_bits;
        Int min_b((uint32_t)128), max_b((uint32_t)256);
        rand_bits.Rand(&min_b, &max_b);
        int rbits = (int)rand_bits.GetInt32();
        if (rbits < 128) rbits = 128;
        if (rbits > 256) rbits = 256;
        Int rand_sub;
        Int min_s((uint32_t)0), max_s((uint32_t)99);
        rand_sub.Rand(&min_s, &max_s);
        int sid = (int)rand_sub.GetInt32();
        if (sid < 0) sid = 0;
        if (sid > 99) sid = 99;
        g_current_subrange.store(sid);
        subrange_bounds(rbits, sid, 100, &s, &e);
        a->range_bits = rbits;
        a->subrange_id = sid;
        g_range_bits.store(rbits);
        Int center;
        center.Rand(&s, &e);
        Int up; up.Set(&center);
        Int dn; dn.Set(&center);
        dn.Sub((uint32_t)1);
        derive_triplet(up, p1_up, p2_up, p3_up, priv2_up, priv3_up, &c);
        derive_triplet(dn, p1_dn, p2_dn, p3_dn, priv2_dn, priv3_dn, &c);

        int batch_count = 0;
        while (!g_stop.load()) {
            if ((int)(time(nullptr) - pure_slot_start) >= 25) break;

            p1_up_batch[batch_count] = p1_up;
            p2_up_batch[batch_count] = p2_up;
            p3_up_batch[batch_count] = p3_up;
            priv1_up_batch[batch_count].Set(&up);
            priv2_up_batch[batch_count].Set(&priv2_up);
            priv3_up_batch[batch_count].Set(&priv3_up);

            p1_dn_batch[batch_count] = p1_dn;
            p2_dn_batch[batch_count] = p2_dn;
            p3_dn_batch[batch_count] = p3_dn;
            priv1_dn_batch[batch_count].Set(&dn);
            priv2_dn_batch[batch_count].Set(&priv2_dn);
            priv3_dn_batch[batch_count].Set(&priv3_dn);

            z_up[batch_count].Set(&p1_up.z);
            z_dn[batch_count].Set(&p1_dn.z);
            batch_count++;

            if (batch_count >= 32) {
                apply_triplet_batch_inversion(batch_group, z_up, p1_up_batch, p2_up_batch, p3_up_batch, batch_count);
                apply_triplet_batch_inversion(batch_group, z_dn, p1_dn_batch, p2_dn_batch, p3_dn_batch, batch_count);
                for (int i = 0; i < batch_count; i++) {
                    process_pub(p1_up_batch[i], priv1_up_batch[i], sid, a->thread_id);
                    process_pub(p2_up_batch[i], priv2_up_batch[i], sid, a->thread_id);
                    process_pub(p3_up_batch[i], priv3_up_batch[i], sid, a->thread_id);
                    process_pub(p1_dn_batch[i], priv1_dn_batch[i], sid, a->thread_id);
                    process_pub(p2_dn_batch[i], priv2_dn_batch[i], sid, a->thread_id);
                    process_pub(p3_dn_batch[i], priv3_dn_batch[i], sid, a->thread_id);
                }
                g_thread_keys[a->thread_id].fetch_add((uint64_t)batch_count * 6);
                g_checked.fetch_add((uint64_t)batch_count * 6);
                batch_count = 0;
            }

            up.Add(&one);
            p1_up = g_secp->NextKey(p1_up);
            update_endomorphisms(p1_up, p2_up, p3_up, c);
            scalar_add_mod(priv2_up, &c.lambda, &c.order);
            scalar_add_mod(priv3_up, &c.lambda2mod, &c.order);

            dn.Sub((uint32_t)1);
            p1_dn = g_secp->Add(p1_dn, negG);
            update_endomorphisms(p1_dn, p2_dn, p3_dn, c);
            scalar_sub_mod(priv2_dn, &c.lambda, &c.order);
            scalar_sub_mod(priv3_dn, &c.lambda2mod, &c.order);
        }
        if (batch_count > 0) {
            apply_triplet_batch_inversion(batch_group, z_up, p1_up_batch, p2_up_batch, p3_up_batch, batch_count);
            apply_triplet_batch_inversion(batch_group, z_dn, p1_dn_batch, p2_dn_batch, p3_dn_batch, batch_count);
            for (int i = 0; i < batch_count; i++) {
                process_pub(p1_up_batch[i], priv1_up_batch[i], sid, a->thread_id);
                process_pub(p2_up_batch[i], priv2_up_batch[i], sid, a->thread_id);
                process_pub(p3_up_batch[i], priv3_up_batch[i], sid, a->thread_id);
                process_pub(p1_dn_batch[i], priv1_dn_batch[i], sid, a->thread_id);
                process_pub(p2_dn_batch[i], priv2_dn_batch[i], sid, a->thread_id);
                process_pub(p3_dn_batch[i], priv3_dn_batch[i], sid, a->thread_id);
            }
            g_thread_keys[a->thread_id].fetch_add((uint64_t)batch_count * 6);
            g_checked.fetch_add((uint64_t)batch_count * 6);
            batch_count = 0;
        }
        g_pure_cycle.fetch_add(1);
    }
}
static void run_subrango_random(thread_arg_t *a) {
    run_randoms(a);
}
static void pin_to_big_cores(void) {
#if defined(__linux__) && !defined(__ANDROID__)
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    int num_cpus = sysconf(_SC_NPROCESSORS_ONLN);
    if (num_cpus <= 0) return;
    for (int i = 0; i < num_cpus; ++i) {
        char path[256];
        snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpu%d/cpufreq/cpuinfo_max_freq", i);
        FILE *f = fopen(path, "r");
        if (f) {
            long freq = 0;
            if (fscanf(f, "%ld", &freq) == 1) {
                if (freq > 1500000) CPU_SET(i, &cpuset);
            }
            fclose(f);
        }
    }
    if (CPU_COUNT(&cpuset) > 0) {
        pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
    }
#endif
}
static void* worker(void *arg) {
    thread_arg_t *a = (thread_arg_t*)arg;
    pin_to_big_cores();
    g_thread_active[a->thread_id].store(1);
    g_thread_keys[a->thread_id].store(0);
    switch (a->mode) {
        case 1: run_seq_fwd(a); break;
        case 2: run_bidir_quad(a); break;
        case 3: run_randoms(a); break;
        case 4: run_pure(a); break;
        case 5: run_subrango_random(a); break;
        default: run_seq_fwd(a); break;
    }
    g_thread_active[a->thread_id].store(0);
    return nullptr;
}
static void* monitor_worker(void *arg) {
    (void)arg;
    while (!g_stop.load()) {
        ui_draw();
        usleep(100000);
        std::unique_lock<std::mutex> lock(g_monitor_mutex);
        g_ui_cv.wait_for(lock, std::chrono::milliseconds(49900), [](){ return g_stop.load() != 0; });
    }
    return nullptr;
}
static void on_sigint(int) {
    g_stop.store(1);
    g_ui_cv.notify_all();
}
static void usage(const char *prog) {
    fprintf(stderr, "================================================================================\n");
    fprintf(stderr, "<x> BTC-MOBILE <x> By Zoey Alejandroo JF <x>\n");
    fprintf(stderr, "================================================================================\n");
    fprintf(stderr, "USAGE:\n");
    fprintf(stderr, "  %s -R <1-256> -S\n", prog);
    fprintf(stderr, "      Sequential forward brute force in selected range.\n");
    fprintf(stderr, "  %s -R <1-256> -B\n", prog);
    fprintf(stderr, "      Bidirectional search with 4 strategies anchored by threads.\n");
    fprintf(stderr, "  %s -R <1-256> -Random\n", prog);
    fprintf(stderr, "      Random position jumps every 25 seconds within range.\n");
    fprintf(stderr, "  %s -Pure\n", prog);
    fprintf(stderr, "      Pure random across full secp256k1 space, 100 subranges, 24 cycles.\n");
    fprintf(stderr, "  %s -R <1-256> -Subrango -Random\n", prog);
    fprintf(stderr, "      Split range into 100 subranges and search randomly.\n");
    fprintf(stderr, "--------------------------------------------------------------------------------\n");
    fprintf(stderr, "OPTIONS:\n");
    fprintf(stderr, "  -R N         Range bits between 1 and 256\n");
    fprintf(stderr, "  -S           Sequential forward mode\n");
    fprintf(stderr, "  -B           Bidirectional quad-strategy mode\n");
    fprintf(stderr, "  -Random      Random jump mode\n");
    fprintf(stderr, "  -Pure        Pure random global mode (no -R or -Subrango)\n");
    fprintf(stderr, "  -Subrango    Split range into 100 divisions\n");
    fprintf(stderr, "  -t N         Thread count between 1 and 64\n");
    fprintf(stderr, "================================================================================\n");
}
static int parse_int(const char *s, int *out) {
    if (!s || !*s) return 0;
    char *end = nullptr;
    long v = strtol(s, &end, 10);
    if (*end != 0) return 0;
    *out = (int)v;
    return 1;
}
static bool select_storage_mode(const char* filename) {
    fprintf(stdout, "Select storage mode for %s:\n", filename);
    fprintf(stdout, "1. RAM (Ultra Fast, High Memory Usage)\n");
    fprintf(stdout, "2. DISK (Streaming/Mmap, Low Memory Usage)\n");
    fprintf(stdout, "Enter choice (1/2): ");
    fflush(stdout);
    char choice_buf[16];
    if (fgets(choice_buf, sizeof(choice_buf), stdin)) {
        int choice = atoi(choice_buf);
        return (choice == 1);
    }
    return false;
}
static void int_selfcheck(void) {
    if (SHA3_Selftest() != 0) {
        fprintf(stderr, "error: SHA3 self-test failed\n");
        exit(1);
    }
}
int main(int argc, char **argv) {
    int_selfcheck();
    if (argc < 2) {
        usage(argv[0]);
        return 1;
    }
    int seen_S = 0, seen_B = 0, seen_Randoms = 0, seen_Pure = 0;
    int seen_R = 0, seen_Sub = 0, seen_t = 0;
    int range_value = 0, thread_value = 0;
    for (int i = 1; i < argc; i++) {
        const char *s = argv[i];
        if (strcmp(s, "-S") == 0) seen_S = 1;
        else if (strcmp(s, "-B") == 0) seen_B = 1;
        else if (strcmp(s, "-Random") == 0) seen_Randoms = 1;
        else if (strcmp(s, "-Pure") == 0) seen_Pure = 1;
        else if (strcmp(s, "-Subrango") == 0) seen_Sub = 1;
        else if (strcmp(s, "-R") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "error: -R requires a numeric argument\n");
                usage(argv[0]);
                return 1;
            }
            int v = 0;
            if (!parse_int(argv[i + 1], &v)) {
                fprintf(stderr, "error: -R argument '%s' is not a valid integer\n", argv[i + 1]);
                usage(argv[0]);
                return 1;
            }
            if (v < 1 || v > 256) {
                fprintf(stderr, "error: -R value %d is out of range, must be between 1 and 256\n", v);
                usage(argv[0]);
                return 1;
            }
            range_value = v;
            seen_R = 1;
            i++;
        }
        else if (strcmp(s, "-t") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "error: -t requires a numeric argument\n");
                usage(argv[0]);
                return 1;
            }
            int v = 0;
            if (!parse_int(argv[i + 1], &v)) {
                fprintf(stderr, "error: -t argument '%s' is not a valid integer\n", argv[i + 1]);
                usage(argv[0]);
                return 1;
            }
            if (v < 1 || v > 64) {
                fprintf(stderr, "error: -t value %d is out of range, must be between 1 and 64\n", v);
                usage(argv[0]);
                return 1;
            }
            thread_value = v;
            seen_t = 1;
            i++;
        }
        else {
            fprintf(stderr, "error: unknown command flag '%s'\n", s);
            usage(argv[0]);
            return 1;
        }
    }
    int mode_flags = seen_S + seen_B + seen_Randoms + seen_Pure;
    if (mode_flags == 0) {
        fprintf(stderr, "error: no search mode specified\n");
        usage(argv[0]);
        return 1;
    }
    if (mode_flags > 1) {
        fprintf(stderr, "error: conflicting search modes, only one allowed\n");
        usage(argv[0]);
        return 1;
    }
    if (seen_Pure) {
        g_mode = 4;
        if (seen_R) {
            fprintf(stderr, "error: -Pure does not accept -R\n");
            usage(argv[0]);
            return 1;
        }
        if (seen_Sub) {
            fprintf(stderr, "error: -Pure does not accept -Subrango\n");
            usage(argv[0]);
            return 1;
        }
    } else if (seen_S) {
        g_mode = 1;
        if (!seen_R) {
            fprintf(stderr, "error: -S requires -R N\n");
            usage(argv[0]);
            return 1;
        }
    } else if (seen_B) {
        g_mode = 2;
        if (!seen_R) {
            fprintf(stderr, "error: -B requires -R N\n");
            usage(argv[0]);
            return 1;
        }
    } else if (seen_Randoms) {
        if (seen_Sub) g_mode = 5;
        else g_mode = 3;
        if (!seen_R) {
            fprintf(stderr, "error: -Random requires -R N\n");
            usage(argv[0]);
            return 1;
        }
    }
    if (seen_R) g_range_bits.store(range_value);
    if (seen_Sub) g_subranges = 100;
    else if (g_mode != 4) g_subranges = 1;
    if (seen_t) g_threads = thread_value;
    else {
        long c = sysconf(_SC_NPROCESSORS_ONLN);
        if (c < 1) c = 1;
        g_threads = (int)(c > 1 ? c - 1 : 1);
        if (g_threads > THREAD_MAX) g_threads = THREAD_MAX;
    }
    if (g_mode == 4) {
        g_range_bits.store(256);
        g_subranges = 100;
    }
    g_use_ram_bin = select_storage_mode("BTC.bin");
    g_use_ram_idx = select_storage_mode("BTC.idx");
    g_use_ram_xor = select_storage_mode("BTC.xor");
    if (validate_and_load_bin() != 0) { free_all(); return 1; }
    if (validate_and_load_idx() != 0) { free_all(); return 1; }
    if (validate_and_load_xor() != 0) { free_all(); return 1; }
    signal(SIGINT, on_sigint);
    signal(SIGTERM, on_sigint);
    int_randominit();
    srand((unsigned int)time(nullptr));
    {
        std::lock_guard<std::mutex> lock(g_secp_init_mutex);
        if (!g_secp) {
            g_secp = new Secp256K1();
            g_secp->Init();
        }
    }
    g_start = time(nullptr);
    g_checked.store(0);
    g_found.store(0);
    g_derive_count.store(0);
    g_xor_queries.store(0);
    g_xor_hits.store(0);
    g_xor_fp.store(0);
    g_pure_cycle.store(0);
    g_fwd_checked.store(0);
    g_bwd_checked.store(0);
    g_current_subrange.store(0);
    for (int i = 0; i < THREAD_MAX; i++) {
        g_thread_active[i].store(0);
        g_thread_subrange[i].store(0);
        g_thread_keys[i].store(0);
    }
    if (g_mode == 4) {
        g_global_start.SetInt32(1);
        g_global_start.ShiftL(255);
        g_global_end.SetInt32(1);
        g_global_end.ShiftL(256);
        g_global_end.Sub((uint32_t)1);
    } else {
        g_global_start.SetInt32(1);
        g_global_start.ShiftL(g_range_bits.load() - 1);
        g_global_end.SetInt32(1);
        g_global_end.ShiftL(g_range_bits.load());
        g_global_end.Sub((uint32_t)1);
    }
    ui_enter();
    pthread_t monitor_tid;
    pthread_create(&monitor_tid, nullptr, monitor_worker, nullptr);
    thread_arg_t *args = new thread_arg_t[g_threads];
    pthread_t *tids = new pthread_t[g_threads];
    for (int i = 0; i < g_threads; i++) {
        int sid = i % g_subranges;
        Int s, e;
        if (g_mode == 4) {
            s.SetInt32(1);
            s.ShiftL(255);
            e.SetInt32(1);
            e.ShiftL(256);
            e.Sub((uint32_t)1);
        } else if (g_mode == 2 && g_subranges == 1) {
            s.Set(&g_global_start);
            e.Set(&g_global_end);
        } else {
            subrange_bounds(g_range_bits.load(), sid, g_subranges, &s, &e);
        }
        args[i].thread_id = i;
        args[i].subrange_id = sid;
        args[i].mode = g_mode;
        args[i].range_bits = g_range_bits.load();
        args[i].subranges = g_subranges;
        args[i].start.Set(&s);
        args[i].end.Set(&e);
        pthread_create(&tids[i], nullptr, worker, &args[i]);
    }
    ui_draw();
    for (int i = 0; i < g_threads; i++) pthread_join(tids[i], nullptr);
    g_stop.store(1);
    g_ui_cv.notify_all();
    pthread_join(monitor_tid, nullptr);
    ui_leave();
    fprintf(stderr, "checked: %llu  found: %llu\n",
            (unsigned long long)g_checked.load(),
            (unsigned long long)g_found.load());
    delete[] tids;
    delete[] args;
    free_all();
    delete g_secp;
    return 0;
}
