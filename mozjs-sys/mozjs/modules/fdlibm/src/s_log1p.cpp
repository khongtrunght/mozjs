/*
 * double log1p(double x)
 * Replaced with the LLVM-libc shared implementation, vendored under
 * llvmlibc/, so that results are bit-identical to V8 (Chrome), which
 * computes log1p via LIBC_NAMESPACE::shared::log1p.
 * See llvmlibc/src/__support/math/log1p.h for the algorithm.
 */

#include "fdlibm.h"
#include "shared/math/log1p.h"

double
fdlibm_log1p(double x)
{
  return LIBC_NAMESPACE::shared::log1p(x);
}
