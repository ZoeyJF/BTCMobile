#ifndef ADDRESSHELPER_H
#define ADDRESSHELPER_H

#include <string>
#include <cstddef>
#include "Int.h"
#include "Point.h"

struct DerivedInfo {
    std::string privkey_hex;
    std::string pubkey_compressed_hex;
    std::string pubkey_uncompressed_hex;
    std::string hash160_hex;
    std::string btc_p2pkh_compressed;
    std::string btc_p2pkh_uncompressed;
    std::string btc_p2sh_p2wpkh;
    std::string btc_p2wpkh_bech32;
    std::string eth_address;
};

int derive_all_info(Int &priv, Point &pub, DerivedInfo &out);

void format_extended_entry(const DerivedInfo &info,
                           int range_bits,
                           int subrange_id,
                           int thread_id,
                           const char *timestamp_utc,
                           char *out,
                           size_t out_size);

#endif

