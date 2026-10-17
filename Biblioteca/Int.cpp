#include "Int.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <random>

static inline bool gte_256_words(const uint64_t a[4], const uint64_t b[4]) {
    for (int i = 3; i >= 0; i--) {
        if (a[i] > b[i]) return true;
        if (a[i] < b[i]) return false;
    }
    return true;
}

static inline bool add_256_words(const uint64_t a[4], const uint64_t b[4], uint64_t res[4]) {
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

static inline bool sub_256_words(const uint64_t a[4], const uint64_t b[4], uint64_t res[4]) {
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

static inline void shiftl_256_words(uint64_t a[4], uint32_t n) {
    if (n == 0) return;
    if (n >= 256) {
        memset(a, 0, 32);
        return;
    }
    uint32_t w = n / 64;
    uint32_t b = n % 64;
    uint64_t res[4] = {0};
    for (int i = 3; i >= (int)w; i--) {
        if (b == 0) {
            res[i] = a[i - w];
        } else {
            res[i] = a[i - w] << b;
            if (i - (int)w - 1 >= 0) {
                res[i] |= a[i - w - 1] >> (64 - b);
            }
        }
    }
    memcpy(a, res, 32);
}

static inline void shiftr_256_words(uint64_t a[4], uint32_t n) {
    if (n == 0) return;
    if (n >= 256) {
        memset(a, 0, 32);
        return;
    }
    uint32_t w = n / 64;
    uint32_t b = n % 64;
    uint64_t res[4] = {0};
    for (int i = 0; i + (int)w < 4; i++) {
        if (b == 0) {
            res[i] = a[i + w];
        } else {
            res[i] = a[i + w] >> b;
            if (i + (int)w + 1 < 4) {
                res[i] |= a[i + w + 1] << (64 - b);
            }
        }
    }
    memcpy(a, res, 32);
}

static inline void div_256_words(const uint64_t num[4], const uint64_t den[4], uint64_t quo[4], uint64_t rem[4]) {
    uint64_t q[4] = {0};
    uint64_t r[4] = {0};
    if (den[0] == 0 && den[1] == 0 && den[2] == 0 && den[3] == 0) {
        if (quo) memset(quo, 0, 32);
        if (rem) memset(rem, 0, 32);
        return;
    }
    for (int i = 255; i >= 0; i--) {
        shiftl_256_words(r, 1);
        if ((num[i / 64] >> (i % 64)) & 1ULL) {
            r[0] |= 1ULL;
        }
        if (gte_256_words(r, den)) {
            sub_256_words(r, den, r);
            q[i / 64] |= (1ULL << (i % 64));
        }
    }
    if (quo) memcpy(quo, q, 32);
    if (rem) memcpy(rem, r, 32);
}

Int::Int() {
	memset(bits, 0, sizeof(bits));
}

Int::Int(const int32_t i32) {
	memset(bits, 0, sizeof(bits));
	bits[0] = (uint64_t)i32;
}

Int::Int(const uint32_t u32) {
	memset(bits, 0, sizeof(bits));
	bits[0] = (uint64_t)u32;
}

Int::Int(const Int *other) {
	if (other) memcpy(bits, other->bits, sizeof(bits));
	else memset(bits, 0, sizeof(bits));
}

Int::Int(const char *str) {
	memset(bits, 0, sizeof(bits));
	if (str) Set(str);
}

Int::Int(const uint64_t u64) {
	memset(bits, 0, sizeof(bits));
	bits[0] = u64;
}

Int::Int(const int64_t i64) {
	memset(bits, 0, sizeof(bits));
	bits[0] = (uint64_t)i64;
}

Int::Int(const Int &value) {
	memcpy(bits, value.bits, sizeof(bits));
}

Int::~Int() {
}

Int& Int::operator=(const Int& other) {
	if (this != &other) {
		memcpy(bits, other.bits, sizeof(bits));
	}
	return *this;
}

void Int::CLEAR() {
	memset(bits, 0, sizeof(bits));
}

void Int::SetInt64(const uint64_t value) {
	memset(bits, 0, sizeof(bits));
	bits[0] = value;
}

void Int::SetInt32(const uint32_t value) {
	memset(bits, 0, sizeof(bits));
	bits[0] = (uint64_t)value;
}

void Int::Set(const Int* other) {
	if (other) memcpy(bits, other->bits, sizeof(bits));
	else memset(bits, 0, sizeof(bits));
}

void Int::Set(const char *str) {
	if (!str || !*str) { CLEAR(); return; }
	if (str[0] == '0' && (str[1] == 'x' || str[1] == 'X')) {
		SetBase16(str + 2);
	} else {
		SetBase16(str);
	}
}

void Int::SetBase10(const char *str) {
	CLEAR();
	if (!str) return;
	while (*str && isspace((unsigned char)*str)) str++;
	for (int i = 0; str[i]; i++) {
		if (str[i] >= '0' && str[i] <= '9') {
			Mult((uint64_t)10);
			Add((uint32_t)(str[i] - '0'));
		}
	}
}

static uint8_t hex_val(char c) {
	if (c >= '0' && c <= '9') return (uint8_t)(c - '0');
	if (c >= 'a' && c <= 'f') return (uint8_t)(c - 'a' + 10);
	if (c >= 'A' && c <= 'F') return (uint8_t)(c - 'A' + 10);
	return 0;
}

void Int::SetBase16(const char *str) {
	CLEAR();
	if (!str) return;
	if (str[0] == '0' && (str[1] == 'x' || str[1] == 'X')) str += 2;
	int len = (int)strlen(str);
	if (len > 64) {
		return;
	}
	if (len == 0) return;
	unsigned char bytes[32] = {0};
	int byte_idx = 31;
	for (int i = len - 1; i >= 0 && byte_idx >= 0; i -= 2) {
		uint8_t lo = hex_val(str[i]);
		uint8_t hi = (i > 0) ? hex_val(str[i - 1]) : 0;
		bytes[byte_idx--] = (uint8_t)((hi << 4) | lo);
	}
	Set32Bytes(bytes);
}

void Int::Add(const uint64_t u64) {
	uint64_t b[4] = {u64, 0, 0, 0};
	add_256_words(bits, b, bits);
}

void Int::Add(const uint32_t u32) {
	uint64_t b[4] = {(uint64_t)u32, 0, 0, 0};
	add_256_words(bits, b, bits);
}

void Int::Add(const Int *a) {
	add_256_words(bits, a->bits, bits);
}

void Int::Add(const Int *a, const Int *b) {
	add_256_words(a->bits, b->bits, bits);
}

void Int::AddOne() {
	Add((uint32_t)1);
}

void Int::Sub(const uint32_t u32) {
	uint64_t b[4] = {(uint64_t)u32, 0, 0, 0};
	sub_256_words(bits, b, bits);
}

void Int::Sub(const uint64_t u64) {
	uint64_t b[4] = {u64, 0, 0, 0};
	sub_256_words(bits, b, bits);
}

void Int::Sub(Int *a) {
	sub_256_words(bits, a->bits, bits);
}

void Int::Sub(Int *a, Int *b) {
	sub_256_words(a->bits, b->bits, bits);
}

void Int::Mult(Int *a) {
	uint64_t res[8] = {0};
	for (int i = 0; i < 4; i++) {
		unsigned __int128 carry = 0;
		for (int j = 0; j < 4; j++) {
			unsigned __int128 prod = (unsigned __int128)bits[i] * a->bits[j] + res[i + j] + carry;
			res[i + j] = (uint64_t)prod;
			carry = prod >> 64;
		}
		if (i + 4 < 8) res[i + 4] = (uint64_t)carry;
	}
	memcpy(bits, res, 32);
}

void Int::Mult(uint64_t u64) {
	unsigned __int128 carry = 0;
	for (int i = 0; i < 4; i++) {
		unsigned __int128 prod = (unsigned __int128)bits[i] * u64 + carry;
		bits[i] = (uint64_t)prod;
		carry = prod >> 64;
	}
}

void Int::IMult(int64_t i64) {
	Mult((uint64_t)i64);
}

void Int::Neg() {
	uint64_t zero[4] = {0};
	sub_256_words(zero, bits, bits);
}

void Int::Abs() {
}

bool Int::IsGreater(Int *a) {
	for (int i = 3; i >= 0; i--) {
		if (bits[i] > a->bits[i]) return true;
		if (bits[i] < a->bits[i]) return false;
	}
	return false;
}

bool Int::IsGreaterOrEqual(Int *a) {
	for (int i = 3; i >= 0; i--) {
		if (bits[i] > a->bits[i]) return true;
		if (bits[i] < a->bits[i]) return false;
	}
	return true;
}

bool Int::IsLowerOrEqual(Int *a) {
	for (int i = 3; i >= 0; i--) {
		if (bits[i] < a->bits[i]) return true;
		if (bits[i] > a->bits[i]) return false;
	}
	return true;
}

bool Int::IsLower(Int *a) {
	for (int i = 3; i >= 0; i--) {
		if (bits[i] < a->bits[i]) return true;
		if (bits[i] > a->bits[i]) return false;
	}
	return false;
}

bool Int::IsEqual(Int *a) {
	return memcmp(bits, a->bits, 32) == 0;
}

bool Int::IsZero() {
	return bits[0] == 0 && bits[1] == 0 && bits[2] == 0 && bits[3] == 0;
}

bool Int::IsOne() {
	return bits[0] == 1 && bits[1] == 0 && bits[2] == 0 && bits[3] == 0;
}

bool Int::IsPositive() {
	return true;
}

bool Int::IsNegative() {
	return false;
}

bool Int::IsEven() {
	return (bits[0] & 1ULL) == 0;
}

bool Int::IsOdd() {
	return (bits[0] & 1ULL) == 1ULL;
}

int Int::GetSize() {
	int b = GetBitLength();
	return (b + 7) / 8;
}

int Int::GetBitLength() {
	for (int i = 3; i >= 0; i--) {
		if (bits[i] != 0) {
			for (int b = 63; b >= 0; b--) {
				if ((bits[i] >> b) & 1ULL) {
					return i * 64 + b + 1;
				}
			}
		}
	}
	return 0;
}

uint64_t Int::GetInt64() {
	return bits[0];
}

uint32_t Int::GetInt32() {
	return (uint32_t)bits[0];
}

int Int::GetBit(uint32_t n) {
	if (n >= 256) return 0;
	return (int)((bits[n / 64] >> (n % 64)) & 1ULL);
}

void Int::SetBit(uint32_t n) {
	if (n < 256) {
		bits[n / 64] |= (1ULL << (n % 64));
	}
}

void Int::ClearBit(uint32_t n) {
	if (n < 256) {
		bits[n / 64] &= ~(1ULL << (n % 64));
	}
}

void Int::Get32Bytes(unsigned char *buff) {
	if (!buff) return;
	for (int i = 0; i < 4; i++) {
		uint64_t w = bits[3 - i];
		buff[i * 8 + 0] = (unsigned char)(w >> 56);
		buff[i * 8 + 1] = (unsigned char)(w >> 48);
		buff[i * 8 + 2] = (unsigned char)(w >> 40);
		buff[i * 8 + 3] = (unsigned char)(w >> 32);
		buff[i * 8 + 4] = (unsigned char)(w >> 24);
		buff[i * 8 + 5] = (unsigned char)(w >> 16);
		buff[i * 8 + 6] = (unsigned char)(w >> 8);
		buff[i * 8 + 7] = (unsigned char)(w);
	}
}

void Int::Set32Bytes(unsigned char *buff) {
	if (!buff) return;
	for (int i = 0; i < 4; i++) {
		uint64_t w = 0;
		w |= ((uint64_t)buff[i * 8 + 0]) << 56;
		w |= ((uint64_t)buff[i * 8 + 1]) << 48;
		w |= ((uint64_t)buff[i * 8 + 2]) << 40;
		w |= ((uint64_t)buff[i * 8 + 3]) << 32;
		w |= ((uint64_t)buff[i * 8 + 4]) << 24;
		w |= ((uint64_t)buff[i * 8 + 5]) << 16;
		w |= ((uint64_t)buff[i * 8 + 6]) << 8;
		w |= ((uint64_t)buff[i * 8 + 7]);
		bits[3 - i] = w;
	}
}

unsigned char Int::GetByte(int n) {
	if (n < 0 || n >= 32) return 0;
	unsigned char buf[32];
	Get32Bytes(buf);
	return buf[n];
}

char* Int::GetBase2() {
	char *res = (char*)malloc(257);
	if (!res) return NULL;
	int len = GetBitLength();
	if (len == 0) {
		res[0] = '0';
		res[1] = 0;
		return res;
	}
	for (int i = 0; i < len; i++) {
		res[len - 1 - i] = GetBit(i) ? '1' : '0';
	}
	res[len] = 0;
	return res;
}

char* Int::GetBase10() {
	char *res = (char*)malloc(80);
	if (!res) return NULL;
	uint64_t tmp[4];
	memcpy(tmp, bits, 32);
	if (tmp[0] == 0 && tmp[1] == 0 && tmp[2] == 0 && tmp[3] == 0) {
		strcpy(res, "0");
		return res;
	}
	uint64_t chunks[5] = {0};
	int num_chunks = 0;
	const uint64_t E19 = 10000000000000000000ULL;
	while (tmp[0] != 0 || tmp[1] != 0 || tmp[2] != 0 || tmp[3] != 0) {
		unsigned __int128 rem = 0;
		for (int i = 3; i >= 0; i--) {
			rem = (rem << 64) | tmp[i];
			tmp[i] = (uint64_t)(rem / E19);
			rem %= E19;
		}
		chunks[num_chunks++] = (uint64_t)rem;
	}
	int pos = 0;
	pos += sprintf(res + pos, "%llu", (unsigned long long)chunks[num_chunks - 1]);
	for (int i = num_chunks - 2; i >= 0; i--) {
		pos += sprintf(res + pos, "%019llu", (unsigned long long)chunks[i]);
	}
	return res;
}

char* Int::GetBase16() {
	char *res = (char*)malloc(65);
	if (!res) return NULL;
	unsigned char buf[32];
	Get32Bytes(buf);
	static const char hex[] = "0123456789abcdef";
	int start = 0;
	while (start < 32 && buf[start] == 0) start++;
	if (start == 32) {
		strcpy(res, "0");
		return res;
	}
	int j = 0;
	for (int i = start; i < 32; i++) {
		res[j++] = hex[(buf[i] >> 4) & 0xF];
		res[j++] = hex[buf[i] & 0xF];
	}
	res[j] = 0;
	return res;
}

void Int::ShiftL(uint32_t n) {
	shiftl_256_words(bits, n);
}

void Int::ShiftR(uint32_t n) {
	shiftr_256_words(bits, n);
}

void Int::Div(Int *a, Int *mod) {
	if (!a || a->IsZero()) {
		CLEAR();
		if (mod) mod->CLEAR();
		return;
	}
	uint64_t q[4] = {0};
	uint64_t r[4] = {0};
	div_256_words(bits, a->bits, q, r);
	memcpy(bits, q, 32);
	if (mod) memcpy(mod->bits, r, 32);
}

void Int::Mod(Int *a) {
	if (!a || a->IsZero()) return;
	uint64_t r[4] = {0};
	div_256_words(bits, a->bits, NULL, r);
	memcpy(bits, r, 32);
}

void Int::Rand(int nbit) {
	CLEAR();
	if (nbit <= 0) return;
	if (nbit > 256) nbit = 256;
	static thread_local std::mt19937_64 rng(std::random_device{}());
	for (int i = 0; i < 4; i++) {
		if (nbit >= 64) {
			bits[i] = rng();
			nbit -= 64;
		} else if (nbit > 0) {
			uint64_t mask = (1ULL << nbit) - 1ULL;
			bits[i] = rng() & mask;
			nbit = 0;
		}
	}
}

void Int::Rand(Int *min, Int *max) {
	if (!min || !max) return;
	if (min->IsEqual(max)) {
		Set(min);
		return;
	}
	Int range(max);
	range.Sub(min);
	int range_bits = range.GetBitLength();
	Int r;
	do {
		r.Rand(range_bits);
	} while (r.IsGreater(&range));
	r.Add(min);
	Set(&r);
}
