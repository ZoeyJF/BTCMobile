CXX := g++
UNAME_M := $(shell uname -m)
ARCH_FLAG := -march=native
ifneq ($(filter aarch64 arm%,$(UNAME_M)),)
    ARCH_FLAG := -march=armv8-a
endif

CXXFLAGS := -std=c++17 -Ofast $(ARCH_FLAG) -pthread -D_GNU_SOURCE -w -I.
LDFLAGS := -pthread -lpthread

OBJDIR := Secp256k1/.o

SRCS_C := $(wildcard Secp256k1/*.c)
SRCS_CPP := $(wildcard Secp256k1/*.cpp)

OBJS_LIB := $(patsubst Secp256k1/%.c,$(OBJDIR)/%.o,$(SRCS_C)) \
            $(patsubst Secp256k1/%.cpp,$(OBJDIR)/%.o,$(SRCS_CPP))

OBJS_ROOT := $(OBJDIR)/BTC.o $(OBJDIR)/CREATE-BIX.o

all: BTC CREATE-BIX

$(OBJDIR):
	mkdir -p $(OBJDIR)

$(OBJDIR)/%.o: Secp256k1/%.c | $(OBJDIR)
	$(CXX) $(CXXFLAGS) -x c++ -c $< -o $@

$(OBJDIR)/%.o: Secp256k1/%.cpp | $(OBJDIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(OBJDIR)/BTC.o: BTC.cpp | $(OBJDIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(OBJDIR)/CREATE-BIX.o: CREATE-BIX.cpp | $(OBJDIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

BTC: $(OBJS_LIB) $(OBJDIR)/BTC.o
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

CREATE-BIX: $(OBJS_LIB) $(OBJDIR)/CREATE-BIX.o
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

clean:
	rm -rf $(OBJDIR) BTC CREATE-BIX
