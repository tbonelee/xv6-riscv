#include "types.h"

static uint32 seed = 1;

static const uint64 MODULUS = 2147483648;
static const uint64 MULTIPLIER = 1103515245;
static const uint64 INCREMENT = 12345;

// Pseudo-Random Number Generator
static int
rand() {
  seed = (MULTIPLIER * seed + INCREMENT) % MODULUS;
  return seed;
}

// Get a random number between start and end
// start is inclusive, end is exclusive
int
getrandom(int start, int end) {
  return start + rand() % (end - start);
}