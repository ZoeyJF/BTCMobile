#include "Int.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

static Int _P("FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFEFFFFFC2F");
static Int *_O = NULL;

static inline bool gte_256(const uint64_t a[4], const uint64_t b[4]) {
    for (int i = 3; i >= 0; i--) {
        if (a[i] > b[i]) return true;
        if (a[i] < b[i]) return false;
    }
    return true;
}

static inline bool add_256(const uint64_t a[4], const uint64_t b[4], uint64_t res[4]) {
    unsigned __int128 sum = (unsigned __int128)a[0] + b[0];
    res[0] = (uint64_t)sum;
    sum = (unsigned __int128)a[1] + b[1] + (sum >> 64);
    res[1] = (uint64_t)sum;
    sum = (unsigned __int128)a[2] + b[2] + (sum >> 64);
    res[2] = (uint64_t)sum;
    sum = (unsigned __int128)a[3] + b[3] + (sum >> 64);
    res[3] = (uint64_t)sum;
    return (bool)(sum >> 64);
}

static inline bool sub_256(const uint64_t a[4], const uint64_t b[4], uint64_t res[4]) {
    unsigned __int128 diff = (unsigned __int128)a[0] - b[0];
    res[0] = (uint64_t)diff;
    diff = (unsigned __int128)a[1] - b[1] - (diff >> 127 ? 1 : 0);
    res[1] = (uint64_t)diff;
    diff = (unsigned __int128)a[2] - b[2] - (diff >> 127 ? 1 : 0);
    res[2] = (uint64_t)diff;
    diff = (unsigned __int128)a[3] - b[3] - (diff >> 127 ? 1 : 0);
    res[3] = (uint64_t)diff;
    return (bool)(diff >> 127);
}

static inline void mul_256_512(const uint64_t a[4], const uint64_t b[4], uint64_t res[8]) {
    memset(res, 0, 8 * sizeof(uint64_t));
    for (int i = 0; i < 4; i++) {
        unsigned __int128 carry = 0;
        for (int j = 0; j < 4; j++) {
            unsigned __int128 prod = (unsigned __int128)a[i] * b[j] + res[i + j] + carry;
            res[i + j] = (uint64_t)prod;
            carry = prod >> 64;
        }
        res[i + 4] += (uint64_t)carry;
    }
}

static inline void reduce_p(uint64_t res[4], const uint64_t r[8]) {
    const uint64_t K = 0x1000003D1ULL;
    uint64_t L[4] = {r[0], r[1], r[2], r[3]};
    uint64_t H[4] = {r[4], r[5], r[6], r[7]};

    unsigned __int128 carry = 0;
    uint64_t HK[5] = {0};
    for (int i = 0; i < 4; i++) {
        unsigned __int128 prod = (unsigned __int128)H[i] * K + carry;
        HK[i] = (uint64_t)prod;
        carry = prod >> 64;
    }
    HK[4] = (uint64_t)carry;

    unsigned __int128 sum = (unsigned __int128)L[0] + HK[0];
    res[0] = (uint64_t)sum;
    sum = (unsigned __int128)L[1] + HK[1] + (sum >> 64);
    res[1] = (uint64_t)sum;
    sum = (unsigned __int128)L[2] + HK[2] + (sum >> 64);
    res[2] = (uint64_t)sum;
    sum = (unsigned __int128)L[3] + HK[3] + (sum >> 64);
    res[3] = (uint64_t)sum;
    uint64_t ovf = HK[4] + (uint64_t)(sum >> 64);

    while (ovf > 0) {
        unsigned __int128 prod2 = (unsigned __int128)ovf * K;
        sum = (unsigned __int128)res[0] + (uint64_t)prod2;
        res[0] = (uint64_t)sum;
        sum = (unsigned __int128)res[1] + (uint64_t)(prod2 >> 64) + (sum >> 64);
        res[1] = (uint64_t)sum;
        sum = (unsigned __int128)res[2] + (sum >> 64);
        res[2] = (uint64_t)sum;
        sum = (unsigned __int128)res[3] + (sum >> 64);
        res[3] = (uint64_t)sum;
        ovf = (uint64_t)(sum >> 64);
    }

    while (gte_256(res, _P.bits)) {
        sub_256(res, _P.bits, res);
    }
}

static inline void reduce_n(uint64_t res[4], const uint64_t r[8], const uint64_t n_words[4]) {
    uint64_t rem[5] = {0};
    for (int i = 511; i >= 0; i--) {
        uint64_t c0 = rem[0] >> 63;
        uint64_t c1 = rem[1] >> 63;
        uint64_t c2 = rem[2] >> 63;
        uint64_t c3 = rem[3] >> 63;
        rem[0] <<= 1;
        rem[1] = (rem[1] << 1) | c0;
        rem[2] = (rem[2] << 1) | c1;
        rem[3] = (rem[3] << 1) | c2;
        rem[4] = (rem[4] << 1) | c3;

        if ((r[i / 64] >> (i % 64)) & 1ULL) {
            rem[0] |= 1ULL;
        }

        if (rem[4] > 0 || gte_256(rem, n_words)) {
            bool borrow = sub_256(rem, n_words, rem);
            if (borrow) rem[4]--;
        }
    }
    memcpy(res, rem, 32);
}

