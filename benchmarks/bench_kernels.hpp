#pragma once

#include <cstddef>
#include <cstdint>

namespace pyrabind_bench {

struct BenchPoint {
    double x = 0.0;
    double y = 0.0;

    BenchPoint() = default;
    BenchPoint(double x_val, double y_val) noexcept : x(x_val), y(y_val) {}

    double dot(const BenchPoint& other) const noexcept { return x * other.x + y * other.y; }
};

inline void noop_kernel() noexcept {}

inline std::int64_t add_i64_kernel(std::int64_t a, std::int64_t b) noexcept {
    return a + b;
}

inline double dot_contiguous_kernel(const double* a, const double* b, std::size_t n) noexcept {
    double acc = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        acc += a[i] * b[i];
    }
    return acc;
}

inline void scale_contiguous_kernel(const double* in, double* out, std::size_t n, double factor,
                                    double offset) noexcept {
    for (std::size_t i = 0; i < n; ++i) {
        out[i] = in[i] * factor + offset;
    }
}

inline double sum_contiguous_kernel(const double* data, std::size_t n) noexcept {
    double acc = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        acc += data[i];
    }
    return acc;
}

} // namespace pyrabind_bench
