#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>
#include <signal.h>
#include <unistd.h>
#include <time.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <malloc.h>

typedef struct {
    uint64_t v[4];
} u256;

typedef struct {
    u256 x;
    u256 y;
    u256 z;
} PointJacobian;

typedef struct {
    u256 x;
    u256 y;
} PointAffine;

static const u256 SECP256K1_P = {{
    0xFFFFFFFEFFFFFC2FULL, 0xFFFFFFFFFFFFFFFFULL,
    0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFFFFFFFFFFULL
}};

static const u256 SECP256K1_GX = {{
    0x59F2815B16F81798ULL, 0x029BFCDB2DCE28D9ULL,
    0x55A06295CE870B07ULL, 0x79BE667EF9DCBBACULL
}};

static const u256 SECP256K1_GY = {{
    0x9C47D08FFB10D4B8ULL, 0xFD17B448A6855419ULL,
    0x5DA4FBFC0E1108A8ULL, 0x483ADA7726A3C465ULL
}};

static volatile bool running = true;
static int g_range_bits = 64;
static char g_mode_str[32] = "Sequential";

static uint32_t db_option = 1;
static uint8_t *db_bin = NULL;
static size_t db_bin_count = 0;
static uint64_t *db_xor = NULL;
static size_t db_xor_count = 0;

static uint64_t *thread_keys_count = NULL;
static uint64_t *thread_fps = NULL;
static uint64_t *thread_fns = NULL;
static int g_num_threads = 1;

static inline int u256_is_zero(const u256 *a) {
    return (a->v[0] == 0 && a->v[1] == 0 && a->v[2] == 0 && a->v[3] == 0);
}

static inline int u256_cmp(const u256 *a, const u256 *b) {
    for (int i = 3; i >= 0; i--) {
        if (a->v[i] > b->v[i]) return 1;
        if (a->v[i] < b->v[i]) return -1;
    }
    return 0;
}

static inline void u256_add_p(u256 *r, const u256 *a, const u256 *b) {
    unsigned __int128 c = 0;
    for (int i = 0; i < 4; i++) {
        c += (unsigned __int128)a->v[i] + b->v[i];
        r->v[i] = (uint64_t)c;
        c >>= 64;
    }
    if (c || u256_cmp(r, &SECP256K1_P) >= 0) {
        c = 0;
        for (int i = 0; i < 4; i++) {
            unsigned __int128 sub = (unsigned __int128)r->v[i] - SECP256K1_P.v[i] - c;
            r->v[i] = (uint64_t)sub;
            c = (sub >> 127) & 1;
        }
    }
}

static inline void u256_sub_p(u256 *r, const u256 *a, const u256 *b) {
    unsigned __int128 c = 0;
    for (int i = 0; i < 4; i++) {
        unsigned __int128 sub = (unsigned __int128)a->v[i] - b->v[i] - c;
        r->v[i] = (uint64_t)sub;
        c = (sub >> 127) & 1;
    }
    if (c) {
        c = 0;
        for (int i = 0; i < 4; i++) {
            c += (unsigned __int128)r->v[i] + SECP256K1_P.v[i];
            r->v[i] = (uint64_t)c;
            c >>= 64;
        }
    }
}

static inline void u256_mul_p(u256 *r, const u256 *a, const u256 *b) {
    uint64_t t[8] = {0};
    for (int i = 0; i < 4; i++) {
        unsigned __int128 c = 0;
        for (int j = 0; j < 4; j++) {
            c += (unsigned __int128)a->v[i] * b->v[j] + t[i + j];
            t[i + j] = (uint64_t)c;
            c >>= 64;
        }
        t[i + 4] = (uint64_t)c;
    }

    u256 h, l;
    l.v[0] = t[0]; l.v[1] = t[1]; l.v[2] = t[2]; l.v[3] = t[3];
    h.v[0] = t[4]; h.v[1] = t[5]; h.v[2] = t[6]; h.v[3] = t[7];

    unsigned __int128 c = 0;
    c = (unsigned __int128)h.v[0] * 0x1000003D1ULL;
    unsigned __int128 c0 = (unsigned __int128)l.v[0] + (uint64_t)c;
    l.v[0] = (uint64_t)c0;
    c = (c >> 64) + (c0 >> 64);

    c += (unsigned __int128)h.v[1] * 0x1000003D1ULL + l.v[1];
    l.v[1] = (uint64_t)c;
    c >>= 64;

    c += (unsigned __int128)h.v[2] * 0x1000003D1ULL + l.v[2];
    l.v[2] = (uint64_t)c;
    c >>= 64;

    c += (unsigned __int128)h.v[3] * 0x1000003D1ULL + l.v[3];
    l.v[3] = (uint64_t)c;
    c >>= 64;

    if (c) {
        c *= 0x1000003D1ULL;
        for (int i = 0; i < 4; i++) {
            c += l.v[i];
            l.v[i] = (uint64_t)c;
            c >>= 64;
        }
    }

    while (u256_cmp(&l, &SECP256K1_P) >= 0) {
        u256_sub_p(&l, &l, &SECP256K1_P);
    }
    *r = l;
}

static inline void u256_sqr_p(u256 *r, const u256 *a) {
    u256_mul_p(r, a, a);
}

static inline void u256_inv_p(u256 *r, const u256 *a) {
    u256 p2;
    u256_sub_p(&p2, &SECP256K1_P, &(u256){{2, 0, 0, 0}});
    u256 res = {{1, 0, 0, 0}};
    u256 base = *a;
    for (int i = 0; i < 256; i++) {
        int bit = (p2.v[i / 64] >> (i % 64)) & 1;
        if (bit) {
            u256_mul_p(&res, &res, &base);
        }
        u256_sqr_p(&base, &base);
    }
    *r = res;
}

