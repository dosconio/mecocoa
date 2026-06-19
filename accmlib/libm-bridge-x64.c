#include "c/arith.h"

double floor(double val) { return dblfloor(val); }
double exp(double val) { return dblexp(val); }
double log(double val) { return dbllog(val); }
double log10(double val) { return dbllog10(val); }
double pow(double x, double y) { return dblpow_fexpo(x, y); }
double sin(double val) { return dblsin(val); }
double cos(double val) { return dblcos(val); }
double tan(double val) { return dbltan(val); }
double asin(double val) { return dblasin(val); }
double asinh(double val) { return dblasinh(val); }
double acos(double val) { return dblacos(val); }
double acosh(double val) { return dblacosh(val); }
double atan(double val) { return dblatan(val); }
double atanh(double val) { return dblatanh(val); }
double sinh(double val) { return dblsinh(val); }
double cosh(double val) { return dblcosh(val); }
double tanh(double val) { return dbltanh(val); }
