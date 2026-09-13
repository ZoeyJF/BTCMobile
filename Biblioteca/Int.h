#ifndef BIGINTH
#define BIGINTH

#include "Random.h"
#include<stdlib.h>
#include<stdint.h>
#include<gmp.h>

class Int {
public:
	mpz_t num;

    Int();
    Int(const char*);
    Int(const int32_t);
    Int(const uint32_t);
    Int(const int64_t);
    Int(const uint64_t);
    Int(const Int*);
	Int(const Int&);

	void Add(const uint64_t);
	void Add(const uint32_t);
	void Add(const Int*);
	void Add(const Int*,const Int*);
	void AddOne();
	void Sub(const uint64_t);
	void Sub(const uint32_t);
	void Sub(Int *);
	void Sub(Int *a, Int *b);
	void Mult(Int *);
	void Mult(uint64_t );
	void IMult(int64_t );
	
	void Div(Int *a,Int *mod = NULL);
	void Neg();
	void Abs();

	bool IsGreater(Int *a);
	bool IsGreaterOrEqual(Int *a);
	bool IsLowerOrEqual(Int *a);
	bool IsLower(Int *a);
	bool IsEqual(Int *a);
	bool IsZero();
	bool IsOne();
	bool IsPositive();
	bool IsNegative();
	bool IsEven();
	bool IsOdd();
	
	void SetInt64(const uint64_t value);
	void SetInt32(const uint32_t value);
	void Set(const Int* other);
	void Set(const char *str);
	void SetBase10(const char *str);
	void SetBase16(const char *str);
	
	int GetSize();
	int GetBitLength();
	uint64_t GetInt64();
	uint32_t GetInt32();
	int GetBit(uint32_t n);
	unsigned char GetByte(int n);
	void Get32Bytes(unsigned char *buff);
	void Set32Bytes(unsigned char *buff);

	char* GetBase2();
	char* GetBase10();
	char* GetBase16();
	
	void SetBit(uint32_t n);
	void ClearBit(uint32_t n);
	void ShiftL(uint32_t n);
	void Mod(Int *a);
	
	void ModInv();
	void ModAdd(Int *a);
	void ModAdd(uint32_t a);
	void ModAdd(Int *a, Int *b);
	void ModSub(Int *a);
	void ModSub(Int *a, Int *b);
	void ModSub(uint64_t a);
	void ModMul(Int *a);
	void ModMul(Int *a,Int *b);
	void ModNeg();
	void ModDouble();
	void ModSqrt();
	bool HasSqrt();
	
	void Rand(int nbit);
	void Rand(Int *min,Int *max);

	static void SetupField(Int *n);
	
	static void InitK1(Int *order);
	void ModMulK1(Int *a, Int *b);
	void ModMulK1(Int *a);
	void ModMulK1order(Int *a);
	void ModInvorder();
	
	void ModSquareK1(Int *a);
	void ModAddK1order(Int *a,Int *b);
		
	~Int();
	Int& operator=(const Int& other);
	void CLEAR();

};

#endif