static void point_double(PointJacobian *r, const PointJacobian *p) {
    if (u256_is_zero(&p->z)) {
        *r = *p;
        return;
    }
    u256 a, b, c, d, e, f;
    u256_sqr_p(&a, &p->x);
    u256_sqr_p(&b, &p->y);
    u256_sqr_p(&c, &b);

    u256 t1;
    u256_add_p(&t1, &p->x, &b);
    u256_sqr_p(&t1, &t1);
    u256_sub_p(&t1, &t1, &a);
    u256_sub_p(&t1, &t1, &c);
    u256_add_p(&d, &t1, &t1);

    u256_add_p(&e, &a, &a);
    u256_add_p(&e, &e, &a);

    u256_sqr_p(&f, &e);

    u256_add_p(&t1, &d, &d);
    u256_sub_p(&r->x, &f, &t1);

    u256_sub_p(&t1, &d, &r->x);
    u256_mul_p(&t1, &e, &t1);
    u256_add_p(&c, &c, &c);
    u256_add_p(&c, &c, &c);
    u256_add_p(&c, &c, &c);
    u256_sub_p(&r->y, &t1, &c);

    u256_mul_p(&r->z, &p->y, &p->z);
    u256_add_p(&r->z, &r->z, &r->z);
}

static void point_add(PointJacobian *r, const PointJacobian *p, const PointJacobian *q) {
    if (u256_is_zero(&p->z)) { *r = *q; return; }
    if (u256_is_zero(&q->z)) { *r = *p; return; }

    u256 z12, z22, u1, u2, s1, s2, h, i, j, r_val, v;
    u256_sqr_p(&z12, &p->z);
    u256_sqr_p(&z22, &q->z);

    u256_mul_p(&u1, &p->x, &z22);
    u256_mul_p(&u2, &q->x, &z12);

    u256 t1;
    u256_mul_p(&t1, &q->z, &z22);
    u256_mul_p(&s1, &p->y, &t1);

    u256_mul_p(&t1, &p->z, &z12);
    u256_mul_p(&s2, &q->y, &t1);

    if (u256_cmp(&u1, &u2) == 0) {
        if (u256_cmp(&s1, &s2) == 0) {
            point_double(r, p);
        } else {
            r->x = (u256){{0}};
            r->y = (u256){{0}};
            r->z = (u256){{0}};
        }
        return;
    }

    u256_sub_p(&h, &u2, &u1);
    u256_add_p(&i, &h, &h);
    u256_sqr_p(&i, &i);

    u256_mul_p(&j, &h, &i);
    u256_sub_p(&r_val, &s2, &s1);
    u256_add_p(&r_val, &r_val, &r_val);

    u256_mul_p(&v, &u1, &i);

    u256_sqr_p(&r->x, &r_val);
    u256_sub_p(&r->x, &r->x, &j);
    u256_add_p(&t1, &v, &v);
    u256_sub_p(&r->x, &r->x, &t1);

    u256_sub_p(&t1, &v, &r->x);
    u256_mul_p(&t1, &r_val, &t1);
    u256_mul_p(&s1, &s1, &j);
    u256_add_p(&s1, &s1, &s1);
    u256_sub_p(&r->y, &t1, &s1);

    u256_add_p(&t1, &p->z, &q->z);
    u256_sqr_p(&t1, &t1);
    u256_sub_p(&t1, &t1, &z12);
    u256_sub_p(&t1, &t1, &z22);
    u256_mul_p(&r->z, &t1, &h);
}

static void point_to_affine(PointAffine *r, const PointJacobian *p) {
    if (u256_is_zero(&p->z)) {
        r->x = (u256){{0}};
        r->y = (u256){{0}};
        return;
    }
    u256 z_inv, z_inv2, z_inv3;
    u256_inv_p(&z_inv, &p->z);
    u256_sqr_p(&z_inv2, &z_inv);
    u256_mul_p(&z_inv3, &z_inv2, &z_inv);
    u256_mul_p(&r->x, &p->x, &z_inv2);
    u256_mul_p(&r->y, &p->y, &z_inv3);
}

static void point_mul(PointJacobian *r, const u256 *scalar, const PointJacobian *p) {
    PointJacobian res = {{{0}}, {{0}}, {{0}}};
    PointJacobian base = *p;
    for (int i = 0; i < 256; i++) {
        int bit = (scalar->v[i / 64] >> (i % 64)) & 1;
        if (bit) {
            point_add(&res, &res, &base);
        }
        point_double(&base, &base);
    }
    *r = res;
}

static void sha256_transform(uint32_t state[8], const uint8_t data[64]) {
    static const uint32_t K[64] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
        0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
        0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
        0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
        0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
    };

    uint32_t w[64], a, b, c, d, e, f, g, h, i, j, t1, t2;
    for (i = 0, j = 0; i < 16; ++i, j += 4)
        w[i] = (data[j] << 24) | (data[j + 1] << 16) | (data[j + 2] << 8) | (data[j + 3]);
    for (; i < 64; ++i) {
        uint32_t s0 = (w[i - 15] >> 7 | w[i - 15] << 25) ^ (w[i - 15] >> 18 | w[i - 15] << 14) ^ (w[i - 15] >> 3);
        uint32_t s1 = (w[i - 2] >> 17 | w[i - 2] << 15) ^ (w[i - 2] >> 19 | w[i - 2] << 13) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    a = state[0]; b = state[1]; c = state[2]; d = state[3];
    e = state[4]; f = state[5]; g = state[6]; h = state[7];

    for (i = 0; i < 64; ++i) {
        uint32_t S1 = (e >> 6 | e << 26) ^ (e >> 11 | e << 21) ^ (e >> 25 | e << 7);
        uint32_t ch = (e & f) ^ ((~e) & g);
        t1 = h + S1 + ch + K[i] + w[i];
        uint32_t S0 = (a >> 2 | a << 30) ^ (a >> 13 | a << 19) ^ (a >> 22 | a << 10);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        t2 = S0 + maj;

        h = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }

    state[0] += a; state[1] += b; state[2] += c; state[3] += d;
    state[4] += e; state[5] += f; state[6] += g; state[7] += h;
}

