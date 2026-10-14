#ifndef SECP256K1H
#define SECP256K1H
#include "Point.h"
#define P2PKH 0
#define P2SH  1
#define BECH32 2
class Secp256K1 {
public:
	Secp256K1();
	~Secp256K1();
	void Init();
	Point G;
	Int P;
	Int order;
	Point Add(Point &p1, Point &p2);
	Point Add2(Point &p1, Point &p2);
	Point AddDirect(Point &p1, Point &p2);
	Point Double(Point &p);
	Point DoubleDirect(Point &p);
	Point Negation(Point &p);
	Point NextKey(Point &key);
	Point ScalarMultiplication(Point &P, Int *scalar);
	Point GTable[256 * 32];
	bool EC(Point &p);
	Int GetY(Int x, bool isEven);
	Point ComputePublicKey(Int *privKey);
	bool ParsePublicKeyHex(char *str, Point &ret, bool &isCompressed);
	char* GetPublicKeyHex(bool compressed, Point &pubKey);
	void GetPublicKeyHex(bool compressed, Point &pubKey, char *dst);
	char* GetPublicKeyRaw(bool compressed, Point &pubKey);
	void GetPublicKeyRaw(bool compressed, Point &pubKey, char *dst);
	uint8_t GetByte(char *str, int idx);
	void GetHash160(int type, bool compressed, Point &pubKey, unsigned char *hash);
	void GetHash160(int type, bool compressed, Point &k0, Point &k1, Point &k2, Point &k3, uint8_t *h0, uint8_t *h1, uint8_t *h2, uint8_t *h3);
	void GetHash160_fromX(int type, unsigned char prefix, Int *k0, Int *k1, Int *k2, Int *k3, uint8_t *h0, uint8_t *h1, uint8_t *h2, uint8_t *h3);
};
#endif
