


#include "Random.h"

#if defined(_WIN64) && !defined(__CYGWIN__)
#else
#include <sys/random.h>
#endif

#ifdef __unix__
#ifdef __CYGWIN__
#else
#include <linux/random.h>
#endif
#endif

#define  RK_STATE_LEN 624


typedef struct rk_state_
{
  unsigned long key[RK_STATE_LEN];
  int pos;
} rk_state;

rk_state localState;


#define RK_MAX 0xFFFFFFFFUL

void rk_seed(unsigned long seed, rk_state *state)
{
  int pos;
  seed &= 0xffffffffUL;


  for (pos=0; pos<RK_STATE_LEN; pos++)
  {
    state->key[pos] = seed;
    seed = (1812433253UL * (seed ^ (seed >> 30)) + pos + 1) & 0xffffffffUL;
  }

  state->pos = RK_STATE_LEN;
}


#define N 624
#define M 397
#define MATRIX_A 0x9908b0dfUL
#define UPPER_MASK 0x80000000UL
#define LOWER_MASK 0x7fffffffUL

#ifdef _WIN64

#pragma warning(disable : 4146)
#endif


inline unsigned long rk_random(rk_state *state)
{
  unsigned long y;

  if (state->pos == RK_STATE_LEN)
  {
    int i;

    for (i=0;i<N-M;i++)
    {
      y = (state->key[i] & UPPER_MASK) | (state->key[i+1] & LOWER_MASK);
      state->key[i] = state->key[i+M] ^ (y>>1) ^ (-(y & 1) & MATRIX_A);
    }
    for (;i<N-1;i++)
    {
      y = (state->key[i] & UPPER_MASK) | (state->key[i+1] & LOWER_MASK);
      state->key[i] = state->key[i+(M-N)] ^ (y>>1) ^ (-(y & 1) & MATRIX_A);
    }
    y = (state->key[N-1] & UPPER_MASK) | (state->key[0] & LOWER_MASK);
    state->key[N-1] = state->key[M-1] ^ (y>>1) ^ (-(y & 1) & MATRIX_A);

    state->pos = 0;
  }

  y = state->key[state->pos++];


  y ^= (y >> 11);
  y ^= (y << 7) & 0x9d2c5680UL;
  y ^= (y << 15) & 0xefc60000UL;
  y ^= (y >> 18);

  return y;
}

inline double rk_double(rk_state *state)
{

	long a = rk_random(state) >> 5, b = rk_random(state) >> 6;
	return (a * 67108864.0 + b) / 9007199254740992.0;
}


void rseed(unsigned long seed) {
	rk_seed(seed,&localState);

}

#if defined(_WIN64) && !defined(__CYGWIN__)
unsigned long rndl() {
	return rk_random(&localState);
}
#else
unsigned long rndl() {
	unsigned long r;
	int bytes_read = getrandom(&r, sizeof(unsigned long), GRND_NONBLOCK );
	if (bytes_read > 0) {
		return r;
	}
	else	{

		return rk_random(&localState);
	}
}

#endif


double rnd() {
	return rk_double(&localState);
}
