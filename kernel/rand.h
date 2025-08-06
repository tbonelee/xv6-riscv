#ifndef RAND_H
#define RAND_H

#include "types.h"

uint32 get_random_below(uint32 max);
void seed_rand(uint64 seed);
void init_rand(void);

#endif