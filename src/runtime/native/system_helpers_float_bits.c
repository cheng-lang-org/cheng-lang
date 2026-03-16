#include <stdint.h>

static int64_t cheng_f64_to_bits(double value) {
  union {
    double d;
    int64_t i;
  } v;
  v.d = value;
  return v.i;
}

static double cheng_bits_to_f64(int64_t bits) {
  union {
    double d;
    int64_t i;
  } v;
  v.i = bits;
  return v.d;
}

int64_t cheng_f64_add_bits(int64_t a_bits, int64_t b_bits) {
  return cheng_f64_to_bits(cheng_bits_to_f64(a_bits) + cheng_bits_to_f64(b_bits));
}

int64_t cheng_f64_mul_bits(int64_t a_bits, int64_t b_bits) {
  return cheng_f64_to_bits(cheng_bits_to_f64(a_bits) * cheng_bits_to_f64(b_bits));
}

int64_t cheng_f64_div_bits(int64_t a_bits, int64_t b_bits) {
  return cheng_f64_to_bits(cheng_bits_to_f64(a_bits) / cheng_bits_to_f64(b_bits));
}

int64_t cheng_f64_neg_bits(int64_t a_bits) {
  return cheng_f64_to_bits(-cheng_bits_to_f64(a_bits));
}

int64_t cheng_i64_to_f64_bits(int64_t value) {
  return cheng_f64_to_bits((double)value);
}
