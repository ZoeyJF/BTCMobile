



#ifndef BIGINTH
#define BIGINTH

#include "Random.h"
#include <stdlib.h>
#include <inttypes.h>


#define BISIZE 256

#if BISIZE==256
  #define NB64BLOCK 5
  #define NB32BLOCK 10
#elif BISIZE==512
  #define NB64BLOCK 9
  #define NB32BLOCK 18
#else
  #error Unsuported size
#endif

class Int {

public:

  Int();
  Int(int32_t i32);
  Int(int64_t i64);
  Int(uint64_t u64);
  Int(Int *a);


  void Add(uint64_t a);
  void Add(Int *a);
  void Add(Int *a,Int *b);
  void AddOne();
  void Sub(uint64_t a);
  void Sub(Int *a);
  void Sub(Int *a, Int *b);
  void SubOne();
  void Mult(Int *a);
  void Mult(uint64_t a);
  void IMult(int64_t a);
  void Mult(Int *a,uint64_t b);
  void IMult(Int *a, int64_t b);
  void Mult(Int *a,Int *b);
  void Div(Int *a,Int *mod = NULL);
  void MultModN(Int *a, Int *b, Int *n);
  void Neg();
  void Abs();


  void ShiftR(uint32_t n);
  void ShiftR32Bit();
  void ShiftR64Bit();

  void ShiftL(uint32_t n);
  void ShiftL32Bit();
  void ShiftL64Bit();


  bool IsGreater(Int *a);
  bool IsGreaterOrEqual(Int *a);
  bool IsLowerOrEqual(Int *a);
  bool IsLower(Int *a);
  bool IsEqual(Int *a);
  bool IsZero();
  bool IsOne();
  bool IsStrictPositive();
  bool IsPositive();
  bool IsNegative();
  bool IsEven();
  bool IsOdd();







  static void SetupField(Int *n, Int *R = NULL, Int *R2 = NULL, Int *R3 = NULL, Int *R4 = NULL);
  static Int *GetR();
  static Int *GetR2();
  static Int *GetR3();
  static Int *GetR4();
  static Int* GetFieldCharacteristic();

  void GCD(Int *a);
  void Mod(Int *n);
  void ModInv();
  void MontgomeryMult(Int *a,Int *b);
  void MontgomeryMult(Int *a);
  void ModAdd(Int *a);
  void ModAdd(Int *a,Int *b);
  void ModAdd(uint64_t a);
  void ModSub(Int *a);
  void ModSub(Int *a, Int *b);
  void ModSub(uint64_t a);
  void ModMul(Int *a,Int *b);
  void ModMul(Int *a);
  void ModSquare(Int *a);
  void ModCube(Int *a);
  void ModDouble();
  void ModExp(Int *e);
  void ModNeg();
  void ModSqrt();
  bool HasSqrt();


  static void InitK1(Int *order);
  void ModMulK1(Int *a, Int *b);
  void ModMulK1(Int *a);
  void ModMulK1order(Int *a);
  void ModSquareK1(Int *a);
  void ModAddK1order(Int *a,Int *b);


  int GetSize();
  int GetBitLength();


  void SetInt64(uint64_t value);
  void SetInt32(uint32_t value);
  void Set(Int *a);
  void SetBase10(const char *value);
  void SetBase16(const char *value);
  void SetBaseN(int n,const char *charset,const char *value);
  void SetByte(int n,unsigned char byte);
  void SetDWord(int n, uint32_t b);
  void SetQWord(int n,uint64_t b);
  void Rand(int nbit);
  void Rand(Int *min,Int *max);
  void Set32Bytes(unsigned char *bytes);
  void MaskByte(int n);


  uint64_t GetInt64();
  uint32_t GetInt32();
  int GetBit(uint32_t n);
  unsigned char GetByte(int n);
  void Get32Bytes(unsigned char *buff);

  char* GetBase2();
  char* GetBase10();
  char* GetBase16();
  char* GetBaseN(int n,const char *charset);
  char* GetBlockStr();
  char* GetC64Str(int nbDigit);





  union {
    uint32_t bits[NB32BLOCK];
    uint64_t bits64[NB64BLOCK];
  };

private:

  void ShiftL32BitAndSub(Int *a,int n);
  uint64_t AddC(Int *a);
  void AddAndShift(Int *a, Int *b,uint64_t cH);
  void Mult(Int *a, uint32_t b);
  int  GetLowestBit();
  void CLEAR();
  void CLEARFF();


};



#ifndef _WIN64

static inline uint64_t _umul128(uint64_t a, uint64_t b, uint64_t *h) {
  unsigned __int128 res = (unsigned __int128)a * b;
  *h = (uint64_t)(res >> 64);
  return (uint64_t)res;
}