void Int::ModAdd(Int *a) {
    bool carry = add_256(bits, a->bits, bits);
    if (carry || gte_256(bits, _P.bits)) {
        sub_256(bits, _P.bits, bits);
    }
}

void Int::ModAdd(uint32_t a) {
    Int A(a);
    ModAdd(&A);
}

void Int::ModAdd(Int *a, Int *b) {
    bool carry = add_256(a->bits, b->bits, bits);
    if (carry || gte_256(bits, _P.bits)) {
        sub_256(bits, _P.bits, bits);
    }
}

void Int::ModSub(Int *a) {
    if (sub_256(bits, a->bits, bits)) {
        add_256(bits, _P.bits, bits);
    }
}

void Int::ModSub(Int *a, Int *b) {
    if (sub_256(a->bits, b->bits, bits)) {
        add_256(bits, _P.bits, bits);
    }
}

void Int::ModSub(uint64_t a) {
    Int A(a);
    if (sub_256(bits, A.bits, bits)) {
        add_256(bits, _P.bits, bits);
    }
}

void Int::ModMul(Int *a) {
    uint64_t r[8];
    mul_256_512(bits, a->bits, r);
    reduce_p(bits, r);
}

void Int::ModMul(Int *a, Int *b) {
    uint64_t r[8];
    mul_256_512(a->bits, b->bits, r);
    reduce_p(bits, r);
}

void Int::ModMulK1(Int *a) {
    uint64_t r[8];
    mul_256_512(bits, a->bits, r);
    reduce_p(bits, r);
}

void Int::ModMulK1(Int *a, Int *b) {
    uint64_t r[8];
    mul_256_512(a->bits, b->bits, r);
    reduce_p(bits, r);
}

void Int::ModSquareK1(Int *a) {
    uint64_t r[8];
    mul_256_512(a->bits, a->bits, r);
    reduce_p(bits, r);
}

void Int::ModDouble() {
    add_256(bits, bits, bits);
    if (gte_256(bits, _P.bits)) {
        sub_256(bits, _P.bits, bits);
    }
}

void Int::ModNeg() {
    if (IsZero()) return;
    sub_256(_P.bits, bits, bits);
}

void Int::ModInv() {
    if (IsZero()) return;
    Int base(this);
    Int res((uint32_t)1);
    Int p_minus_2(&_P);
    p_minus_2.Sub((uint32_t)2);

    for (int i = 255; i >= 0; i--) {
        res.ModSquareK1(&res);
        if (p_minus_2.GetBit(i)) {
            res.ModMulK1(&base);
        }
    }
    Set(&res);
}

void Int::ModSqrt() {
    Int base(this);
    Int res((uint32_t)1);
    Int p_plus_1(&_P);
    p_plus_1.Add((uint32_t)1);
    p_plus_1.ShiftR(2);

    for (int i = 255; i >= 0; i--) {
        res.ModSquareK1(&res);
        if (p_plus_1.GetBit(i)) {
            res.ModMulK1(&base);
        }
    }
    Set(&res);
}

bool Int::HasSqrt() {
    Int s(this);
    s.ModSqrt();
    s.ModSquareK1(&s);
    return s.IsEqual(this);
}

void Int::SetupField(Int *n) {
    if (n) _P.Set(n);
}

void Int::InitK1(Int *order) {
    if (order) _O = order;
}

void Int::ModMulK1order(Int *a) {
    if (!_O) return;
    uint64_t r[8];
    mul_256_512(bits, a->bits, r);
    reduce_n(bits, r, _O->bits);
}

void Int::ModAddK1order(Int *a, Int *b) {
    if (!_O) return;
    add_256(a->bits, b->bits, bits);
    if (gte_256(bits, _O->bits)) {
        sub_256(bits, _O->bits, bits);
    }
}

void Int::ModInvorder() {
    if (!_O) return;
    Int base(this);
    Int res((uint32_t)1);
    Int order_minus_2(_O);
    order_minus_2.Sub((uint32_t)2);

    for (int i = 255; i >= 0; i--) {
        uint64_t r[8];
        mul_256_512(res.bits, res.bits, r);
        reduce_n(res.bits, r, _O->bits);
        if (order_minus_2.GetBit(i)) {
            mul_256_512(res.bits, base.bits, r);
            reduce_n(res.bits, r, _O->bits);
        }
    }
    Set(&res);
}
