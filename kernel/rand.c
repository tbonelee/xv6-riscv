/**
 * xoshiro128++ PRNG 알고리즘 사용
 * https://prng.di.unimi.it/xoshiro128plusplus.c
 */
#include "types.h"
#include "riscv.h"

static inline uint32 rotl(const uint32 x, int k) {
	return (x << k) | (x >> (32 - k));
}

static uint32 s[4];

static uint32
next(void) {
	const uint32 result = rotl(s[0] + s[3], 7) + s[0];

	const uint32 t = s[1] << 9;

	s[2] ^= s[0];
	s[3] ^= s[1];
	s[1] ^= s[2];
	s[0] ^= s[3];

	s[2] ^= t;

	s[3] = rotl(s[3], 11);

	return result;
}

static uint64 splitmix64_state;

/**
 * https://rosettacode.org/wiki/Pseudo-random_numbers/Splitmix64
 */
static uint64
splitmix64_next(void) {
  uint64 z = (splitmix64_state += 0x9e3779b97f4a7c15ULL);
  z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
  z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
  return z ^ (z >> 31);
}

/**
 * splitmix64 알고리즘을 사용하여 초기화
 */
static void
seed_rand(uint64 seed) {
  splitmix64_state = seed;
  s[0] = (uint32)splitmix64_next();
  s[1] = (uint32)splitmix64_next();
  s[2] = (uint32)splitmix64_next();
  s[3] = (uint32)splitmix64_next();
}

/**
 * 기존 맵핑 함수의 Modulo bias를 줄이기 위해 다음을 참고하여 수정
 * https://lemire.me/blog/2016/06/27/a-fast-alternative-to-the-modulo-reduction/
 */
uint32
get_random_below(uint32 max) {
  uint32 r = next();
  return ((uint64)r * (uint64)max) >> 32;
}

// initialize random number generator
void
randinit(void) {
  seed_rand(r_time());
}