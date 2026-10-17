#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "GMP256K1.h"
#include "Point.h"
#include "util.h"
#include "sha256.h"
#include "ripemd160.h"
#include "keccak.h"

Secp256K1::Secp256K1() {
}

Secp256K1::~Secp256K1() {
}

void Secp256K1::Init() {
	P.SetBase16("FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFEFFFFFC2F");
	Int::SetupField(&P);
	G.x.SetBase16("79BE667EF9DCBBAC55A06295CE870B07029BFCDB2DCE28D959F2815B16F81798");
	G.y.SetBase16("483ADA7726A3C4655DA4FBFC0E1108A8FD17B448A68554199C47D08FFB10D4B8");
	G.z.SetInt32(1);
	order.SetBase16("FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFEBAAEDCE6AF48A03BBFD25E8CD0364141");
	Int::InitK1(&order);

	Point B = G;
	for (int i = 0; i < 64; i++) {
		GTable[i * 16 + 0].Clear();
		GTable[i * 16 + 0].z.SetInt32(0);
		GTable[i * 16 + 1] = B;
		for (int v = 2; v < 16; v++) {
			GTable[i * 16 + v] = AddDirect(GTable[i * 16 + v - 1], B);
		}
		B = DoubleDirect(B);
		B = DoubleDirect(B);
		B = DoubleDirect(B);
		B = DoubleDirect(B);
	}
}

Point Secp256K1::Negation(Point &p) {
	Point Q;
	Q.x.Set(&p.x);
	Q.y.Set(&this->P);
	Q.y.Sub(&p.y);
	Q.z.Set(&p.z);
	return Q;
}

Point Secp256K1::DoubleDirect(Point &p) {
	if (p.z.IsZero() || p.y.IsZero()) {
		Point zero;
		zero.Clear();
		return zero;
	}
	Int _s, _p, a;
	Point r;
	r.z.SetInt32(1);
	_s.ModMulK1(&p.x, &p.x);
	_p.ModAdd(&_s, &_s);
	_p.ModAdd(&_s);
	a.ModAdd(&p.y, &p.y);
	a.ModInv();
	_s.ModMulK1(&_p, &a);
	_p.ModMulK1(&_s, &_s);
	a.ModAdd(&p.x, &p.x);
	a.ModNeg();
	r.x.ModAdd(&a, &_p);
	a.ModSub(&r.x, &p.x);
	_p.ModMulK1(&a, &_s);
	r.y.ModAdd(&_p, &p.y);
	r.y.ModNeg();
	return r;
}

Int Secp256K1::GetY(Int x, bool isEven) {
	Int y;
	y.ModSquareK1(&x);
	y.ModMulK1(&x);
	y.ModAdd(7);
	y.ModSqrt();
	if (y.IsEven() != isEven) {
		y.ModNeg();
	}
	return y;
}

Point Secp256K1::ComputePublicKey(Int *privKey) {
	uint8_t buffer[32];
	privKey->Get32Bytes(buffer);
	Point Q;
	Q.Clear();
	Q.z.SetInt32(0);

	for (int i = 0; i < 32; i++) {
		uint8_t byte_val = buffer[31 - i];
		uint8_t lo = byte_val & 0x0F;
		uint8_t hi = (byte_val >> 4) & 0x0F;

		int win_lo = i * 2;
		int win_hi = i * 2 + 1;

		if (lo > 0) {
			if (Q.z.IsZero()) Q = GTable[win_lo * 16 + lo];
			else Q = Add2(Q, GTable[win_lo * 16 + lo]);
		}
		if (hi > 0) {
			if (Q.z.IsZero()) Q = GTable[win_hi * 16 + hi];
			else Q = Add2(Q, GTable[win_hi * 16 + hi]);
		}
	}
	Q.Reduce();
	return Q;
}

Point Secp256K1::NextKey(Point &key) {
	return AddDirect(key, G);
}

