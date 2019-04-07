#ifndef CLION_PROJ_FIXED_POINT_H
#define CLION_PROJ_FIXED_POINT_H

/* Basic definitions of fixed point. */
typedef int fixed_t;

#define FFRAC(n, m) (FDIV(FIXED(n), FIXED(m)))
/* 16 LSB used for fractional part. */
#define FP_SHIFT_AMOUNT 16
/* Convert a value to fixed-point value. */
#define FIXED(A) ((fixed_t)(A << FP_SHIFT_AMOUNT))
/* Add two fixed-point value. */
#define FADD(A,B) (A + B)
/* Add a fixed-point value A and an int value B. */
#define FADD_INT(A,B) (A + (B << FP_SHIFT_AMOUNT))
/* Substract two fixed-point value. */
#define FSUB(A,B) (A - B)
/* Substract an int value B from a fixed-point value A */
#define FSUB_INT(A,B) (A - (B << FP_SHIFT_AMOUNT))
/* Multiply a fixed-point value A by an int value B. */
#define FMUL_INT(A,B) (A * B)
/* Divide a fixed-point value A by an int value B. */
#define FDIV_INT(A,B) (A / B)
/* Multiply two fixed-point value. */
#define FMUL(A,B) ((fixed_t)(((int64_t) A) * B >> FP_SHIFT_AMOUNT))
/* Divide two fixed-point value. */
#define FDIV(A,B) ((fixed_t)((((int64_t) A) << FP_SHIFT_AMOUNT) / B))
/* Get integer part of a fixed-point value. */
#define FINT_ZERO(A) (A >> FP_SHIFT_AMOUNT)
/* Get rounded integer of a fixed-point value. */
#define FINT_NEAR(A) (A >= 0 ? ((A + (1 << (FP_SHIFT_AMOUNT - 1))) >> FP_SHIFT_AMOUNT) \
        : ((A - (1 << (FP_SHIFT_AMOUNT - 1))) >> FP_SHIFT_AMOUNT))

#endif //CLION_PROJ_FIXED_POINT_H
