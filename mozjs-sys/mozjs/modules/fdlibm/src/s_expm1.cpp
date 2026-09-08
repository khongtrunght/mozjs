/*
 * double expm1(double x)
 * Replaced with the LLVM-libc shared implementation, vendored under
 * llvmlibc/, so that results are bit-identical to V8 (Chrome), which
 * computes expm1 via LIBC_NAMESPACE::shared::expm1.
 * See llvmlibc/src/__support/math/expm1.h for the algorithm.
 */

#include "fdlibm.h"
#include "shared/math/expm1.h"

double
fdlibm_expm1(double x)
{
  return LIBC_NAMESPACE::shared::expm1(x);
}