bool Secp256K1::EC(Point &p) {
	Int _s, _p;
	_s.ModSquareK1(&p.x);
	_p.ModMulK1(&_s, &p.x);
	_p.ModAdd(7);
	_s.ModMulK1(&p.y, &p.y);
	_s.ModSub(&_p);
	return _s.IsZero();
}

bool Secp256K1::ParsePublicKeyHex(char *str, Point &ret, bool &isCompressed) {
	char tempbuffer[65];
	int len = strlen(str);
	ret.Clear();
	if (len < 2) return false;
	uint8_t type = GetByte(str, 0);
	switch (type) {
		case 0x02:
			if (len != 66) return false;
			ret.x.SetBase16(str + 2);
			ret.y = GetY(ret.x, true);
			isCompressed = true;
			break;
		case 0x03:
			if (len != 66) return false;
			ret.x.SetBase16(str + 2);
			ret.y = GetY(ret.x, false);
			isCompressed = true;
			break;
		case 0x04:
			if (len != 130) return false;
			strncpy(tempbuffer, str + 2, 64);
			tempbuffer[64] = 0;
			ret.x.SetBase16(tempbuffer);
			ret.y.SetBase16(str + 66);
			isCompressed = false;
			break;
		default:
			return false;
	}
	ret.z.SetInt32(1);
	if (!EC(ret)) return false;
	return true;
}

char* Secp256K1::GetPublicKeyHex(bool compressed, Point &pubKey) {
	unsigned char publicKeyBytes[65];
	char *ret = NULL;
	if (!compressed) {
		publicKeyBytes[0] = 0x04;
		pubKey.x.Get32Bytes(publicKeyBytes + 1);
		pubKey.y.Get32Bytes(publicKeyBytes + 33);
		ret = (char*)tohex((char*)publicKeyBytes, 65);
	} else {
		publicKeyBytes[0] = pubKey.y.IsEven() ? 0x02 : 0x03;
		pubKey.x.Get32Bytes(publicKeyBytes + 1);
		ret = (char*)tohex((char*)publicKeyBytes, 33);
	}
	return ret;
}

void Secp256K1::GetPublicKeyHex(bool compressed, Point &pubKey, char *dst) {
	unsigned char publicKeyBytes[65];
	if (!compressed) {
		publicKeyBytes[0] = 0x04;
		pubKey.x.Get32Bytes(publicKeyBytes + 1);
		pubKey.y.Get32Bytes(publicKeyBytes + 33);
		tohex_dst((char*)publicKeyBytes, 65, dst);
	} else {
		publicKeyBytes[0] = pubKey.y.IsEven() ? 0x02 : 0x03;
		pubKey.x.Get32Bytes(publicKeyBytes + 1);
		tohex_dst((char*)publicKeyBytes, 33, dst);
	}
}

char* Secp256K1::GetPublicKeyRaw(bool compressed, Point &pubKey) {
	char *ret = (char*)malloc(65);
	if (!ret) return NULL;
	if (!compressed) {
		ret[0] = 0x04;
		pubKey.x.Get32Bytes((unsigned char*)(ret + 1));
		pubKey.y.Get32Bytes((unsigned char*)(ret + 33));
	} else {
		ret[0] = pubKey.y.IsEven() ? 0x02 : 0x03;
		pubKey.x.Get32Bytes((unsigned char*)(ret + 1));
	}
	return ret;
}

void Secp256K1::GetPublicKeyRaw(bool compressed, Point &pubKey, char *dst) {
	if (!compressed) {
		dst[0] = 0x04;
		pubKey.x.Get32Bytes((unsigned char*)(dst + 1));
		pubKey.y.Get32Bytes((unsigned char*)(dst + 33));
	} else {
		dst[0] = pubKey.y.IsEven() ? 0x02 : 0x03;
		pubKey.x.Get32Bytes((unsigned char*)(dst + 1));
	}
}

