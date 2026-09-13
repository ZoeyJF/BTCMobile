#include <iostream>
#include <fstream>
#include <string>
#include <cstring>
#include <chrono>
#include <thread>
#include <atomic>
#include <mutex>
#include <random>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <iomanip>
#include <vector>
#include <cstdlib>
#include <sstream>
#include <ctime>

#include "Biblioteca/GMP256K1.h"
#include "Biblioteca/Int.h"
#include "Biblioteca/Point.h"
#include "Biblioteca/Random.h"
#include "Biblioteca/bloom.h"
#include "Biblioteca/sha256.h"
#include "Biblioteca/ripemd160.h"
#include "Biblioteca/sha3.h"

#define MAX_THREADS 32

unsigned char* binMap = nullptr;
size_t binSize = 0;

#pragma pack(push, 1)
struct IdxEntry {
    uint64_t prefix;
    long long offset;
    uint64_t count;
    uint32_t reserved;
};
#pragma pack(pop)

IdxEntry* idxMap = nullptr;
size_t idxCount = 0;

unsigned char* xorMap = nullptr;
size_t xorSize = 0;

struct bloom* bloomFilter = nullptr;

std::atomic<uint64_t> totalKeysScanned(0);
std::atomic<uint64_t> currentSubRange(0);
std::atomic<uint64_t> currentRangeId(0);
std::atomic<bool> foundFlag(false);
std::atomic<uint64_t> bloomFalsePositives(0);
std::atomic<uint64_t> bloomTruePositives(0);
std::atomic<uint64_t> walletsFoundCount(0);
std::atomic<uint64_t> threadSubRanges[MAX_THREADS];
std::atomic<unsigned int> activeThreadCount(0);
std::mutex consoleMutex;
std::mutex fileMutex;
int gReportLines = 0;

std::string formatWithCommas(uint64_t value) {
    std::string result;
    int count = 0;
    while(value > 0) {
        if(count > 0 && count % 3 == 0) result = "," + result;
        result = char('0' + (value % 10)) + result;
        value /= 10;
        count++;
    }
    return result.empty() ? "0" : result;
}

void cleanupResources() {
    if(binMap && binMap != MAP_FAILED) { munmap(binMap, binSize); binMap = nullptr; }
    if(idxMap) { free(idxMap); idxMap = nullptr; }
    if(xorMap) { free(xorMap); xorMap = nullptr; }
    if(bloomFilter) { delete bloomFilter; bloomFilter = nullptr; }
}

