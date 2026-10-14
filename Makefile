CXX = g++
CXXFLAGS = -O3 -std=c++17 -I. -ISecp256k1Tool -pthread -mssse3
CFLAGS = -O3 -I. -ISecp256k1Tool

SECP_SRCS = Secp256k1Tool/Int.cpp \
            Secp256k1Tool/IntGroup.cpp \
            Secp256k1Tool/IntMod.cpp \
            Secp256k1Tool/Point.cpp \
            Secp256k1Tool/Random.cpp \
            Secp256k1Tool/SECP256K1.cpp \
            Secp256k1Tool/bloom.cpp \
            Secp256k1Tool/ripemd160.cpp \
            Secp256k1Tool/sha256.cpp \
            Secp256k1Tool/sha512.cpp \
            Secp256k1Tool/sha256_sse.cpp \
            Secp256k1Tool/ripemd160_sse.cpp \
            Secp256k1Tool/xxhash.c \
            Secp256k1Tool/rmd160.c \
            Secp256k1Tool/sha3.c \
            Secp256k1Tool/keccak.c \
            Secp256k1Tool/util.c

SECP_OBJS = $(SECP_SRCS:.cpp=.o)
SECP_OBJS := $(SECP_OBJS:.c=.o)

all: CREATE-BiX BTC

CREATE-BiX: CREATE-BiX.o Secp256k1Tool/bloom.o Secp256k1Tool/xxhash.o
	$(CXX) $(CXXFLAGS) -o $@ $^ -lm

BTC: BTC.o $(SECP_OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^ -lm

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f *.o Secp256k1Tool/*.o CREATE-BiX BTC BTC.bin BTC.xor BTC.idx
