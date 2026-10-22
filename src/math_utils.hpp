#pragma once
#include <cmath> 

namespace qa::math{
 // Constants are precomputed, so no sqrt runs at call time and we do not rely on M_PI
// (M_PI is a POSIX extension, not standard C++).   
inline constexpr double kInvSqrt2   = 0.70710678118654752440;  // 1 / sqrt(2)
inline constexpr double kInvSqrt2Pi = 0.39894228040143267794;// 1 / sqrt(2*pi)

[[nodiscard]] inline double norm_cdf(double x) noexcept{
    return 0.5*std::erfc(-x*kInvSqrt2);
}

[[nodiscard]] inline double norm_pdf(double x) noexcept{
    return kInvSqrt2Pi * std::exp(-0.5 * x * x);
}
}