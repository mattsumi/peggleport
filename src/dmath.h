#pragma once
#include <cmath>

namespace pp::dm {

inline double kSin(double x) {
  const double S1 = -1.66666666666666324348e-01, S2 = 8.33333333332248946124e-03, S3 = -1.98412698298579493134e-04,
               S4 = 2.75573137070700676789e-06, S5 = -2.50507602534068634195e-08, S6 = 1.58969099521155010221e-10;
  double z = x * x, v = z * x;
  double r = S2 + z * (S3 + z * (S4 + z * (S5 + z * S6)));
  return x + v * (S1 + z * r);
}

inline double kCos(double x) {
  const double C1 = 4.16666666666666019037e-02, C2 = -1.38888888888741095749e-03, C3 = 2.48015872894767294178e-05,
               C4 = -2.75573143513906633035e-07, C5 = 2.08757232129817482790e-09, C6 = -1.13596475577881948265e-11;
  double z = x * x;
  double r = z * (C1 + z * (C2 + z * (C3 + z * (C4 + z * (C5 + z * C6)))));
  return 1.0 - (0.5 * z - z * r);
}

inline void sincos(double x, double& s, double& c) {
  const double invpio2 = 6.36619772367581382433e-01;
  const double p1 = 1.57079632673412561417e+00, p2 = 6.07710050650619224932e-11, p3 = 2.02226624871116645580e-21;
  double n = std::floor(x * invpio2 + 0.5);
  double r = ((x - n * p1) - n * p2) - n * p3;
  long long q = n >= -9223372036854775808.0 && n < 9223372036854775808.0 ? ((long long)n) & 3 : 0;
  double ks = kSin(r), kc = kCos(r);
  switch (q) {
    case 0: s = ks; c = kc; break;
    case 1: s = kc; c = -ks; break;
    case 2: s = -ks; c = -kc; break;
    default: s = -kc; c = ks; break;
  }
}
inline double sin(double x) { double s, c; sincos(x, s, c); return s; }
inline double cos(double x) { double s, c; sincos(x, s, c); return c; }

inline double atanPos(double x) {
  bool inv = x > 1.0;
  if (inv) x = 1.0 / x;
  x = x / (1.0 + std::sqrt(1.0 + x * x));
  x = x / (1.0 + std::sqrt(1.0 + x * x));
  double z = x * x;
  double r = x * (1.0 + z * (-1.0 / 3 + z * (1.0 / 5 + z * (-1.0 / 7 + z * (1.0 / 9 + z * (-1.0 / 11 + z * (1.0 / 13 +
             z * (-1.0 / 15 + z * (1.0 / 17 + z * (-1.0 / 19 + z * (1.0 / 21 + z * (-1.0 / 23))))))))))));
  r *= 4.0;
  return inv ? 1.57079632679489661923 - r : r;
}
inline double atan2(double y, double x) {
  const double PI = 3.14159265358979323846;
  if (x == 0 && y == 0) return 0;
  if (x == 0) return y > 0 ? PI / 2 : -PI / 2;
  double a = atanPos(std::fabs(y / x));
  if (x < 0) a = PI - a;
  return y < 0 ? -a : a;
}

}
