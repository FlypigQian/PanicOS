#ifndef CLION_PROJ_FIXED_POINT_H
#define CLION_PROJ_FIXED_POINT_H

typedef int fixed_t;

#define FRAC_BITS ((unsigned int) 14)

#define FRAC ((unsigned int) 1 << FRAC_BITS)

#define FIXED(n) ((fixed_t)((n) * FRAC))

#define FFRAC(n, m) (FDIV(FIXED(n), FIXED(m)))

#define FINT_ZERO(x) ((int) ((x) / FRAC))

#define FINT_NEAR(x) ((int) (((x) >= 0) ? (((x) + (FRAC/2)) / FRAC) : (((x) - (FRAC/2)) / FRAC)))

#define FADD(x, y) ((x) + (y))

#define FSUB(x, y) ((x) - (y))

#define FADD_INT(x, n) ((fixed_t) ((x) + FIXED(n)))

#define FSUB_INT(x, n) ((fixed_t) ((x) - FIXED(n)))

#define FMUL(x, y) ((fixed_t) ((((int64_t) (x)) * (y)) / FRAC))

#define FMUL_INT(x, n) ((fixed_t) ((x) * (n)))

#define FDIV(x, y) ((fixed_t) ((((int64_t) (x)) * FRAC) / (y)))

#define FDIV_INT(x, n) ((fixed_t) ((x) / (n)))

#endif //CLION_PROJ_FIXED_POINT_H
