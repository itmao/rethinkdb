
#include <time.h>
#include <stdlib.h>
#include <gsl/gsl_randist.h>
#include "utils.hpp"

/* Really fast random function */
static unsigned long x=123456789, y=362436069, z=521288629;

unsigned long xorshf96(void) {          //period 2^96-1
unsigned long t;
    x ^= x << 16;
    x ^= x >> 5;
    x ^= x << 1;

   t = x;
   x = y;
   y = z;
   z = t ^ x ^ y;

  return z;
}

static unsigned long saltx=123456789, salty=362436069, saltz=521288629;
unsigned long seeded_xorshf96(unsigned long seed) {
    unsigned long X = saltx ^ seed << 2, Y = salty ^ seed << 13, Z = saltz ^ seed << 5;

    X ^= X << 17;
    X ^= Y >> 13;
    X ^= Z << 43;

    X ^= X >> 5;
    X ^= X >> 16;
    X ^= X >> 44;

    return X;
}

/* Returns random number between [min, max] */
size_t random(size_t min, size_t max) {
    //return rand() % (max - min + 1) + min;
    return xorshf96() % (max - min + 1) + min;
}

size_t seeded_random(size_t min, size_t max, unsigned long seed) {
    return seeded_xorshf96(seed) % (max - min + 1) + min;
}

/* Returns random number between [min, max] using various distributions */
rnd_gen_t xrandom_create(rnd_distr_t rnd_distr, int mu) {
    rnd_gen_t rnd;
    rnd.rnd_distr = rnd_distr;
    rnd.gsl_rnd = gsl_rng_alloc(gsl_rng_mt19937);
    gsl_rng_set((gsl_rng *)rnd.gsl_rnd, get_ticks());
    rnd.mu = mu;
    return rnd;
}

size_t xrandom(size_t min, size_t max) {
    rnd_gen_t rnd;
    rnd.rnd_distr = rnd_uniform_t;
    xrandom(rnd, min, max);
}

size_t xrandom(rnd_gen_t rnd, size_t min, size_t max) {
    double tmp;
    int len = (min + max) / 2;
    
    switch(rnd.rnd_distr) {
    case rnd_uniform_t:
        tmp = random(min, max);
        break;
    case rnd_normal_t:
        // Here one percent of the database is within the standard deviation
        tmp = gsl_ran_gaussian((gsl_rng*)rnd.gsl_rnd,  (double)len * (double)rnd.mu / 100.0) + (len / 2);
        break;
    };
    
    if(tmp < min)
        tmp = min;
    if(tmp > max)
        tmp = max;
    //printf("%lu\n", (size_t)tmp);
    return tmp;
}

size_t seeded_xrandom(size_t min, size_t max, unsigned long seed) {
    rnd_gen_t rnd;
    rnd.rnd_distr = rnd_uniform_t;
    seeded_xrandom(rnd, min, max, seed);
}

size_t seeded_xrandom(rnd_gen_t rnd, size_t min, size_t max, unsigned long seed) {
    double tmp;
    int len = (min + max) / 2;
    
    switch(rnd.rnd_distr) {
    case rnd_uniform_t:
        tmp = seeded_random(min, max, seed);
        break;
    case rnd_normal_t:
        gsl_rng_set((gsl_rng *)rnd.gsl_rnd, seed);
        tmp = gsl_ran_gaussian((gsl_rng*)rnd.gsl_rnd,  (double)len / 4) + (len / 2);
        break;
    };
    
    if(tmp < min)
        tmp = min;
    if(tmp > max)
        tmp = max;
    return tmp;
}

