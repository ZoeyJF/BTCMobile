/*
 * BTCMobile - High-Performance Cryptographic Engine
 * Copyright (c) 2026 Zoey Alejandroo JF
 *
 * Technical Description:
 * Implements high-speed Secp256k1 field arithmetic, Montgomery batch inversion,
 * and accelerated cryptographic hashing (SHA256, RIPEMD160, Keccak-256).
 */


#ifndef INTGROUPH
#define INTGROUPH

#include "Int.h"
#include <vector>

class IntGroup {

public:

	IntGroup(int size);
	~IntGroup();
	void Set(Int *pts);
	void ModInv();

private:

	Int *ints;
  Int *subp;
  int size;

};

#endif // INTGROUPCPUH