bool initFiles() {
    const char* files[] = {"BTC.bin", "BTC.idx", "BTC.xor"};
    for(const char* f : files) {
        if(access(f, R_OK) != 0) {
            std::cerr << "[ERROR] File not found: " << f << "\n";
            return false;
        }
    }

    int fd = -1;
    struct stat st;
    size_t idxSize = 0;
    size_t total_read = 0;
    ssize_t bytes_read = 0;

    fd = open("BTC.bin", O_RDONLY);
    if(fd < 0) { std::cerr << "[ERROR] Cannot open BTC.bin\n"; return false; }
    if(fstat(fd, &st) < 0) { close(fd); return false; }
    if(st.st_size <= 0 || (size_t)st.st_size % 20 != 0) {
        std::cerr << "[ERROR] BTC.bin incorrect size: " << st.st_size << "\n";
        close(fd);
        return false;
    }
    binMap = (unsigned char*)mmap(nullptr, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    binSize = st.st_size;
    close(fd);
    fd = -1;
    if(binMap == MAP_FAILED) {
        std::cerr << "[ERROR] mmap BTC.bin failed\n";
        binMap = nullptr;
        binSize = 0;
        return false;
    }

    fd = open("BTC.idx", O_RDONLY);
    if(fd < 0) { std::cerr << "[ERROR] Cannot open BTC.idx\n"; cleanupResources(); return false; }
    if(fstat(fd, &st) < 0) { close(fd); cleanupResources(); return false; }
    idxCount = st.st_size / sizeof(IdxEntry);
    if(idxCount == 0 || (st.st_size % sizeof(IdxEntry)) != 0) {
        std::cerr << "[ERROR] BTC.idx corrupt. Size: " << st.st_size
                  << ", EntrySize: " << sizeof(IdxEntry) << "\n";
        close(fd); cleanupResources(); return false;
    }
    idxSize = st.st_size;
    idxMap = (IdxEntry*)malloc(idxSize);
    if(!idxMap) { std::cerr << "[ERROR] malloc BTC.idx failed\n"; close(fd); cleanupResources(); return false; }
    total_read = 0;
    while(total_read < idxSize) {
        bytes_read = read(fd, (char*)idxMap + total_read, idxSize - total_read);
        if(bytes_read <= 0) {
            std::cerr << "[ERROR] read BTC.idx failed at byte " << total_read << "\n";
            close(fd); cleanupResources(); return false;
        }
        total_read += bytes_read;
    }
    close(fd);
    fd = -1;

    fd = open("BTC.xor", O_RDONLY);
    if(fd < 0) { std::cerr << "[ERROR] Cannot open BTC.xor\n"; cleanupResources(); return false; }
    if(fstat(fd, &st) < 0) { close(fd); cleanupResources(); return false; }
    xorSize = st.st_size;
    if(xorSize == 0) {
        std::cerr << "[ERROR] BTC.xor empty\n";
        close(fd); cleanupResources(); return false;
    }
    xorMap = (unsigned char*)malloc(xorSize);
    if(!xorMap) { std::cerr << "[ERROR] malloc BTC.xor failed\n"; close(fd); cleanupResources(); return false; }
    total_read = 0;
    while(total_read < xorSize) {
        bytes_read = read(fd, (char*)xorMap + total_read, xorSize - total_read);
        if(bytes_read <= 0) {
            std::cerr << "[ERROR] read BTC.xor failed at byte " << total_read << "\n";
            close(fd); cleanupResources(); return false;
        }
        total_read += bytes_read;
    }
    close(fd);
    fd = -1;

    try {
        bloomFilter = new struct bloom;
    } catch(...) {
        std::cerr << "[ERROR] Failed to allocate bloom filter\n";
        cleanupResources();
        return false;
    }
    bloomFilter->entries = binSize / 20;
    bloomFilter->bits = xorSize * 8;
    bloomFilter->bytes = xorSize;
    bloomFilter->hashes = 10;
    bloomFilter->error = 0.001;
    bloomFilter->ready = 1;
    bloomFilter->major = 1;
    bloomFilter->minor = 0;
    bloomFilter->bpe = 14.3;
    bloomFilter->bf = xorMap;

    std::cout << "[OK] Files loaded in RAM\n";
    std::cout << "[OK] BTC.bin: " << (binSize / 20) << " hashes (" << formatWithCommas(binSize) << " bytes)\n";
    std::cout << "[OK] BTC.idx: " << idxCount << " entries (" << formatWithCommas(idxSize) << " bytes)\n";
    std::cout << "[OK] BTC.xor: " << formatWithCommas(xorSize) << " bytes\n";
    return true;
}

void deriveHash160FromPoint(Point &pubKey, unsigned char* outHash160) {
    unsigned char pubKeyComp[33];
    pubKeyComp[0] = pubKey.y.IsEven() ? 0x02 : 0x03;
    pubKey.x.Get32Bytes(pubKeyComp + 1);
    sha256(pubKeyComp, 33, outHash160);
    ripemd160(outHash160, 32, outHash160);
}

void deriveKeccakFromPoint(Point &pubKey, unsigned char* outHash160) {
    unsigned char pubKeyUncomp[64];
    pubKey.x.Get32Bytes(pubKeyUncomp);
    pubKey.y.Get32Bytes(pubKeyUncomp + 32);
    SHA3_256_CTX ctx;
    KECCAK_256_Init(&ctx);
    KECCAK_256_Update(&ctx, pubKeyUncomp, 64);
    unsigned char keccakOut[32];
    KECCAK_256_Final(keccakOut, &ctx);
    memcpy(outHash160, keccakOut + 12, 20);
}

bool searchInBin(const unsigned char* target) {
    if(idxCount == 0) return false;
    size_t lo = 0;
    size_t hi = (idxCount > 0) ? (idxCount - 1) : 0;
    size_t best = (size_t)-1;
    uint64_t tgtPrefix;
    std::memcpy(&tgtPrefix, target, sizeof(uint64_t));
    tgtPrefix = __builtin_bswap64(tgtPrefix);

    while(lo <= hi) {
        size_t mid = lo + (hi - lo) / 2;
        if(idxMap[mid].prefix <= tgtPrefix) { best = mid; lo = mid + 1; }
        else {
            if(mid == 0) break;
            hi = mid - 1;
        }
    }
    if(best == (size_t)-1) return false;

    long long off = idxMap[best].offset;
    size_t count = (size_t)idxMap[best].count;
    if(count == 0) return false;
    if(off < 0) return false;
    if((size_t)off >= binSize) return false;
    if(count > (binSize - (size_t)off) / 20) return false;
    const unsigned char* block = binMap + off;
    lo = 0;
    hi = (count > 0) ? (count - 1) : 0;

    while(lo <= hi) {
        size_t mid = lo + (hi - lo) / 2;
        int cmp = memcmp(block + mid * 20, target, 20);
        if(cmp == 0) return true;
        else if(cmp < 0) lo = mid + 1;
        else {
            if(mid == 0) break;
            hi = mid - 1;
        }
    }
    return false;
}

void saveFoundKey(const char* hex, const char* dec, uint64_t rng, uint64_t sub) {
    std::lock_guard<std::mutex> lock(fileMutex);
    std::ofstream f("FOUND.txt", std::ios::app);
    if(f.is_open()) {
        f << "HEX: " << hex << "\nDEC: " << dec << "\nRANGE_ID: " << rng
          << "\nSUB: " << sub << "/100\nTIME: " << std::time(nullptr)
          << "\n----------------------------------------\n";
        f.close();
    }
    walletsFoundCount.fetch_add(1, std::memory_order_relaxed);
}

std::string makeBar(int filled) {
    std::string bar = "[";
    for(int i = 0; i < 10; i++) bar += (i < filled) ? "#" : "-";
    bar += "]";
    return bar;
}

void getRangeHex(uint64_t rangeId, Int &rangeBlockSize, char* fromHex, size_t fromSize, char* toHex, size_t toSize) {
    Int rIndex;
    rIndex.SetInt64(rangeId > 0 ? rangeId - 1 : 0);
    Int minKey;
    minKey.Set(&rangeBlockSize);
    minKey.Mult(&rIndex);
    Int maxKey;
    maxKey.Set(&minKey);
    maxKey.Add(&rangeBlockSize);
    char* f = minKey.GetBase16();
    char* t = maxKey.GetBase16();
    if(f) { snprintf(fromHex, fromSize, "%s", f); free(f); }
    if(t) { snprintf(toHex, toSize, "%s", t); free(t); }
}

void getSubRangeHex(uint64_t rangeId, uint64_t subId, Int &rangeBlockSize, Int &subRangeSize, char* fromHex, size_t fromSize, char* toHex, size_t toSize) {
    Int rIndex;
    rIndex.SetInt64(rangeId > 0 ? rangeId - 1 : 0);
    Int minKey;
    minKey.Set(&rangeBlockSize);
    minKey.Mult(&rIndex);
    Int subIndex;
    subIndex.SetInt64(subId);
    Int subStart;
    subStart.Set(&subRangeSize);
    subStart.Mult(&subIndex);
    subStart.Add(&minKey);
    Int subEnd;
    subEnd.Set(&subStart);
    subEnd.Add(&subRangeSize);
    char* f = subStart.GetBase16();
    char* t = subEnd.GetBase16();
    if(f) { snprintf(fromHex, fromSize, "%s", f); free(f); }
    if(t) { snprintf(toHex, toSize, "%s", t); free(t); }
}

void printReport(uint64_t keysInPeriod, uint64_t grandTotal, uint64_t rangeId,
                 uint64_t found, uint64_t falsePos, uint64_t truePos, unsigned int nThreads,
                 Int &rangeBlockSize, Int &subRangeSize) {
    std::lock_guard<std::mutex> lock(consoleMutex);

    if(nThreads > MAX_THREADS) nThreads = MAX_THREADS;

    double spd = (double)keysInPeriod / 50.0;
    double rangePct = (double)rangeId / 256.0 * 100.0;
    int rBar = (int)(rangePct / 10.0);
    if(rBar > 10) rBar = 10;
    double eff = (truePos + falsePos > 0) ? ((double)truePos / (truePos + falsePos) * 100.0) : 0.0;

    uint64_t curSub = currentSubRange.load(std::memory_order_relaxed);

    char rangeFrom[96];
    char rangeTo[96];
    getRangeHex(rangeId, rangeBlockSize, rangeFrom, sizeof(rangeFrom), rangeTo, sizeof(rangeTo));

    char subFrom[96];
    char subTo[96];
    getSubRangeHex(rangeId, curSub, rangeBlockSize, subRangeSize, subFrom, sizeof(subFrom), subTo, sizeof(subTo));

    std::vector<std::string> L;
    std::ostringstream os;

    os.str(""); os << "<[x]> Version 1.0.2. Developep By Zoey Alejandroo JF"; L.push_back(os.str());
    os.str(""); os << "<[x]> Pure Randomization Mode"; L.push_back(os.str());
    os.str(""); os << "<[x]> Cores: " << nThreads << " | Threds : " << nThreads << " |"; L.push_back(os.str());
    os.str(""); os << "<[x]> Bit Range " << rangeId; L.push_back(os.str());
    os.str(""); os << "<[x]> --from : 0x" << rangeFrom; L.push_back(os.str());
    os.str(""); os << "<[x]> --to   : 0x" << rangeTo; L.push_back(os.str());
    os.str(""); os << "<[x]> Sub-Range " << curSub; L.push_back(os.str());
    os.str(""); os << "<[x]> -- from : 0x" << subFrom; L.push_back(os.str());
    os.str(""); os << "<[x]> -- to   : 0x" << subTo; L.push_back(os.str());
    os.str(""); os << "<[x]> RANGE            " << std::setw(3) << rangeId << " / 256     " << makeBar(rBar) << "  " << std::fixed << std::setprecision(2) << std::setw(6) << rangePct << "%"; L.push_back(os.str());
    os.str(""); os << "<[x]> Sub-Ranges Divided by threads"; L.push_back(os.str());
    for(unsigned int t = 0; t < nThreads && t < MAX_THREADS; t++) {
        uint64_t tSub = threadSubRanges[t].load(std::memory_order_relaxed);
        double tPct = (double)tSub / 100.0 * 100.0;
        int tBar = (int)(tPct / 10.0);
        if(tBar > 10) tBar = 10;
        os.str(""); os << "<[x]> Threads " << (t + 1) << " : " << std::setw(3) << tSub << " / 100      " << makeBar(tBar) << "  " << std::fixed << std::setprecision(2) << std::setw(6) << tPct << "%"; L.push_back(os.str());
    }
    os.str(""); os << "<[x]> [BLOOM] : " << formatWithCommas(falsePos) << " False Positive/s] <[=]> [EFFICIENCY/s : " << (uint64_t)eff << "]"; L.push_back(os.str());
    os.str(""); os << "<[x]> [SPEED] : " << std::fixed << std::setprecision(2) << spd << " keys/s] <=[X]=> [TOTAL : " << formatWithCommas(grandTotal) << " key/s]"; L.push_back(os.str());
    os.str(""); os << "<[x]> [FOUND] :  " << found << "  [Wallet/s]"; L.push_back(os.str());

    int lineCount = (int)L.size();

    if(gReportLines > 0) {
        std::cout << "\033[" << (gReportLines - 1) << "A";
    }

    for(int i = 0; i < lineCount; i++) {
        std::cout << "\r\033[2K" << L[i];
        if(i + 1 < lineCount) std::cout << "\n";
    }

    std::cout.flush();
    gReportLines = lineCount;
}

void workerThread(int id, unsigned int totalThreads) {
    Secp256K1 localSecp;
    localSecp.Init();

    uint64_t seed = (uint64_t)std::chrono::steady_clock::now().time_since_epoch().count();
    seed ^= ((uint64_t)id * 2654435761ULL);
    seed ^= ((uint64_t)std::hash<std::thread::id>{}(std::this_thread::get_id()) << 13);
    std::mt19937_64 gen(seed);

    Int globalMax, rangeBlockSize, subRangeSize;
    globalMax.SetInt64(0xFFFFFFFFFFFFFFFFULL);
    globalMax.ShiftL(192);
    Int twoFiftySix;
    twoFiftySix.SetInt64(256);
    rangeBlockSize.Set(&globalMax);
    rangeBlockSize.Div(&twoFiftySix);
    Int hundred;
    hundred.SetInt64(100);
    subRangeSize.Set(&rangeBlockSize);
    subRangeSize.Div(&hundred);

    Int numThreads;
    numThreads.SetInt64(totalThreads);
    Int zoneSize;
    zoneSize.Set(&subRangeSize);
    zoneSize.Div(&numThreads);

    Int beta;
    beta.SetBase16("7ae96a2b657c07106e64479eac3434e99cf0497512f58995c1396c28719501ee");
    Int beta2;
    beta2.SetBase16("7ae96a2b657c07106e64479eac3434e99cf0497512f58995c1396c28719501ee");
    beta2.ModMulK1(&beta2, &beta);

    Int lambda;
    lambda.SetBase16("5363ad4cc05c30e0a5261c028812645a122e22ea20816678df02967c1b23bd72");
    Int lambda2;
    lambda2.Set(&lambda);
    lambda2.Mult(&lambda);
    Int curveOrder;
    curveOrder.SetBase16("FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFEBAAEDCE6AF48A03BBFD25E8CD0364141");
    Int lambda2Mod;
    lambda2Mod.Set(&lambda2);
    Int dummyMod;
    lambda2Mod.Div(&curveOrder, &dummyMod);
    lambda2Mod.Set(&dummyMod);

    Int fragCount;
    fragCount.SetInt64(100);
    Int fragSize;
    fragSize.Set(&zoneSize);
    fragSize.Div(&fragCount);

    Int microCount;
    microCount.SetInt64(100);
    Int microSize;
    microSize.Set(&fragSize);
    microSize.Div(&microCount);

    const int BATCH_SIZE = 1024;
    Int effectiveBatch;
    effectiveBatch.SetInt64(BATCH_SIZE);
    if(microSize.IsLower(&effectiveBatch)) {
        effectiveBatch.Set(&microSize);
    }
    int batchLimit = (int)effectiveBatch.GetInt64();
    if(batchLimit < 1) batchLimit = 1;

    std::uniform_int_distribution<uint64_t> subDist(0, 99);
    uint64_t curSub = subDist(gen);
    if(id < MAX_THREADS) {
        threadSubRanges[id].store(curSub, std::memory_order_relaxed);
    }
    int subChangesCount = 0;
    const int RANGE_CHANGE_THRESHOLD = 24;
    auto tStart = std::chrono::steady_clock::now();

    Int idOffset;
    idOffset.SetInt64(id);
    idOffset.Mult(&zoneSize);

    while(!foundFlag.load(std::memory_order_relaxed)) {
        auto now = std::chrono::steady_clock::now();
        if(std::chrono::duration<double>(now - tStart).count() >= 25.0) {
            subChangesCount++;
            if(subChangesCount >= RANGE_CHANGE_THRESHOLD) {
                uint64_t oldRange = currentRangeId.load(std::memory_order_relaxed);
                uint64_t nextRange = (oldRange < 1 || oldRange >= 256) ? 1 : oldRange + 1;
                while(!currentRangeId.compare_exchange_weak(oldRange, nextRange, std::memory_order_relaxed)) {
                    nextRange = (oldRange < 1 || oldRange >= 256) ? 1 : oldRange + 1;
                }
                subChangesCount = 0;
            }
            curSub = subDist(gen);
            if(id < MAX_THREADS) {
                threadSubRanges[id].store(curSub, std::memory_order_relaxed);
            }
            currentSubRange.store(curSub, std::memory_order_relaxed);
            tStart = now;
        }

        uint64_t rId = currentRangeId.load(std::memory_order_relaxed);
if(rId < 1) rId = 1;
        if(rId > 256) rId = 256;

        Int minKey, maxKey;
        Int rIndex;
        rIndex.SetInt64(rId - 1);
        minKey.Set(&rangeBlockSize);
        minKey.Mult(&rIndex);
        maxKey.Set(&minKey);
        maxKey.Add(&rangeBlockSize);

        Int subIndex;
        subIndex.SetInt64(curSub);
        Int subStart, subEnd;
        subStart.Set(&subRangeSize);
        subStart.Mult(&subIndex);
        subStart.Add(&minKey);
        subEnd.Set(&subStart);
        subEnd.Add(&subRangeSize);

        Int zoneStart;
        zoneStart.Set(&subStart);
        zoneStart.Add(&idOffset);
        Int zoneEnd;
        if(id == (int)(totalThreads - 1)) {
            zoneEnd.Set(&subEnd);
        } else {
            zoneEnd.Set(&zoneStart);
            zoneEnd.Add(&zoneSize);
        }

        Int privKey;
        privKey.Rand(&zoneStart, &zoneEnd);
        Point p1 = localSecp.ComputePublicKey(&privKey);

        Point p2;
        p2.x.ModMulK1(&p1.x, &beta);
        p2.y.Set(&p1.y);
        p2.z.Set(&p1.z);

        Point p3;
        p3.x.ModMulK1(&p1.x, &beta2);
        p3.y.Set(&p1.y);
        p3.z.Set(&p1.z);

        Int privKey2;
        privKey2.Set(&privKey);
        privKey2.Mult(&lambda);
        Int modBuf2;
        privKey2.Div(&curveOrder, &modBuf2);
        privKey2.Set(&modBuf2);

        Int privKey3;
        privKey3.Set(&privKey);
        privKey3.Mult(&lambda2);
        Int modBuf3;
        privKey3.Div(&curveOrder, &modBuf3);
        privKey3.Set(&modBuf3);

        uint64_t localCounter = 0;

        for(int batch = 0; batch < batchLimit; batch++) {
            if(foundFlag.load(std::memory_order_relaxed)) break;

            unsigned char h160[20];
            bool found = false;
            Int* matchedKey = nullptr;

            deriveHash160FromPoint(p1, h160);
            if(bloom_check(bloomFilter, h160, 20) == 1) {
                if(searchInBin(h160)) { found = true; matchedKey = &privKey; }
                else bloomFalsePositives.fetch_add(1, std::memory_order_relaxed);
            }
            if(!found) {
                deriveKeccakFromPoint(p1, h160);
                if(bloom_check(bloomFilter, h160, 20) == 1) {
                    if(searchInBin(h160)) { found = true; matchedKey = &privKey; }
                    else bloomFalsePositives.fetch_add(1, std::memory_order_relaxed);
                }
            }

            if(!found) {
                deriveHash160FromPoint(p2, h160);
                if(bloom_check(bloomFilter, h160, 20) == 1) {
                    if(searchInBin(h160)) { found = true; matchedKey = &privKey2; }
                    else bloomFalsePositives.fetch_add(1, std::memory_order_relaxed);
                }
            }
            if(!found) {
                deriveKeccakFromPoint(p2, h160);
                if(bloom_check(bloomFilter, h160, 20) == 1) {
                    if(searchInBin(h160)) { found = true; matchedKey = &privKey2; }
                    else bloomFalsePositives.fetch_add(1, std::memory_order_relaxed);
                }
            }

            if(!found) {
                deriveHash160FromPoint(p3, h160);
                if(bloom_check(bloomFilter, h160, 20) == 1) {
                    if(searchInBin(h160)) { found = true; matchedKey = &privKey3; }
                    else bloomFalsePositives.fetch_add(1, std::memory_order_relaxed);
                }
            }
            if(!found) {
                deriveKeccakFromPoint(p3, h160);
                if(bloom_check(bloomFilter, h160, 20) == 1) {
                    if(searchInBin(h160)) { found = true; matchedKey = &privKey3; }
                    else bloomFalsePositives.fetch_add(1, std::memory_order_relaxed);
                }
            }

            if(found) {
                bloomTruePositives.fetch_add(1, std::memory_order_relaxed);
                char* hex = matchedKey->GetBase16();
                char* dec = matchedKey->GetBase10();
                saveFoundKey(hex, dec, currentRangeId.load(std::memory_order_relaxed), curSub);
                foundFlag.store(true, std::memory_order_release);
                {
                    std::lock_guard<std::mutex> lk(consoleMutex);
                    std::cout << "\n\nKEY FOUND T" << id << ": " << hex << "\n\n";
                    std::cout.flush();
                }
                if(hex) free(hex);
                if(dec) free(dec);
                break;
            }

            localCounter += 3;

            if(batch < batchLimit - 1) {
                privKey.AddOne();
                if(privKey.IsGreaterOrEqual(&zoneEnd)) {
                    privKey.Rand(&zoneStart, &zoneEnd);
                    Point tmp = localSecp.ComputePublicKey(&privKey);
                    p1.x.Set(&tmp.x);
                    p1.y.Set(&tmp.y);
                    p1.z.Set(&tmp.z);

                    p2.x.ModMulK1(&p1.x, &beta);
                    p2.y.Set(&p1.y);
                    p2.z.Set(&p1.z);

                    p3.x.ModMulK1(&p1.x, &beta2);
                    p3.y.Set(&p1.y);
                    p3.z.Set(&p1.z);

                    privKey2.Set(&privKey);
                    privKey2.Mult(&lambda);
                    Int mb2;
                    privKey2.Div(&curveOrder, &mb2);
                    privKey2.Set(&mb2);

                    privKey3.Set(&privKey);
                    privKey3.Mult(&lambda2);
                    Int mb3;
                    privKey3.Div(&curveOrder, &mb3);
                    privKey3.Set(&mb3);
                } else {
                    Point tmp = localSecp.NextKey(p1);
                    p1.x.Set(&tmp.x);
                    p1.y.Set(&tmp.y);
                    p1.z.Set(&tmp.z);

                    p2.x.ModMulK1(&p1.x, &beta);
                    p2.y.Set(&p1.y);
                    p2.z.Set(&p1.z);

                    p3.x.ModMulK1(&p1.x, &beta2);
                    p3.y.Set(&p1.y);
                    p3.z.Set(&p1.z);

                    privKey2.Add(&lambda);
                    if(privKey2.IsGreaterOrEqual(&curveOrder)) {
                        privKey2.Sub(&curveOrder);
                    }

                    privKey3.Add(&lambda2Mod);
                    if(privKey3.IsGreaterOrEqual(&curveOrder)) {
                        privKey3.Sub(&curveOrder);
                    }
                }
            }
        }

        totalKeysScanned.fetch_add(localCounter, std::memory_order_relaxed);
        std::this_thread::sleep_for(std::chrono::microseconds(50));
    }
}

void reportThread() {
    uint64_t lastReportTotal = 0;
    uint64_t elapsed = 0;
    unsigned int nt = activeThreadCount.load(std::memory_order_relaxed);
    if(nt > MAX_THREADS) nt = MAX_THREADS;

    Int globalMax, rangeBlockSize, subRangeSize;
    globalMax.SetInt64(0xFFFFFFFFFFFFFFFFULL);
    globalMax.ShiftL(192);
    Int twoFiftySix;
    twoFiftySix.SetInt64(256);
    rangeBlockSize.Set(&globalMax);
    rangeBlockSize.Div(&twoFiftySix);
    Int hundred;
    hundred.SetInt64(100);
    subRangeSize.Set(&rangeBlockSize);
    subRangeSize.Div(&hundred);

    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    printReport(0, 0, currentRangeId.load(std::memory_order_relaxed),
               0, bloomFalsePositives.load(std::memory_order_relaxed),
               bloomTruePositives.load(std::memory_order_relaxed), nt,
               rangeBlockSize, subRangeSize);

    while(!foundFlag.load(std::memory_order_acquire)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        elapsed += 100;
        if(elapsed >= 50000) {
            elapsed = 0;
            uint64_t currTotal = totalKeysScanned.load(std::memory_order_relaxed);
            uint64_t keysInLastPeriod = currTotal - lastReportTotal;
            lastReportTotal = currTotal;
            uint64_t fc = walletsFoundCount.load(std::memory_order_relaxed);
            printReport(keysInLastPeriod, currTotal, currentRangeId.load(std::memory_order_relaxed),
                       fc, bloomFalsePositives.load(std::memory_order_relaxed),
                       bloomTruePositives.load(std::memory_order_relaxed), nt,
                       rangeBlockSize, subRangeSize);
        }
    }
}

int main() {
    std::cout << "Starting BTC Hunter...\n";
    if(!initFiles()) {
        cleanupResources();
        return 1;
    }
    int_randominit();

    uint64_t mainSeed = (uint64_t)std::chrono::steady_clock::now().time_since_epoch().count();
    mainSeed ^= ((uint64_t)getpid() << 17);
    std::mt19937_64 initGen(mainSeed);

    std::uniform_int_distribution<uint64_t> rangeDist(1, 256);
    currentRangeId.store(rangeDist(initGen), std::memory_order_relaxed);
    currentSubRange.store(0, std::memory_order_relaxed);
    unsigned int nt = std::thread::hardware_concurrency();
    if(nt == 0) nt = 4;
    if(nt > MAX_THREADS) nt = MAX_THREADS;
    activeThreadCount.store(nt, std::memory_order_relaxed);
    for(unsigned int i = 0; i < MAX_THREADS; i++) threadSubRanges[i].store(0, std::memory_order_relaxed);

    std::cout << "Scanning...\n";

    try {
        std::vector<std::thread> w;
        w.reserve(nt);
        for(unsigned int i = 0; i < nt; i++) w.emplace_back(workerThread, i, nt);
        std::thread rp(reportThread);
        for(auto& t : w) {
            if(t.joinable()) t.join();
        }
        if(rp.joinable()) rp.join();
    } catch(...) {
        foundFlag.store(true, std::memory_order_release);
    }

    cleanupResources();
    return 0;
}
