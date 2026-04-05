#include <stdint.h>

/* Integer power: base^exp */
int32_t math_pow(int32_t base, int32_t exp) {
    int32_t result = 1;
    while (exp-- > 0) result *= base;
    return result;
}

/* Integer square root (floor) */
int32_t math_sqrt(int32_t n) {
    if (n <= 0) return 0;
    int32_t x = n;
    int32_t y = (x + 1) / 2;
    while (y < x) {
        x = y;
        y = (x + n / x) / 2;
    }
    return x;
}

/* Absolute value */
int32_t math_abs(int32_t n) {
    return n < 0 ? -n : n;
}

/* Greatest common divisor */
int32_t math_gcd(int32_t a, int32_t b) {
    if (a < 0) a = -a;
    if (b < 0) b = -b;
    while (b != 0) {
        int32_t t = b;
        b = a % b;
        a = t;
    }
    return a;
}

/* Clamp value to [lo, hi] */
int32_t math_clamp(int32_t val, int32_t lo, int32_t hi) {
    if (val < lo) return lo;
    if (val > hi) return hi;
    return val;
}