static void sha256(const uint8_t *data, size_t len, uint8_t hash[32]) {
    uint32_t state[8] = {
        0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
        0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
    };
    uint8_t buf[64];
    size_t i = 0;

    for (i = 0; i + 64 <= len; i += 64) {
        sha256_transform(state, data + i);
    }

    size_t rem = len - i;
    memcpy(buf, data + i, rem);
    buf[rem] = 0x80;
    rem++;

    if (rem > 56) {
        memset(buf + rem, 0, 64 - rem);
        sha256_transform(state, buf);
        memset(buf, 0, 56);
    } else {
        memset(buf + rem, 0, 56 - rem);
    }

    uint64_t bits = len * 8;
    for (int k = 0; k < 8; k++) {
        buf[63 - k] = (uint8_t)(bits >> (k * 8));
    }
    sha256_transform(state, buf);

    for (int k = 0; k < 8; k++) {
        hash[k * 4 + 0] = (state[k] >> 24) & 0xFF;
        hash[k * 4 + 1] = (state[k] >> 16) & 0xFF;
        hash[k * 4 + 2] = (state[k] >> 8) & 0xFF;
        hash[k * 4 + 3] = state[k] & 0xFF;
    }
}

static void ripemd160_transform(uint32_t state[5], const uint8_t block[64]) {
    uint32_t X[16];
    for (int i = 0; i < 16; i++) {
        X[i] = (uint32_t)block[i * 4] | ((uint32_t)block[i * 4 + 1] << 8) |
               ((uint32_t)block[i * 4 + 2] << 16) | ((uint32_t)block[i * 4 + 3] << 24);
    }

    uint32_t A = state[0], B = state[1], C = state[2], D = state[3], E = state[4];
    uint32_t Ap = state[0], Bp = state[1], Cp = state[2], Dp = state[3], Ep = state[4];

#define ROL(x, n) (((x) << (n)) | ((x) >> (32 - (n))))
#define F1(x, y, z) ((x) ^ (y) ^ (z))
#define F2(x, y, z) (((x) & (y)) | ((~x) & (z)))
#define F3(x, y, z) (((x) | (~y)) ^ (z))
#define F4(x, y, z) (((x) & (z)) | ((y) & (~z)))
#define F5(x, y, z) ((x) ^ ((y) | (~z)))

#define STEP_L(f, a, b, c, d, e, x, s, k) { \
    (a) += f((b), (c), (d)) + (x) + (k); \
    (a) = ROL((a), (s)) + (e); \
    (c) = ROL((c), 10); \
}

