#ifndef CRYPTO_CONSTS_H
#define CRYPTO_CONSTS_H
#include "Int.h"
#include <cstdint>
#define SECP256K1_ORDER_HEX "FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFEBAAEDCE6AF48A03BBFD25E8CD0364141"
bool compute_range_bounds(int bits, Int *out_start, Int *out_end);
bool is_valid_private(const Int &v);
int safe_rand_in_range(Int *out, const Int &start, const Int &end);
#endif
