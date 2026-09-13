CXX      = g++
CXXFLAGS = -O3 -std=c++17 -pthread
LDFLAGS  = -lgmp

TARGET   = BTC
NLINES   = 16

SRCS     = BTC.cpp \
           Biblioteca/GMP256K1.cpp \
           Biblioteca/Int.cpp \
           Biblioteca/IntGroup.cpp \
           Biblioteca/IntMod.cpp \
           Biblioteca/Point.cpp \
           Biblioteca/Random.cpp \
           Biblioteca/bloom.cpp \
           Biblioteca/sha256.cpp \
           Biblioteca/ripemd160.cpp \
           Biblioteca/keccak.c \
           Biblioteca/rmd160.c \
           Biblioteca/sha3.c \
           Biblioteca/util.c \
           Biblioteca/xxhash.c

OBJS     = $(SRCS:.cpp=.o)
OBJS     := $(OBJS:.c=.o)

all: $(TARGET)
	@printf '\033[%dA\033[J' $(NLINES)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

%.o: %.c
	$(CXX) $(CXXFLAGS) -c -o $@ $<

clean:
	@rm -f $(OBJS) $(TARGET)

.PHONY: all clean