static inline uint64_t __shiftright128(uint64_t a, uint64_t b, unsigned char n) {
  if (n == 0) return a;
  if (n >= 64) return b >> (n - 64);
  return (a >> n) | (b << (64 - n));
}

static inline uint64_t __shiftleft128(uint64_t a, uint64_t b, unsigned char n) {
  if (n == 0) return b;
  if (n >= 64) return a << (n - 64);
  return (b << n) | (a >> (64 - n));
}

static inline unsigned char _subborrow_u64(unsigned char c, uint64_t a, uint64_t b, uint64_t *d) {
  unsigned __int128 res = (unsigned __int128)a - b - c;
  *d = (uint64_t)res;
  return (unsigned char)((res >> 127) & 1);
}

static inline unsigned char _addcarry_u64(unsigned char c, uint64_t a, uint64_t b, uint64_t *d) {
  unsigned __int128 res = (unsigned __int128)a + b + c;
  *d = (uint64_t)res;
  return (unsigned char)(res >> 64);
}

#define _byteswap_uint64 __builtin_bswap64
#else
#include <intrin.h>
#endif

static void inline imm_mul(uint64_t *x, uint64_t y, uint64_t *dst) {

  unsigned char c = 0;
  uint64_t h, carry;
  dst[0] = _umul128(x[0], y, &h); carry = h;
  c = _addcarry_u64(c, _umul128(x[1], y, &h), carry, dst + 1); carry = h;
  c = _addcarry_u64(c, _umul128(x[2], y, &h), carry, dst + 2); carry = h;
  c = _addcarry_u64(c, _umul128(x[3], y, &h), carry, dst + 3); carry = h;
  c = _addcarry_u64(c, _umul128(x[4], y, &h), carry, dst + 4); carry = h;
#if NB64BLOCK > 5
  c = _addcarry_u64(c, _umul128(x[5], y, &h), carry, dst + 5); carry = h;
  c = _addcarry_u64(c, _umul128(x[6], y, &h), carry, dst + 6); carry = h;
  c = _addcarry_u64(c, _umul128(x[7], y, &h), carry, dst + 7); carry = h;
  c = _addcarry_u64(c, _umul128(x[8], y, &h), carry, dst + 8); carry = h;
#endif

}

static void inline imm_umul(uint64_t *x, uint64_t y, uint64_t *dst) {


  unsigned char c = 0;
  uint64_t h, carry;
  dst[0] = _umul128(x[0], y, &h); carry = h;
  c = _addcarry_u64(c, _umul128(x[1], y, &h), carry, dst + 1); carry = h;
  c = _addcarry_u64(c, _umul128(x[2], y, &h), carry, dst + 2); carry = h;
  c = _addcarry_u64(c, _umul128(x[3], y, &h), carry, dst + 3); carry = h;
#if NB64BLOCK > 5
  c = _addcarry_u64(c, _umul128(x[4], y, &h), carry, dst + 4); carry = h;
  c = _addcarry_u64(c, _umul128(x[5], y, &h), carry, dst + 5); carry = h;
  c = _addcarry_u64(c, _umul128(x[6], y, &h), carry, dst + 6); carry = h;
  c = _addcarry_u64(c, _umul128(x[7], y, &h), carry, dst + 7); carry = h;
#endif
  _addcarry_u64(c, 0ULL, carry, dst + (NB64BLOCK - 1));

}

static void inline shiftR(unsigned char n, uint64_t *d) {

  d[0] = __shiftright128(d[0], d[1], n);
  d[1] = __shiftright128(d[1], d[2], n);
  d[2] = __shiftright128(d[2], d[3], n);
  d[3] = __shiftright128(d[3], d[4], n);
#if NB64BLOCK > 5
  d[4] = __shiftright128(d[4], d[5], n);
  d[5] = __shiftright128(d[5], d[6], n);
  d[6] = __shiftright128(d[6], d[7], n);
  d[7] = __shiftright128(d[7], d[8], n);
#endif
  d[NB64BLOCK-1] = ((int64_t)d[NB64BLOCK-1]) >> n;

}

static void inline shiftL(unsigned char n, uint64_t *d) {

#if NB64BLOCK > 5
  d[8] = __shiftleft128(d[7], d[8], n);
  d[7] = __shiftleft128(d[6], d[7], n);
  d[6] = __shiftleft128(d[5], d[6], n);
  d[5] = __shiftleft128(d[4], d[5], n);
#endif
  d[4] = __shiftleft128(d[3], d[4], n);
  d[3] = __shiftleft128(d[2], d[3], n);
  d[2] = __shiftleft128(d[1], d[2], n);
  d[1] = __shiftleft128(d[0], d[1], n);
  d[0] = d[0] << n;

}

#endif
