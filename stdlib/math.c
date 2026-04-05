#include <stdint.h>

int32_t math_pow(int32_t base, int32_t exp) {
    int32_t result = 1;
    while (exp-- > 0) result *= base;
    return result;
}

int32_t math_sqrt(int32_t n) {
    if (n <= 0) return 0;
    int32_t x = n, y = (x + 1) / 2;
    while (y < x) { x = y; y = (x + n / x) / 2; }
    return x;
}

int32_t math_abs(int32_t n)               { return n < 0 ? -n : n; }
int32_t math_min(int32_t a, int32_t b)    { return a < b ? a : b; }
int32_t math_max(int32_t a, int32_t b)    { return a > b ? a : b; }
int32_t math_clamp(int32_t v, int32_t lo, int32_t hi) {
    return v < lo ? lo : v > hi ? hi : v;
}

int32_t math_gcd(int32_t a, int32_t b) {
    if (a < 0) a = -a;
    if (b < 0) b = -b;
    while (b) { int32_t t = b; b = a % b; a = t; }
    return a;
}