#define STEP_R(f, a, b, c, d, e, x, s, k) { \
    (a) += f((b), (c), (d)) + (x) + (k); \
    (a) = ROL((a), (s)) + (e); \
    (c) = ROL((c), 10); \
}

    STEP_L(F1, A, B, C, D, E, X[0], 11, 0x00000000);
    STEP_L(F1, E, A, B, C, D, X[1], 14, 0x00000000);
    STEP_L(F1, D, E, A, B, C, X[2], 15, 0x00000000);
    STEP_L(F1, C, D, E, A, B, X[3], 12, 0x00000000);
    STEP_L(F1, B, C, D, E, A, X[4], 5, 0x00000000);
    STEP_L(F1, A, B, C, D, E, X[5], 8, 0x00000000);
    STEP_L(F1, E, A, B, C, D, X[6], 7, 0x00000000);
    STEP_L(F1, D, E, A, B, C, X[7], 9, 0x00000000);
    STEP_L(F1, C, D, E, A, B, X[8], 11, 0x00000000);
    STEP_L(F1, B, C, D, E, A, X[9], 13, 0x00000000);
    STEP_L(F1, A, B, C, D, E, X[10], 14, 0x00000000);
    STEP_L(F1, E, A, B, C, D, X[11], 15, 0x00000000);
    STEP_L(F1, D, E, A, B, C, X[12], 6, 0x00000000);
    STEP_L(F1, C, D, E, A, B, X[13], 7, 0x00000000);
    STEP_L(F1, B, C, D, E, A, X[14], 9, 0x00000000);
    STEP_L(F1, A, B, C, D, E, X[15], 8, 0x00000000);

    STEP_L(F2, E, A, B, C, D, X[7], 7, 0x5A827999);
    STEP_L(F2, D, E, A, B, C, X[4], 6, 0x5A827999);
    STEP_L(F2, C, D, E, A, B, X[13], 8, 0x5A827999);
    STEP_L(F2, B, C, D, E, A, X[1], 13, 0x5A827999);
    STEP_L(F2, A, B, C, D, E, X[10], 11, 0x5A827999);
    STEP_L(F2, E, A, B, C, D, X[6], 9, 0x5A827999);
    STEP_L(F2, D, E, A, B, C, X[15], 12, 0x5A827999);
    STEP_L(F2, C, D, E, A, B, X[3], 15, 0x5A827999);
    STEP_L(F2, B, C, D, E, A, X[12], 7, 0x5A827999);
    STEP_L(F2, A, B, C, D, E, X[0], 12, 0x5A827999);
    STEP_L(F2, E, A, B, C, D, X[9], 11, 0x5A827999);
    STEP_L(F2, D, E, A, B, C, X[5], 13, 0x5A827999);
    STEP_L(F2, C, D, E, A, B, X[2], 14, 0x5A827999);
    STEP_L(F2, B, C, D, E, A, X[14], 9, 0x5A827999);
    STEP_L(F2, A, B, C, D, E, X[11], 13, 0x5A827999);
    STEP_L(F2, E, A, B, C, D, X[8], 15, 0x5A827999);

    STEP_L(F3, D, E, A, B, C, X[3], 11, 0x6ED9EBA1);
    STEP_L(F3, C, D, E, A, B, X[10], 13, 0x6ED9EBA1);
    STEP_L(F3, B, C, D, E, A, X[14], 6, 0x6ED9EBA1);
    STEP_L(F3, A, B, C, D, E, X[4], 7, 0x6ED9EBA1);
    STEP_L(F3, E, A, B, C, D, X[9], 14, 0x6ED9EBA1);
    STEP_L(F3, D, E, A, B, C, X[15], 9, 0x6ED9EBA1);
    STEP_L(F3, C, D, E, A, B, X[8], 13, 0x6ED9EBA1);
    STEP_L(F3, B, C, D, E, A, X[1], 15, 0x6ED9EBA1);
    STEP_L(F3, A, B, C, D, E, X[2], 14, 0x6ED9EBA1);
    STEP_L(F3, E, A, B, C, D, X[7], 8, 0x6ED9EBA1);
    STEP_L(F3, D, E, A, B, C, X[0], 13, 0x6ED9EBA1);
    STEP_L(F3, C, D, E, A, B, X[6], 6, 0x6ED9EBA1);
    STEP_L(F3, B, C, D, E, A, X[13], 5, 0x6ED9EBA1);
    STEP_L(F3, A, B, C, D, E, X[11], 12, 0x6ED9EBA1);
    STEP_L(F3, E, A, B, C, D, X[5], 7, 0x6ED9EBA1);
    STEP_L(F3, D, E, A, B, C, X[12], 5, 0x6ED9EBA1);

    STEP_L(F4, C, D, E, A, B, X[1], 11, 0x8F1BBCDC);
    STEP_L(F4, B, C, D, E, A, X[9], 12, 0x8F1BBCDC);
    STEP_L(F4, A, B, C, D, E, X[11], 14, 0x8F1BBCDC);
    STEP_L(F4, E, A, B, C, D, X[10], 15, 0x8F1BBCDC);
    STEP_L(F4, D, E, A, B, C, X[14], 14, 0x8F1BBCDC);
    STEP_L(F4, C, D, E, A, B, X[15], 15, 0x8F1BBCDC);
    STEP_L(F4, B, C, D, E, A, X[4], 9, 0x8F1BBCDC);
    STEP_L(F4, A, B, C, D, E, X[2], 8, 0x8F1BBCDC);
    STEP_L(F4, E, A, B, C, D, X[6], 9, 0x8F1BBCDC);
    STEP_L(F4, D, E, A, B, C, X[12], 14, 0x8F1BBCDC);
    STEP_L(F4, C, D, E, A, B, X[0], 14, 0x8F1BBCDC);
    STEP_L(F4, B, C, D, E, A, X[13], 5, 0x8F1BBCDC);
    STEP_L(F4, A, B, C, D, E, X[3], 12, 0x8F1BBCDC);
    STEP_L(F4, E, A, B, C, D, X[8], 13, 0x8F1BBCDC);
    STEP_L(F4, D, E, A, B, C, X[5], 6, 0x8F1BBCDC);
    STEP_L(F4, C, D, E, A, B, X[7], 5, 0x8F1BBCDC);

    STEP_L(F5, B, C, D, E, A, X[4], 9, 0xA953FD4E);
    STEP_L(F5, A, B, C, D, E, X[0], 15, 0xA953FD4E);
    STEP_L(F5, E, A, B, C, D, X[5], 5, 0xA953FD4E);
    STEP_L(F5, D, E, A, B, C, X[9], 11, 0xA953FD4E);
    STEP_L(F5, C, D, E, A, B, X[7], 6, 0xA953FD4E);
    STEP_L(F5, B, C, D, E, A, X[12], 8, 0xA953FD4E);
    STEP_L(F5, A, B, C, D, E, X[2], 13, 0xA953FD4E);
    STEP_L(F5, E, A, B, C, D, X[10], 12, 0xA953FD4E);
    STEP_L(F5, D, E, A, B, C, X[14], 5, 0xA953FD4E);
    STEP_L(F5, C, D, E, A, B, X[1], 12, 0xA953FD4E);
    STEP_L(F5, B, C, D, E, A, X[3], 13, 0xA953FD4E);
    STEP_L(F5, A, B, C, D, E, X[8], 14, 0xA953FD4E);
    STEP_L(F5, E, A, B, C, D, X[11], 11, 0xA953FD4E);
    STEP_L(F5, D, E, A, B, C, X[6], 8, 0xA953FD4E);
    STEP_L(F5, C, D, E, A, B, X[15], 5, 0xA953FD4E);
    STEP_L(F5, B, C, D, E, A, X[13], 6, 0xA953FD4E);

    STEP_R(F5, Ap, Bp, Cp, Dp, Ep, X[5], 8, 0x50A28BE6);
    STEP_R(F5, Ep, Ap, Bp, Cp, Dp, X[14], 9, 0x50A28BE6);
    STEP_R(F5, Dp, Ep, Ap, Bp, Cp, X[7], 9, 0x50A28BE6);
    STEP_R(F5, Cp, Dp, Ep, Ap, Bp, X[0], 11, 0x50A28BE6);
    STEP_R(F5, Bp, Cp, Dp, Ep, Ap, X[9], 13, 0x50A28BE6);
    STEP_R(F5, Ap, Bp, Cp, Dp, Ep, X[2], 15, 0x50A28BE6);
    STEP_R(F5, Ep, Ap, Bp, Cp, Dp, X[11], 15, 0x50A28BE6);
    STEP_R(F5, Dp, Ep, Ap, Bp, Cp, X[4], 5, 0x50A28BE6);
    STEP_R(F5, Cp, Dp, Ep, Ap, Bp, X[13], 7, 0x50A28BE6);
    STEP_R(F5, Bp, Cp, Dp, Ep, Ap, X[6], 7, 0x50A28BE6);
    STEP_R(F5, Ap, Bp, Cp, Dp, Ep, X[15], 8, 0x50A28BE6);
    STEP_R(F5, Ep, Ap, Bp, Cp, Dp, X[8], 11, 0x50A28BE6);
    STEP_R(F5, Dp, Ep, Ap, Bp, Cp, X[1], 14, 0x50A28BE6);
    STEP_R(F5, Cp, Dp, Ep, Ap, Bp, X[10], 14, 0x50A28BE6);
    STEP_R(F5, Bp, Cp, Dp, Ep, Ap, X[3], 12, 0x50A28BE6);
    STEP_R(F5, Ap, Bp, Cp, Dp, Ep, X[12], 6, 0x50A28BE6);

    STEP_R(F4, Ep, Ap, Bp, Cp, Dp, X[6], 9, 0x5C4DD124);
    STEP_R(F4, Dp, Ep, Ap, Bp, Cp, X[11], 13, 0x5C4DD124);
    STEP_R(F4, Cp, Dp, Ep, Ap, Bp, X[3], 15, 0x5C4DD124);
    STEP_R(F4, Bp, Cp, Dp, Ep, Ap, X[7], 7, 0x5C4DD124);
    STEP_R(F4, Ap, Bp, Cp, Dp, Ep, X[0], 12, 0x5C4DD124);
    STEP_R(F4, Ep, Ap, Bp, Cp, Dp, X[13], 8, 0x5C4DD124);
    STEP_R(F4, Dp, Ep, Ap, Bp, Cp, X[5], 9, 0x5C4DD124);
    STEP_R(F4, Cp, Dp, Ep, Ap, Bp, X[10], 11, 0x5C4DD124);
    STEP_R(F4, Bp, Cp, Dp, Ep, Ap, X[14], 7, 0x5C4DD124);
    STEP_R(F4, Ap, Bp, Cp, Dp, Ep, X[15], 7, 0x5C4DD124);
    STEP_R(F4, Ep, Ap, Bp, Cp, Dp, X[8], 12, 0x5C4DD124);
    STEP_R(F4, Dp, Ep, Ap, Bp, Cp, X[12], 7, 0x5C4DD124);
    STEP_R(F4, Cp, Dp, Ep, Ap, Bp, X[4], 6, 0x5C4DD124);
    STEP_R(F4, Bp, Cp, Dp, Ep, Ap, X[9], 15, 0x5C4DD124);
    STEP_R(F4, Ap, Bp, Cp, Dp, Ep, X[1], 13, 0x5C4DD124);
    STEP_R(F4, Ep, Ap, Bp, Cp, Dp, X[2], 11, 0x5C4DD124);

    STEP_R(F3, Dp, Ep, Ap, Bp, Cp, X[15], 9, 0x6D703EF3);
    STEP_R(F3, Cp, Dp, Ep, Ap, Bp, X[5], 7, 0x6D703EF3);
    STEP_R(F3, Bp, Cp, Dp, Ep, Ap, X[1], 15, 0x6D703EF3);
    STEP_R(F3, Ap, Bp, Cp, Dp, Ep, X[3], 11, 0x6D703EF3);
    STEP_R(F3, Ep, Ap, Bp, Cp, Dp, X[7], 8, 0x6D703EF3);
    STEP_R(F3, Dp, Ep, Ap, Bp, Cp, X[14], 6, 0x6D703EF3);
    STEP_R(F3, Cp, Dp, Ep, Ap, Bp, X[6], 6, 0x6D703EF3);
    STEP_R(F3, Bp, Cp, Dp, Ep, Ap, X[9], 14, 0x6D703EF3);
    STEP_R(F3, Ap, Bp, Cp, Dp, Ep, X[11], 12, 0x6D703EF3);
    STEP_R(F3, Ep, Ap, Bp, Cp, Dp, X[8], 13, 0x6D703EF3);
    STEP_R(F3, Dp, Ep, Ap, Bp, Cp, X[12], 5, 0x6D703EF3);
    STEP_R(F3, Cp, Dp, Ep, Ap, Bp, X[2], 14, 0x6D703EF3);
    STEP_R(F3, Bp, Cp, Dp, Ep, Ap, X[10], 13, 0x6D703EF3);
    STEP_R(F3, Ap, Bp, Cp, Dp, Ep, X[0], 13, 0x6D703EF3);
    STEP_R(F3, Ep, Ap, Bp, Cp, Dp, X[4], 7, 0x6D703EF3);
    STEP_R(F3, Dp, Ep, Ap, Bp, Cp, X[13], 5, 0x6D703EF3);

    STEP_R(F2, Cp, Dp, Ep, Ap, Bp, X[8], 15, 0x7A6D76E9);
    STEP_R(F2, Bp, Cp, Dp, Ep, Ap, X[6], 9, 0x7A6D76E9);
    STEP_R(F2, Ap, Bp, Cp, Dp, Ep, X[4], 8, 0x7A6D76E9);
    STEP_R(F2, Ep, Ap, Bp, Cp, Dp, X[1], 11, 0x7A6D76E9);
    STEP_R(F2, Dp, Ep, Ap, Bp, Cp, X[3], 14, 0x7A6D76E9);
    STEP_R(F2, Cp, Dp, Ep, Ap, Bp, X[11], 14, 0x7A6D76E9);
    STEP_R(F2, Bp, Cp, Dp, Ep, Ap, X[15], 12, 0x7A6D76E9);
    STEP_R(F2, Ap, Bp, Cp, Dp, Ep, X[0], 6, 0x7A6D76E9);
    STEP_R(F2, Ep, Ap, Bp, Cp, Dp, X[5], 14, 0x7A6D76E9);
    STEP_R(F2, Dp, Ep, Ap, Bp, Cp, X[12], 9, 0x7A6D76E9);
    STEP_R(F2, Cp, Dp, Ep, Ap, Bp, X[2], 12, 0x7A6D76E9);
    STEP_R(F2, Bp, Cp, Dp, Ep, Ap, X[13], 9, 0x7A6D76E9);
    STEP_R(F2, Ap, Bp, Cp, Dp, Ep, X[9], 12, 0x7A6D76E9);
    STEP_R(F2, Ep, Ap, Bp, Cp, Dp, X[7], 5, 0x7A6D76E9);
    STEP_R(F2, Dp, Ep, Ap, Bp, Cp, X[10], 15, 0x7A6D76E9);
    STEP_R(F2, Cp, Dp, Ep, Ap, Bp, X[14], 8, 0x7A6D76E9);

    STEP_R(F1, Bp, Cp, Dp, Ep, Ap, X[12], 8, 0x00000000);
    STEP_R(F1, Ap, Bp, Cp, Dp, Ep, X[15], 5, 0x00000000);
    STEP_R(F1, Ep, Ap, Bp, Cp, Dp, X[10], 12, 0x00000000);
    STEP_R(F1, Dp, Ep, Ap, Bp, Cp, X[4], 9, 0x00000000);
    STEP_R(F1, Cp, Dp, Ep, Ap, Bp, X[1], 12, 0x00000000);
    STEP_R(F1, Bp, Cp, Dp, Ep, Ap, X[5], 5, 0x00000000);
    STEP_R(F1, Ap, Bp, Cp, Dp, Ep, X[8], 14, 0x00000000);
    STEP_R(F1, Ep, Ap, Bp, Cp, Dp, X[7], 6, 0x00000000);
    STEP_R(F1, Dp, Ep, Ap, Bp, Cp, X[6], 8, 0x00000000);
    STEP_R(F1, Cp, Dp, Ep, Ap, Bp, X[2], 13, 0x00000000);
    STEP_R(F1, Bp, Cp, Dp, Ep, Ap, X[13], 5, 0x00000000);
    STEP_R(F1, Ap, Bp, Cp, Dp, Ep, X[14], 15, 0x00000000);
    STEP_R(F1, Ep, Ap, Bp, Cp, Dp, X[0], 13, 0x00000000);
    STEP_R(F1, Dp, Ep, Ap, Bp, Cp, X[3], 11, 0x00000000);
    STEP_R(F1, Cp, Dp, Ep, Ap, Bp, X[9], 11, 0x00000000);
    STEP_R(F1, Bp, Cp, Dp, Ep, Ap, X[11], 11, 0x00000000);

    uint32_t t = state[1] + C + Dp;
    state[1] = state[2] + D + Ep;
    state[2] = state[3] + E + Ap;
    state[3] = state[4] + A + Bp;
    state[4] = state[0] + B + Cp;
    state[0] = t;
}

