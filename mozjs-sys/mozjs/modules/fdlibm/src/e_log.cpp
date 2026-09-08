/*
 * double log(double x)
 * Replaced with the LLVM-libc shared implementation, vendored under
 * llvmlibc/, so that results are bit-identical to V8 (Chrome), which
 * computes log via LIBC_NAMESPACE::shared::log.
 * See llvmlibc/src/__support/math/log.h for the algorithm.
 */

#include "fdlibm.h"
#include "shared/math/log.h"

double
fdlibm_log(double x)
{
  return LIBC_NAMESPACE::shared::log(x);
}
