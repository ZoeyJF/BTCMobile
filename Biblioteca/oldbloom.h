#ifndef _OLDBLOOM_H
#define _OLDBLOOM_H

#if defined(_WIN64) && !defined(__CYGWIN__)
#include <windows.h>
#else
#endif
#ifdef __cplusplus
extern "C" {
#endif


struct oldbloom
{
  uint64_t entries;
  uint64_t bits;
  uint64_t bytes;
  uint8_t hashes;
  long double error;

  uint8_t ready;
  uint8_t major;
  uint8_t minor;
  double bpe;
  uint8_t checksum[32];
  uint8_t checksum_backup[32];
  uint8_t *bf;
#if defined(_WIN64) && !defined(__CYGWIN__)
  HANDLE mutex;
#else
  pthread_mutex_t mutex;
#endif
};


int oldbloom_init2(struct oldbloom * bloom, uint64_t entries, long double error);


int oldbloom_init(struct oldbloom * bloom, uint64_t entries, long double error);


int oldbloom_check(struct oldbloom * bloom, const void * buffer, int len);


int oldbloom_add(struct oldbloom * bloom, const void * buffer, int len);


void oldbloom_print(struct oldbloom * bloom);


void oldbloom_free(struct oldbloom * bloom);


int oldbloom_reset(struct oldbloom * bloom);


const char * oldbloom_version();

#ifdef __cplusplus
}
#endif

#endif
