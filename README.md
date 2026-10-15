# BTC-MOBILE: High-Performance Standalone Private Key Engine

## Overview
BTC-MOBILE is a multi-threaded C++17 Bitcoin/Ethereum private key search engine designed for high performance on both Linux servers and Android/Termux mobile environments. It operates completely standalone with zero external dependencies (no GMP, no Bitcoin Core, no external libsecp256k1).

---

## Key Features & Architecture
- **Native 256-bit BigInt Engine**: Custom `uint64_t[4]` 256-bit integer class using GCC/Clang `unsigned __int128` primitives.
- **Fast Secp256k1 Field Reduction**: Fast reduction modulo $p = 2^{256} - 2^{32} - 977$ using the identity $2^{256} \equiv 2^{32} + 977 \pmod p$.
- **Binary Extended GCD ModInv**: Constant-structure binary extended Euclidean algorithm for 256-bit modular inverse.
- **GLV Endomorphism Acceleration**: Efficient $3\times$ point decomposition using secp256k1 curve endomorphisms ($\beta, \lambda$).
- **Multi-Threaded Architecture**: Lock-free thread-local Mersenne Twister PRNG (`std::mt19937_64`) per worker thread without mutex contention in hot search loops.
- **Mobile/Termux Compatible**: Detects aarch64 vs x86_64, uses `sysconf(_SC_NPROCESSORS_ONLN)` for core count without requiring `/sys/cpufreq` paths or root access.

---

## Database Files Explanation
BTC-MOBILE uses a precomputed 3-tier lookup system:

1. **`BTC.bin`**: Binary file containing 20-byte RIPEMD160 / Hash160 hashes sorted in ascending binary order.
2. **`BTC.idx`**: Primary index for binary search.
   - **Option A (Full Index)**: 28 bytes per entry (20-byte hash + 8-byte uint64 offset).
   - **Option B (Sparse Index)**: 24 bytes per entry (20-byte hash + 4-byte uint32 offset), storing 1 index record for every 64 hashes in `BTC.bin`.
3. **`BTC.xor`**: XOR Filter table providing ultra-fast $O(1)$ probabilistic membership queries to filter non-matching hashes before accessing disk/RAM.

---

## Commands & Search Modes

| Command Flag | Description | Requirements |
| :--- | :--- | :--- |
| `-R N` | Selects bit range between 1 and 256 bits | Required for `-S`, `-B`, `-Random` |
| `-S` | Sequential forward brute-force within specified range | Requires `-R N` |
| `-B` | Bidirectional search with threads distributed across search fronts | Requires `-R N` |
| `-Random` | Multi-threaded random jump search every 25 seconds within selected range | Requires `-R N` |
| `-Pure` | Global pure random search (random bit range in 128..256 bits, random subrange 0..99, 25s window) | Standalone (disallows `-R` and `-Subrango`) |
| `-Subrango` | Splits range into 100 subranges and searches randomly | Used with `-Random` |
| `-t N` | Sets thread count between 1 and 64 | Optional |

---

## Compilation & Usage

### 1. Build
```bash
make clean && make
```

### 2. Generate Binary Database Files
```bash
./CREATE-BIX Direcciones.hex
```

### 3. Run Key Engine
```bash
./BTC -R 66 -S -t 4
./BTC -R 66 -B -t 8
./BTC -Pure -t 4
```

---

## Performance & Results
- Matched keys are printed directly to the terminal UI and appended to `FOUND.txt`.