static void ripemd160(const uint8_t *data, size_t len, uint8_t hash[20]) {
    uint32_t state[5] = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};
    uint8_t buf[64];
    size_t i = 0;

    for (i = 0; i + 64 <= len; i += 64) {
        ripemd160_transform(state, data + i);
    }

    size_t rem = len - i;
    memcpy(buf, data + i, rem);
    buf[rem] = 0x80;
    rem++;

    if (rem > 56) {
        memset(buf + rem, 0, 64 - rem);
        ripemd160_transform(state, buf);
        memset(buf, 0, 56);
    } else {
        memset(buf + rem, 0, 56 - rem);
    }

    uint64_t bits = len * 8;
    for (int k = 0; k < 8; k++) {
        buf[56 + k] = (uint8_t)(bits >> (k * 8));
    }
    ripemd160_transform(state, buf);

    for (int k = 0; k < 5; k++) {
        hash[k * 4 + 0] = state[k] & 0xFF;
        hash[k * 4 + 1] = (state[k] >> 8) & 0xFF;
        hash[k * 4 + 2] = (state[k] >> 16) & 0xFF;
        hash[k * 4 + 3] = (state[k] >> 24) & 0xFF;
    }
}

static void pubkey_to_hash160(const PointAffine *pub, int compressed, uint8_t out[20]) {
    uint8_t sha_out[32];
    if (compressed) {
        uint8_t cpub[33];
        cpub[0] = (pub->y.v[0] & 1) ? 0x03 : 0x02;
        for (int i = 0; i < 32; i++) {
            cpub[1 + i] = (uint8_t)(pub->x.v[3 - i / 8] >> (8 * (7 - (i % 8))));
        }
        sha256(cpub, 33, sha_out);
    } else {
        uint8_t upub[65];
        upub[0] = 0x04;
        for (int i = 0; i < 32; i++) {
            upub[1 + i] = (uint8_t)(pub->x.v[3 - i / 8] >> (8 * (7 - (i % 8))));
            upub[33 + i] = (uint8_t)(pub->y.v[3 - i / 8] >> (8 * (7 - (i % 8))));
        }
        sha256(upub, 65, sha_out);
    }
    ripemd160(sha_out, 32, out);
}

