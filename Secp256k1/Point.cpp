/*
 * BTCMobile - High-Performance Cryptographic Engine
 * Copyright (c) 2026 Zoey Alejandroo JF
 *
 * Technical Description:
 * Implements high-speed Secp256k1 field arithmetic, Montgomery batch inversion,
 * and accelerated cryptographic hashing (SHA256, RIPEMD160, Keccak-256).
 */


#include "Point.h"

Point::Point() {
}

Point::Point(const Point &p) {
  x.Set((Int *)&p.x);
  y.Set((Int *)&p.y);
  z.Set((Int *)&p.z);
}

Point::Point(Int *cx,Int *cy,Int *cz) {
  x.Set(cx);
  y.Set(cy);
  z.Set(cz);
}

Point::Point(Int *cx, Int *cz) {
  x.Set(cx);
  z.Set(cz);
}

void Point::Clear() {
  x.SetInt32(0);
  y.SetInt32(0);
  z.SetInt32(0);
}

void Point::Set(Int *cx, Int *cy,Int *cz) {
  x.Set(cx);
  y.Set(cy);
  z.Set(cz);
}

Point::~Point() {
}

void Point::Set(Point &p) {
  x.Set(&p.x);
  y.Set(&p.y);
  z.Set(&p.z);
}

bool Point::isZero() {
  return x.IsZero() && y.IsZero();
}

void Point::Reduce() {

  Int i(&z);
  i.ModInv();
  x.ModMul(&x,&i);
  y.ModMul(&y,&i);
  z.SetInt32(1);

}

bool Point::equals(Point &p) {
  return x.IsEqual(&p.x) && y.IsEqual(&p.y) && z.IsEqual(&p.z);
}
