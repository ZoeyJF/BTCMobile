#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <gmp.h>
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
struct ThreadRandState {
    gmp_randstate_t state;
    bool inited;
    ThreadRandState() : inited(false) {}
    ~ThreadRandState() {
        if (inited) {
            gmp_randclear(state);
            inited = false;
        }
    }
    gmp_randstate_t& get() {
        if (!inited) {
            gmp_randinit_mt(state);
            mpz_t mpz_seed;
            mpz_init(mpz_seed);
            unsigned char seed[64];
            int bytes_read = random_bytes(seed, 64);
            if (bytes_read != 64) {
                std::random_device rd;
                for (int i = 0; i < 64; i++) {
                    seed[i] = (unsigned char)(rd() & 0xFF);
                }
            }
            mpz_import(mpz_seed, 64, 1, sizeof(unsigned char), 0, 0, seed);
            gmp_randseed(state, mpz_seed);
            mpz_clear(mpz_seed);
            memset(seed, 0, sizeof(seed));
            inited = true;
        }
        return state;
    }
};
static thread_local ThreadRandState tl_rand;
void int_randominit() {
    tl_rand.get();
}
void Int::Rand(int nbit) {
    mpz_urandomb(num, tl_rand.get(), nbit);
    mpz_setbit(num, nbit - 1);
}
void Int::Rand(Int *min, Int *max) {
    Int diff(max);
    diff.Sub(min);
    this->Rand(256);
    this->Mod(&diff);
    this->Add(min);
}
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
