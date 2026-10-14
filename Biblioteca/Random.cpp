#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#if defined(_WIN32) || defined(_WIN64)
    #include <Windows.h>
    #include <bcrypt.h>
    #pragma comment(lib, "bcrypt.lib")
#elif __unix__ || __unix || __APPLE__ || __MACH__ || __CYGWIN__
    #include <unistd.h>
    #include <fcntl.h>
    #include <sys/syscall.h>
    #include <linux/random.h>
    #if defined(GRND_NONBLOCK)
        #define USE_GETRANDOM
    #endif
#endif
#include "Int.h"
#include "Random.h"
int random_bytes(unsigned char *buffer, int bytes) {
    #if defined(_WIN32) || defined(_WIN64)
        if (!BCryptGenRandom(NULL, buffer, bytes, BCRYPT_USE_SYSTEM_PREFERRED_RNG)) {
            return -1;
        }
        return bytes;
    #elif __unix__ || __unix || __APPLE__ || __MACH__ || __CYGWIN__
        #ifdef USE_GETRANDOM
            return (int)syscall(SYS_getrandom, buffer, bytes, GRND_NONBLOCK);
        #else
            int fd = open("/dev/urandom", O_RDONLY);
            if (fd == -1) return -1;
            ssize_t result = read(fd, buffer, bytes);
            close(fd);
            return (int)result;
        #endif
    #else
        #error "Unsupported platform"
    #endif
}
void int_randominit() {
}
void Int::Rand(int nbit) {
    CLEAR();
    if (nbit <= 0) return;
    if (nbit > 256) nbit = 256;
    int words = (nbit + 63) / 64;
    static thread_local std::mt19937_64 rng(std::random_device{}());
    for (int i = 0; i < words; i++) {
        bits[i] = rng();
    }
    uint32_t rem = nbit % 64;
    if (rem > 0 && words - 1 < 4) {
        bits[words - 1] &= (1ULL << rem) - 1ULL;
    }
    SetBit(nbit - 1);
}
void Int::Rand(Int *min, Int *max) {
    if (!min || !max || min->IsEqual(max) || min->IsGreater(max)) {
        if (min) Set(min);
        else CLEAR();
        return;
    }
    Int diff(max);
    diff.Sub(min);
    int bit_len = diff.GetBitLength();
    if (bit_len <= 0) bit_len = 1;
    Int candidate;
    static thread_local std::mt19937_64 rng(std::random_device{}());
    do {
        candidate.CLEAR();
        int words = (bit_len + 63) / 64;
        for (int i = 0; i < words; i++) {
            candidate.bits[i] = rng();
        }
        uint32_t rem = bit_len % 64;
        if (rem > 0 && words - 1 < 4) {
            candidate.bits[words - 1] &= (1ULL << rem) - 1ULL;
        }
    } while (candidate.IsGreater(&diff));
    Set(&candidate);
    Add(min);
}