uint8_t Secp256K1::GetByte(char *str, int idx) {
	char tmp[3];
	int val = 0;
	tmp[0] = str[2 * idx];
	tmp[1] = str[2 * idx + 1];
	tmp[2] = 0;
	if (sscanf(tmp, "%X", &val) != 1) return 0;
	return (uint8_t)val;
}

Point Secp256K1::AddDirect(Point &p1, Point &p2) {
	if (p1.z.IsZero()) return p2;
	if (p2.z.IsZero()) return p1;
	if (p1.x.IsEqual(&p2.x)) {
		if (p1.y.IsEqual(&p2.y)) return DoubleDirect(p1);
		Point zero; zero.Clear(); return zero;
	}
	Int _s, _p, dy, dx;
	Point r;
	r.z.SetInt32(1);
	dy.ModSub(&p2.y, &p1.y);
	dx.ModSub(&p2.x, &p1.x);
	dx.ModInv();
	_s.ModMulK1(&dy, &dx);
	_p.ModSquareK1(&_s);
	r.x.ModSub(&_p, &p1.x);
	r.x.ModSub(&p2.x);
	r.y.ModSub(&p2.x, &r.x);
	r.y.ModMulK1(&_s);
	r.y.ModSub(&p2.y);
	return r;
}

Point Secp256K1::Add2(Point &p1, Point &p2) {
	if (p1.z.IsZero()) return p2;
	if (p2.z.IsZero()) return p1;

	Int z1_2, u2, s2, h, r;
	z1_2.ModSquareK1(&p1.z);
	u2.ModMulK1(&p2.x, &z1_2);

	Int z1_3;
	z1_3.ModMulK1(&z1_2, &p1.z);
	s2.ModMulK1(&p2.y, &z1_3);

	h.ModSub(&u2, &p1.x);
	r.ModSub(&s2, &p1.y);

	if (h.IsZero()) {
		if (r.IsZero()) return Double(p1);
		Point zero; zero.Clear(); return zero;
	}

	Int h2, h3, v, rx;
	Point res;
	h2.ModSquareK1(&h);
	h3.ModMulK1(&h2, &h);
	v.ModMulK1(&p1.x, &h2);

	rx.ModSquareK1(&r);
	rx.ModSub(&h3);
	Int v2;
	v2.ModAdd(&v, &v);
	rx.ModSub(&v2);
	res.x.Set(&rx);

	Int ry, y1h3;
	ry.ModSub(&v, &res.x);
	ry.ModMulK1(&r);
	y1h3.ModMulK1(&p1.y, &h3);
	res.y.ModSub(&ry, &y1h3);

	res.z.ModMulK1(&p1.z, &h);
	return res;
}

Point Secp256K1::Add(Point &p1, Point &p2) {
	if (p1.z.IsZero()) return p2;
	if (p2.z.IsZero()) return p1;

	Int z1_2, z2_2, u1, u2, z1_3, z2_3, s1, s2, h, r;
	z1_2.ModSquareK1(&p1.z);
	z2_2.ModSquareK1(&p2.z);

	u1.ModMulK1(&p1.x, &z2_2);
	u2.ModMulK1(&p2.x, &z1_2);

	z1_3.ModMulK1(&z1_2, &p1.z);
	z2_3.ModMulK1(&z2_2, &p2.z);

	s1.ModMulK1(&p1.y, &z2_3);
	s2.ModMulK1(&p2.y, &z1_3);

	h.ModSub(&u2, &u1);
	r.ModSub(&s2, &s1);

	if (h.IsZero()) {
		if (r.IsZero()) return Double(p1);
		Point zero; zero.Clear(); return zero;
	}

	Int h2, h3, v, rx;
	Point res;
	h2.ModSquareK1(&h);
	h3.ModMulK1(&h2, &h);
	v.ModMulK1(&u1, &h2);

	rx.ModSquareK1(&r);
	rx.ModSub(&h3);
	Int v2;
	v2.ModAdd(&v, &v);
	rx.ModSub(&v2);
	res.x.Set(&rx);

	Int ry, s1h3;
	ry.ModSub(&v, &res.x);
	ry.ModMulK1(&r);
	s1h3.ModMulK1(&s1, &h3);
	res.y.ModSub(&ry, &s1h3);

	Int z1z2;
	z1z2.ModMulK1(&p1.z, &p2.z);
	res.z.ModMulK1(&z1z2, &h);
	return res;
}