static uint64_t fnv1a_64(const uint8_t *data, size_t len) {
    uint64_t hash = 14695981039346656037ULL;
    for (size_t i = 0; i < len; i++) {
        hash ^= data[i];
        hash *= 1099511628211ULL;
    }
    return hash;
}

static pthread_mutex_t found_mutex = PTHREAD_MUTEX_INITIALIZER;

static bool binary_search_db(const uint8_t *target) {
    if (db_bin_count == 0) return false;

    size_t payload_len = (db_option == 1) ? 8 : 20;
    size_t low = 0;
    size_t high = db_bin_count - 1;

    while (low <= high) {
        size_t mid = low + (high - low) / 2;
        const uint8_t *mid_ptr = db_bin + mid * payload_len;
        int cmp = memcmp(mid_ptr, target, payload_len);
        if (cmp == 0) {
            return true;
        } else if (cmp < 0) {
            low = mid + 1;
        } else {
            if (mid == 0) break;
            high = mid - 1;
        }
    }
    return false;
}

static void check_match_and_save(const u256 *priv, const uint8_t h160[20], int tid) {
    uint64_t xor_val = fnv1a_64(h160, 20);

    bool xor_hit = false;
    for (size_t i = 0; i < db_xor_count; i++) {
        if (db_xor[i] == xor_val) {
            xor_hit = true;
            break;
        }
    }

    if (!xor_hit) {
        return;
    }

    if (binary_search_db(h160)) {
        pthread_mutex_lock(&found_mutex);
        FILE *ff = fopen("FOUND.txt", "a");
        if (ff) {
            fprintf(ff, "FOUND PRIVATE KEY: %016lx%016lx%016lx%016lx\n",
                    (unsigned long)priv->v[3], (unsigned long)priv->v[2],
                    (unsigned long)priv->v[1], (unsigned long)priv->v[0]);
            fprintf(ff, "HASH160: ");
            for (int k = 0; k < 20; k++) fprintf(ff, "%02x", h160[k]);
            fprintf(ff, "\n\n");
            fflush(ff);
            fclose(ff);
        }
        pthread_mutex_unlock(&found_mutex);
    } else {
        thread_fps[tid]++;
    }
}

