.RECIPEPREFIX = >
CXX = g++
CC  = gcc
UNAME_M := $(shell uname -m)
ifeq ($(UNAME_M),x86_64)
    ARCH_FLAGS = -march=native
else ifeq ($(UNAME_M),aarch64)
    ARCH_FLAGS = -march=armv8-a+crypto
else
    ARCH_FLAGS =
endif
INC  = -IBiblioteca
OPT  = -O3 -DNDEBUG $(ARCH_FLAGS)
STD  = -std=c++17
PTH  = -pthread
WARN = -Wall
OBJDIR = Biblioteca/.o
LIB_CPP = Biblioteca/AddressHelper.cpp \
          Biblioteca/xorfilter.cpp \
          Biblioteca/BTC_core.cpp \
          Biblioteca/GMP256K1.cpp \
          Biblioteca/Int.cpp \
          Biblioteca/IntMod.cpp \
          Biblioteca/IntGroup.cpp \
          Biblioteca/Point.cpp \
          Biblioteca/Random.cpp \
          Biblioteca/sha256.cpp \
          Biblioteca/ripemd160.cpp \
          Biblioteca/util.cpp
LIB_C   = Biblioteca/rmd160.c \
          Biblioteca/sha3.c \
          Biblioteca/keccak.c
LIB_OBJ = $(patsubst Biblioteca/%.cpp,$(OBJDIR)/%.o,$(LIB_CPP)) \
          $(patsubst Biblioteca/%.c,$(OBJDIR)/%.o,$(LIB_C))
CREATE_OBJ = $(OBJDIR)/CREATE-BIX.o
BTC_OBJ    = $(OBJDIR)/BTC.o
all: CREATE-BIX BTC
CREATE-BIX: $(CREATE_OBJ) $(LIB_OBJ)
> $(CXX) $(OPT) $(STD) $(INC) $(PTH) -o CREATE-BIX $(CREATE_OBJ) $(LIB_OBJ)
BTC: $(BTC_OBJ) $(LIB_OBJ)
> $(CXX) $(OPT) $(STD) $(INC) $(PTH) -o BTC $(BTC_OBJ) $(LIB_OBJ)
$(OBJDIR)/%.o: Biblioteca/%.cpp
> @mkdir -p $(OBJDIR)
> $(CXX) $(OPT) $(STD) $(INC) $(PTH) $(WARN) -c -o $@ $<
$(OBJDIR)/%.o: Biblioteca/%.c
> @mkdir -p $(OBJDIR)
> $(CC) $(OPT) $(INC) $(PTH) $(WARN) -c -o $@ $<
$(OBJDIR)/CREATE-BIX.o: CREATE-BIX.cpp
> @mkdir -p $(OBJDIR)
> $(CXX) $(OPT) $(STD) $(INC) $(PTH) $(WARN) -c -o $@ $<
$(OBJDIR)/BTC.o: BTC.cpp
> @mkdir -p $(OBJDIR)
> $(CXX) $(OPT) $(STD) $(INC) $(PTH) $(WARN) -c -o $@ $<
clean:
> rm -rf $(OBJDIR)
> rm -f CREATE-BIX BTC
.PHONY: all clean