Point Secp256K1::Double(Point &p) {
	if (p.z.IsZero() || p.y.IsZero()) {
		Point zero; zero.Clear(); return zero;
	}

	Int y1_2, s, x1_2, m, rx, y1_4, ry, z1y1;
	Point res;

	y1_2.ModSquareK1(&p.y);
	s.ModMulK1(&p.x, &y1_2);
	s.ModAdd(&s);
	s.ModAdd(&s);

	x1_2.ModSquareK1(&p.x);
	m.ModAdd(&x1_2, &x1_2);
	m.ModAdd(&x1_2);

	rx.ModSquareK1(&m);
	Int s2;
	s2.ModAdd(&s, &s);
	rx.ModSub(&s2);
	res.x.Set(&rx);

	y1_4.ModSquareK1(&y1_2);
	y1_4.ModAdd(&y1_4);
	y1_4.ModAdd(&y1_4);
	y1_4.ModAdd(&y1_4);

	ry.ModSub(&s, &res.x);
	ry.ModMulK1(&m);
	res.y.ModSub(&ry, &y1_4);

	z1y1.ModMulK1(&p.y, &p.z);
	res.z.ModAdd(&z1y1, &z1y1);
	return res;
}

Point Secp256K1::ScalarMultiplication(Point &P, Int *scalar) {
	Point R, Q;
	R.Clear();
	R.z.SetInt32(0);
	Q.Set(P);

	int bits = scalar->GetBitLength();
	for (int i = 0; i < bits; i++) {
		if (scalar->GetBit(i)) {
			R = R.z.IsZero() ? Q : Add(R, Q);
		}
		Q = Double(Q);
	}
	R.Reduce();
	return R;
}

Point Secp256K1::ScalarMultiplicationGLV(Int *scalar) {
	Int beta("7AE96A2B657C07106E64479EAC3434E99CF0497512F58995C1396C28719501EE");
	Int g1("E4437ED6010E88286F547FA90ABFE4C3");
	Int g2("3086D221A7D46BCDE86C90E49284EB15");
	Int a1("3086D221A7D46BCDE86C90E49284EB15");
	Int b1_abs("E4437ED6010E88286F547FA90ABFE4C3");
	Int a2("114CA50F7A8E2F3F657C1108D9D96B397");
	Int b2("3086D221A7D46BCDE86C90E49284EB15");

	Int k1, k2, c1, c2;

	Int kg1(scalar); kg1.Mult(&g1);
	Int kg2(scalar); kg2.Mult(&g2);

	Int shift256;
	shift256.SetInt32(1);
	shift256.ShiftL(255);

	Int c1_num(&kg1); c1_num.Add(&shift256);
	c1.Set(&c1_num); c1.ShiftR(256);

	Int c2_num(&kg2); c2_num.Add(&shift256);
	c2.Set(&c2_num); c2.ShiftR(256);

	Int c1a1(&c1); c1a1.Mult(&a1);
	Int c2a2(&c2); c2a2.Mult(&a2);
	Int c1b1(&c1); c1b1.Mult(&b1_abs);
	Int c2b2(&c2); c2b2.Mult(&b2);

	Int term1(&c1a1); term1.Add(&c2a2);
	bool k1_neg = false;
	if (scalar->IsLower(&term1)) {
		k1.Set(&term1);
		k1.Sub(scalar);
		k1_neg = true;
	} else {
		k1.Set(scalar);
		k1.Sub(&term1);
	}

	bool k2_neg = false;
	if (c2b2.IsLower(&c1b1)) {
		k2.Set(&c1b1);
		k2.Sub(&c2b2);
		k2_neg = true;
	} else {
		k2.Set(&c2b2);
		k2.Sub(&c1b1);
	}

	Point P1(G);
	if (k1_neg) P1 = Negation(P1);

	Point P2;
	P2.x.ModMulK1(&G.x, &beta);
	P2.y.Set(&G.y);
	P2.z.SetInt32(1);
	if (k2_neg) P2 = Negation(P2);

	Point P1_plus_P2 = AddDirect(P1, P2);

	Point R;
	R.Clear();
	R.z.SetInt32(0);

	int bits1 = k1.GetBitLength();
	int bits2 = k2.GetBitLength();
	int max_bits = (bits1 > bits2) ? bits1 : bits2;

	for (int i = max_bits - 1; i >= 0; i--) {
		if (!R.z.IsZero()) {
			R = Double(R);
		}
		int bit1 = k1.GetBit(i);
		int bit2 = k2.GetBit(i);

		if (bit1 && bit2) {
			R = R.z.IsZero() ? P1_plus_P2 : Add(R, P1_plus_P2);
		} else if (bit1) {
			R = R.z.IsZero() ? P1 : Add(R, P1);
		} else if (bit2) {
			R = R.z.IsZero() ? P2 : Add(R, P2);
		}
	}

	R.Reduce();
	return R;
}