static bool load_databases() {
    FILE *f_bin = fopen("BTC.bin", "rb");
    FILE *f_xor = fopen("BTC.xor", "rb");

    if (!f_bin || !f_xor) {
        if (f_bin) fclose(f_bin);
        if (f_xor) fclose(f_xor);
        return false;
    }

    uint32_t header_bin = 0, header_xor = 0;
    if (fread(&header_bin, sizeof(uint32_t), 1, f_bin) != 1 ||
        fread(&header_xor, sizeof(uint32_t), 1, f_xor) != 1) {
        fclose(f_bin);
        fclose(f_xor);
        return false;
    }

    db_option = header_bin;

    fseek(f_bin, 0, SEEK_END);
    long sz_bin = ftell(f_bin) - 4;
    fseek(f_bin, 4, SEEK_SET);

    size_t payload_len = (db_option == 1) ? 8 : 20;
    db_bin_count = sz_bin / payload_len;
    db_bin = (uint8_t *)malloc(sz_bin);
    if (fread(db_bin, 1, sz_bin, f_bin) != (size_t)sz_bin) {
        free(db_bin);
        fclose(f_bin);
        fclose(f_xor);
        return false;
    }
    fclose(f_bin);

    fseek(f_xor, 0, SEEK_END);
    long sz_xor = ftell(f_xor) - 4;
    fseek(f_xor, 4, SEEK_SET);

    db_xor_count = sz_xor / sizeof(uint64_t);
    db_xor = (uint64_t *)malloc(sz_xor);
    if (fread(db_xor, sizeof(uint64_t), db_xor_count, f_xor) != db_xor_count) {
        free(db_bin);
        free(db_xor);
        fclose(f_xor);
        return false;
    }
    fclose(f_xor);

    return true;
}

static void sig_handler(int signo) {
    if (signo == SIGINT) {
        running = false;
    }
}

typedef struct {
    int thread_id;
    int mode_type;
    u256 start_key;
    u256 end_key;
    u256 step_key;
    bool is_backward;
} ThreadArg;

static void *worker_thread(void *arg) {
    ThreadArg *targ = (ThreadArg *)arg;
    int tid = targ->thread_id;
    int mode = targ->mode_type;

    PointJacobian G_jac = {SECP256K1_GX, SECP256K1_GY, {{1, 0, 0, 0}}};

    if (mode == 0) {
        u256 curr = targ->start_key;
        while (running) {
            PointJacobian P_jac;
            point_mul(&P_jac, &curr, &G_jac);
            PointAffine P_aff;
            point_to_affine(&P_aff, &P_jac);

            uint8_t h160_c[20];
            uint8_t h160_u[20];
            pubkey_to_hash160(&P_aff, 1, h160_c);
            pubkey_to_hash160(&P_aff, 0, h160_u);

            check_match_and_save(&curr, h160_c, tid);
            check_match_and_save(&curr, h160_u, tid);

            thread_keys_count[tid] += 2;

            if (targ->is_backward) {
                u256_sub_p(&curr, &curr, &targ->step_key);
            } else {
                u256_add_p(&curr, &curr, &targ->step_key);
            }
        }
    } else if (mode == 1) {
        u256 curr = targ->start_key;
        while (running) {
            PointJacobian P_jac;
            point_mul(&P_jac, &curr, &G_jac);
            PointAffine P_aff;
            point_to_affine(&P_aff, &P_jac);

            uint8_t h160_c[20];
            uint8_t h160_u[20];
            pubkey_to_hash160(&P_aff, 1, h160_c);
            pubkey_to_hash160(&P_aff, 0, h160_u);

            check_match_and_save(&curr, h160_c, tid);
            check_match_and_save(&curr, h160_u, tid);

            thread_keys_count[tid] += 2;

            u256_add_p(&curr, &curr, &targ->step_key);
        }
    } else if (mode == 2) {
        unsigned int seed = (unsigned int)(time(NULL) ^ tid);
        time_t last_jump = time(NULL);

        u256 curr = {{
            ((uint64_t)rand_r(&seed) << 32) | rand_r(&seed),
            ((uint64_t)rand_r(&seed) << 32) | rand_r(&seed),
            ((uint64_t)rand_r(&seed) << 32) | rand_r(&seed),
            ((uint64_t)rand_r(&seed) << 32) | rand_r(&seed)
        }};

        while (running) {
            if (time(NULL) - last_jump >= 5) {
                curr.v[0] = ((uint64_t)rand_r(&seed) << 32) | rand_r(&seed);
                curr.v[1] = ((uint64_t)rand_r(&seed) << 32) | rand_r(&seed);
                curr.v[2] = ((uint64_t)rand_r(&seed) << 32) | rand_r(&seed);
                curr.v[3] = ((uint64_t)rand_r(&seed) << 32) | rand_r(&seed);
                last_jump = time(NULL);
            }

            PointJacobian P_jac;
            point_mul(&P_jac, &curr, &G_jac);
            PointAffine P_aff;
            point_to_affine(&P_aff, &P_jac);

            uint8_t h160_c[20];
            uint8_t h160_u[20];
            pubkey_to_hash160(&P_aff, 1, h160_c);
            pubkey_to_hash160(&P_aff, 0, h160_u);

            check_match_and_save(&curr, h160_c, tid);
            check_match_and_save(&curr, h160_u, tid);

            thread_keys_count[tid] += 2;

            u256_add_p(&curr, &curr, &(u256){{1, 0, 0, 0}});
        }
    }

    return NULL;
}

