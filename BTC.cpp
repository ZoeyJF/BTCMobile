#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <csignal>
#include <ctime>
#include <vector>
#include <algorithm>
#include <thread>
#include <atomic>
#include <chrono>
#include <sys/stat.h>
#include <unistd.h>

#include "Secp256k1Tool/SECP256k1.h"
#include "Secp256k1Tool/Int.h"
#include "Secp256k1Tool/Point.h"
#include "Secp256k1Tool/bloom.h"
#include "Secp256k1Tool/ripemd160.h"
#include "Secp256k1Tool/sha256.h"

static volatile bool g_running = true;

static void sig_handler(int signum) {
    if (signum == SIGINT || signum == SIGTERM) {
        g_running = false;
    }
}

enum SearchMode {
    MODE_BIDIRECTIONAL,
    MODE_SEQUENTIAL,
    MODE_RANDOM
};

struct Config {
    int range_bits = 64;
    SearchMode mode = MODE_SEQUENTIAL;
    int ram_load_option = 0;
    int num_threads = 0;
};

struct ThreadStats {
    uint64_t keys_evaluated = 0;
};

struct Hash20 {
    uint8_t data[20];
    bool operator<(const Hash20& other) const {
        return memcmp(data, other.data, 20) < 0;
    }
    bool operator==(const Hash20& other) const {
        return memcmp(data, other.data, 20) == 0;
    }
};

static std::vector<Hash20> g_bin_targets;
static struct bloom g_bloom_filter;
static bool g_bloom_loaded = false;

static std::atomic<uint64_t> g_false_positives(0);
static std::atomic<uint64_t> g_false_negatives(0);
static std::atomic<uint64_t> g_keys_found(0);

static void save_found_key(const char* priv_hex, const char* addr_hex) {
    FILE* f = fopen("FOUND.txt", "a");
    if (f) {
        fprintf(f, "Private Key: %s | Hash160: %s\n", priv_hex, addr_hex);
        fclose(f);
    }
}

static void compute_hash160(Point& pubkey, uint8_t* out_hash160) {
    uint8_t pub_bytes[33];
    pub_bytes[0] = pubkey.y.IsEven() ? 0x02 : 0x03;
    pubkey.x.Get32Bytes(pub_bytes + 1);

    uint8_t sha_out[32];
    sha256(pub_bytes, 33, sha_out);
    ripemd160(sha_out, 32, out_hash160);
}

static bool check_match(const uint8_t* hash160) {
    if (g_bloom_loaded) {
        int res = bloom_check(&g_bloom_filter, hash160, 20);
        if (res == 0) return false;
    }

    Hash20 target;
    memcpy(target.data, hash160, 20);
    bool found = std::binary_search(g_bin_targets.begin(), g_bin_targets.end(), target);
    if (g_bloom_loaded && !found) {
        g_false_positives++;
    }
    return found;
}

static void worker_thread(int thread_id, Config config, Int start_key, Int end_key, Int step, ThreadStats* stats, Secp256K1* secp) {
    Int current_key = start_key;
    Point current_pub = secp->ComputePublicKey(&current_key);
    Point g_point = secp->G;

    uint8_t hash160[20];
    char key_hex[65];
    char addr_hex[41];

    while (g_running) {
        compute_hash160(current_pub, hash160);
        stats->keys_evaluated++;

        if (check_match(hash160)) {
            g_keys_found++;
            char* kh = current_key.GetBase16();
            snprintf(key_hex, sizeof(key_hex), "%s", kh);
            free(kh);
            for (int i = 0; i < 20; ++i) {
                snprintf(addr_hex + i * 2, 3, "%02x", hash160[i]);
            }
            save_found_key(key_hex, addr_hex);
        }

        if (config.mode == MODE_SEQUENTIAL) {
            current_key.Add(&step);
            if (current_key.IsGreater(&end_key)) break;
            current_pub = secp->AddDirect(current_pub, g_point);
        } else if (config.mode == MODE_BIDIRECTIONAL) {
            if (step.IsNegative()) {
                current_key.Sub(&step);
                if (current_key.IsLower(&end_key)) break;
            } else {
                current_key.Add(&step);
                if (current_key.IsGreater(&end_key)) break;
            }
            current_pub = secp->ComputePublicKey(&current_key);
        } else if (config.mode == MODE_RANDOM) {
            current_key.Rand(config.range_bits);
            current_pub = secp->ComputePublicKey(&current_key);
        }
    }
}

