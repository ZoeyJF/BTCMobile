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