int main(int argc, char **argv) {
    int search_mode = 1;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-R") == 0 && i + 1 < argc) {
            g_range_bits = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-B") == 0) {
            search_mode = 0;
            strncpy(g_mode_str, "Bidirectional (-B)", sizeof(g_mode_str) - 1);
        } else if (strcmp(argv[i], "-S") == 0) {
            search_mode = 1;
            strncpy(g_mode_str, "Sequential (-S)", sizeof(g_mode_str) - 1);
        } else if (strcmp(argv[i], "-Random") == 0) {
            search_mode = 2;
            strncpy(g_mode_str, "Random (-Random)", sizeof(g_mode_str) - 1);
        }
    }

    signal(SIGINT, sig_handler);
    signal(SIGHUP, SIG_IGN);

    if (!load_databases()) {
        printf("Error: Could not load BTC.bin / BTC.xor databases.\n");
        printf("Please run CREATE-BiX first to generate database files.\n");
        return 1;
    }

    long ncores = sysconf(_SC_NPROCESSORS_ONLN);
    if (ncores < 1) ncores = 1;
    g_num_threads = (int)ncores;

    thread_keys_count = (uint64_t *)calloc(g_num_threads, sizeof(uint64_t));
    thread_fps = (uint64_t *)calloc(g_num_threads, sizeof(uint64_t));
    thread_fns = (uint64_t *)calloc(g_num_threads, sizeof(uint64_t));

    pthread_t *threads = (pthread_t *)malloc(g_num_threads * sizeof(pthread_t));
    ThreadArg *targs = (ThreadArg *)malloc(g_num_threads * sizeof(ThreadArg));

    u256 range_max = {{0}};
    if (g_range_bits >= 256) {
        range_max = (u256){{0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFFFFFFFFFFULL}};
    } else {
        int idx = g_range_bits / 64;
        int shift = g_range_bits % 64;
        if (shift == 0 && idx > 0) {
            idx--;
            shift = 64;
        }
        for (int k = 0; k < idx; k++) range_max.v[k] = 0xFFFFFFFFFFFFFFFFULL;
        if (shift > 0 && idx < 4) {
            range_max.v[idx] = (shift == 64) ? 0xFFFFFFFFFFFFFFFFULL : ((1ULL << shift) - 1);
        }
    }

    u256 range_start = {{1, 0, 0, 0}};

    printf("====================================================\n");
    printf(" BTC Brute-Force Engine Active\n");
    printf(" CPU Cores / Threads: %d\n", g_num_threads);
    printf(" Range Size: 2^%d bits\n", g_range_bits);
    printf(" Execution Mode: %s\n", g_mode_str);
    printf("====================================================\n\n");

    for (int i = 0; i < g_num_threads; i++) {
        targs[i].thread_id = i;
        targs[i].mode_type = search_mode;
        targs[i].start_key = range_start;
        targs[i].end_key = range_max;
        targs[i].step_key = (u256){{(uint64_t)g_num_threads, 0, 0, 0}};
        targs[i].is_backward = false;

        if (search_mode == 0) {
            int factor = i % 4;
            if (factor == 0) {
                targs[i].start_key = (u256){{1 + i, 0, 0, 0}};
                targs[i].is_backward = false;
            } else if (factor == 1) {
                targs[i].start_key = range_max;
                targs[i].is_backward = true;
            } else if (factor == 2) {
                targs[i].start_key.v[0] = range_max.v[0] / 2;
                targs[i].start_key.v[1] = range_max.v[1] / 2;
                targs[i].is_backward = false;
            } else {
                targs[i].start_key.v[0] = range_max.v[0] / 2;
                targs[i].start_key.v[1] = range_max.v[1] / 2;
                targs[i].is_backward = true;
            }
        } else if (search_mode == 1) {
            targs[i].start_key = (u256){{1 + i, 0, 0, 0}};
        }

        pthread_create(&threads[i], NULL, worker_thread, &targs[i]);
    }

    time_t start_time = time(NULL);
    time_t last_report = start_time;

    while (running) {
        sleep(1);
        time_t now = time(NULL);
        if (now - last_report >= 25) {
            last_report = now;
            uint64_t total_keys = 0;
            uint64_t total_fps = 0;
            uint64_t total_fns = 0;

            printf("\n--- Performance Statistics Update [%lds elapsed] ---\n", (long)(now - start_time));
            printf("Active Mode: %s | Range: 2^%d bits\n", g_mode_str, g_range_bits);
            for (int i = 0; i < g_num_threads; i++) {
                uint64_t tk = thread_keys_count[i];
                uint64_t tfp = thread_fps[i];
                uint64_t tfn = thread_fns[i];
                total_keys += tk;
                total_fps += tfp;
                total_fns += tfn;
                printf("  Thread #%d: Keys=%lu | FalsePos=%lu | FalseNeg=%lu\n",
                       i, (unsigned long)tk, (unsigned long)tfp, (unsigned long)tfn);
            }
            printf("----------------------------------------------------\n");
            printf(" TOTAL KEYS SCANNED: %lu\n", (unsigned long)total_keys);
            printf(" TOTAL FALSE POSITIVES: %lu | TOTAL FALSE NEGATIVES: %lu\n",
                   (unsigned long)total_fps, (unsigned long)total_fns);
            printf("----------------------------------------------------\n\n");
            fflush(stdout);
        }
    }

    printf("\nShutdown signal received. Waiting for threads to exit...\n");
    for (int i = 0; i < g_num_threads; i++) {
        pthread_join(threads[i], NULL);
    }

    free(thread_keys_count);
    free(thread_fps);
    free(thread_fns);
    free(threads);
    free(targs);
    free(db_bin);
    free(db_xor);

#if defined(__GLIBC__)
    malloc_trim(0);
#endif

    printf("Engine shut down cleanly.\n");
    return 0;
}