int main(int argc, char** argv) {
    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);

    Config config;
    unsigned int hw_threads = std::thread::hardware_concurrency();
    config.num_threads = hw_threads > 0 ? hw_threads : 4;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-R") == 0 && i + 1 < argc) {
            config.range_bits = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-B") == 0) {
            config.mode = MODE_BIDIRECTIONAL;
        } else if (strcmp(argv[i], "-S") == 0) {
            config.mode = MODE_SEQUENTIAL;
        } else if (strcmp(argv[i], "-Random") == 0) {
            config.mode = MODE_RANDOM;
        }
    }

    printf("BTC Brute Force Engine\n");
    printf("Range: %d bits | Threads: %d | Mode: %s\n",
           config.range_bits, config.num_threads,
           config.mode == MODE_BIDIRECTIONAL ? "Bidirectional (-B)" :
           config.mode == MODE_SEQUENTIAL ? "Sequential (-S)" : "Random (-Random)");

    printf("\nRAM Loading Options:\n");
    printf("1. Load BTC.bin into RAM\n");
    printf("2. Load BTC.bin and BTC.xor into RAM\n");
    printf("3. Load BTC.bin, BTC.xor, and BTC.idx into RAM\n");
    printf("Enter choice (1, 2, or 3): ");

    if (scanf("%d", &config.ram_load_option) != 1) {
        config.ram_load_option = 2;
    }

    FILE* fbin = fopen("BTC.bin", "rb");
    if (fbin) {
        Hash20 h;
        while (fread(h.data, 1, 20, fbin) == 20) {
            g_bin_targets.push_back(h);
        }
        fclose(fbin);
        std::sort(g_bin_targets.begin(), g_bin_targets.end());
    }

    if (config.ram_load_option >= 2) {
        FILE* fxor = fopen("BTC.xor", "rb");
        if (fxor) {
            uint64_t entries = g_bin_targets.size() < 1000 ? 1000 : g_bin_targets.size();
            if (bloom_init2(&g_bloom_filter, entries, 0.001) == 0) {
                fread(g_bloom_filter.bf, 1, g_bloom_filter.bytes, fxor);
                g_bloom_loaded = true;
            }
            fclose(fxor);
        }
    }

    Secp256K1 secp;
    secp.Init();

    Int start_range, end_range;
    start_range.SetInt32(1);
    start_range.ShiftL(config.range_bits - 1);
    end_range.SetInt32(1);
    end_range.ShiftL(config.range_bits);
    end_range.SubOne();

    std::vector<ThreadStats> thread_stats(config.num_threads);
    std::vector<std::thread> threads;

    Int step;
    step.SetInt32(config.num_threads);

    for (int t = 0; t < config.num_threads; ++t) {
        Int t_start = start_range;
        Int t_offset;
        t_offset.SetInt32(t);
        t_start.Add(&t_offset);

        threads.emplace_back(worker_thread, t, config, t_start, end_range, step, &thread_stats[t], &secp);
    }

    auto start_time = std::chrono::steady_clock::now();
    uint64_t prev_total_keys = 0;

    while (g_running) {
        std::this_thread::sleep_for(std::chrono::seconds(25));

        uint64_t total_keys = 0;
        for (int t = 0; t < config.num_threads; ++t) {
            total_keys += thread_stats[t].keys_evaluated;
        }

        auto now = std::chrono::steady_clock::now();
        double elapsed = std::chrono::duration<double>(now - start_time).count();
        double mkeys_per_sec = (total_keys / (1024.0 * 1024.0)) / (elapsed > 0 ? elapsed : 1);

        printf("\n=== Execution Statistics (Updated every 25s) ===\n");
        printf("  Execution Mode    : %s\n",
               config.mode == MODE_BIDIRECTIONAL ? "Bidirectional (-B)" :
               config.mode == MODE_SEQUENTIAL ? "Sequential (-S)" : "Random (-Random)");
        printf("  Working Range     : Range 2^%d bits\n", config.range_bits);
        printf("  Active Threads    : %d threads\n", config.num_threads);
        for (int t = 0; t < config.num_threads; ++t) {
            printf("    Thread %d Keys  : %llu\n", t, (unsigned long long)thread_stats[t].keys_evaluated);
        }
        printf("  TOTAL Keys        : %llu (%.2f MKeys/s)\n", (unsigned long long)total_keys, mkeys_per_sec);
        printf("  False Positives   : %llu\n", (unsigned long long)g_false_positives.load());
        printf("  False Negatives   : %llu\n", (unsigned long long)g_false_negatives.load());
        printf("  Found Matches     : %llu\n", (unsigned long long)g_keys_found.load());
        printf("=================================================\n");
        fflush(stdout);
    }

    for (auto& th : threads) {
        if (th.joinable()) th.join();
    }

    if (g_bloom_loaded) {
        bloom_free(&g_bloom_filter);
    }

    printf("BTC Engine Exited cleanly.\n");
    return 0;
}