void Secp256K1::GetHash160(int type, bool compressed, Point &pubKey, unsigned char *hash) {
	unsigned char shapk[64];
	switch (type) {
		case P2PKH:
		case BECH32:
			unsigned char publicKeyBytes[128];
			if (!compressed) {
				publicKeyBytes[0] = 0x04;
				pubKey.x.Get32Bytes(publicKeyBytes + 1);
				pubKey.y.Get32Bytes(publicKeyBytes + 33);
				sha256(publicKeyBytes, 65, shapk);
			} else {
				publicKeyBytes[0] = pubKey.y.IsEven() ? 0x02 : 0x03;
				pubKey.x.Get32Bytes(publicKeyBytes + 1);
				sha256(publicKeyBytes, 33, shapk);
			}
			ripemd160(shapk, 32, hash);
			break;
		case P2SH:
			unsigned char script[64];
			script[0] = 0x00;
			script[1] = 0x14;
			GetHash160(P2PKH, compressed, pubKey, script + 2);
			sha256(script, 22, shapk);
			ripemd160(shapk, 32, hash);
			break;
	}
}

void Secp256K1::GetHash160(int type, bool compressed,
	Point &k0, Point &k1, Point &k2, Point &k3,
	uint8_t *h0, uint8_t *h1, uint8_t *h2, uint8_t *h3) {
	GetHash160(type, compressed, k0, h0);
	GetHash160(type, compressed, k1, h1);
	GetHash160(type, compressed, k2, h2);
	GetHash160(type, compressed, k3, h3);
}

void Secp256K1::GetHash160_fromX(int type, unsigned char prefix,
	Int *k0, Int *k1, Int *k2, Int *k3,
	uint8_t *h0, uint8_t *h1, uint8_t *h2, uint8_t *h3) {
	unsigned char digests[4][33];
	digests[0][0] = prefix; digests[1][0] = prefix;
	digests[2][0] = prefix; digests[3][0] = prefix;
	k0->Get32Bytes(digests[0] + 1);
	k1->Get32Bytes(digests[1] + 1);
	k2->Get32Bytes(digests[2] + 1);
	k3->Get32Bytes(digests[3] + 1);

	unsigned char sha[32];
	if (type == P2PKH) {
		sha256(digests[0], 33, sha); ripemd160(sha, 32, h0);
		sha256(digests[1], 33, sha); ripemd160(sha, 32, h1);
		sha256(digests[2], 33, sha); ripemd160(sha, 32, h2);
		sha256(digests[3], 33, sha); ripemd160(sha, 32, h3);
	}
}
