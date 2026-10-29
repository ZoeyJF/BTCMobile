/*
 * BTCMobile - High-Performance Cryptographic Engine
 * Copyright (c) 2026 Zoey Alejandroo JF
 *
 * Technical Description:
 * Implements high-speed Secp256k1 field arithmetic, Montgomery batch inversion,
 * and accelerated cryptographic hashing (SHA256, RIPEMD160, Keccak-256).
 */


#ifndef RANDOM_H
#define RANDOM_H

double rnd();
unsigned long rndl();
void rseed(unsigned long seed);

#endif
