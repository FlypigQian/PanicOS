#ifndef CLION_PROJ_FIXED_POINT_H
#define CLION_PROJ_FIXED_POINT_H

typedef int fixed_t;

#define FRAC_BITS (14)

#define FRAC (1 << FRAC_BITS)

#define FIXED(n) ((fixed_t)(n * FRAC))

#define FIXED_INT_ZERO(x) ((int) (x / FRAC))

#define FIXED_INT_NEAR(x) ((int) ((x >= 0) ? ((x + (FRAC/2)) / FRAC) : ((x - (FRAC/2)) / FRAC)))

#define ADD_FIXED(x, y) (x + y)

#define SUB_FIXED(x, y) (x - y)

#define ADD_FIXED_INT(x, n) ((fixed_t) (x + FIXED(n)))

#define SUB_FIXED_INT(x, n) ((fixed_t) (x - FIXED(n)))

#define MUL_FIXED(x, y) ((((int64_t) x) * y) / FRAC)

#define MUL_FIXED_INT(x, n) ((fixed_t) (x * n))

#define DIV_FIXED(x, y) ((((int64_t) x) * FRAC) / y)

#define DIV_FIXED_INT(x, n) ((fixed_t) (x / n))

#endif //CLION_PROJ_FIXED_POINT_H
