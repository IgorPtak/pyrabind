#include "bench_kernels.hpp"
#include "pyrabind/arrow.hpp"
#include "pyrabind/dataframe_types.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <string>
#include <variant>
#include <vector>

namespace {

using pyrabind_bench::BenchPoint;

template <typename T> inline void do_not_optimize(const T& value) noexcept {
#if defined(__clang__) || defined(__GNUC__)
    asm volatile("" : : "r,m"(value) : "memory");
#else
    volatile auto copy = value;
    (void)copy;
#endif
}

inline void clobber_memory() noexcept {
#if defined(__clang__) || defined(__GNUC__)
    asm volatile("" : : : "memory");
#endif
}

#if defined(__clang__) || defined(__GNUC__)
#define BENCH_NOINLINE __attribute__((noinline))
#else
#define BENCH_NOINLINE
#endif

BENCH_NOINLINE void cpp_noop() noexcept {
    pyrabind_bench::noop_kernel();
}

BENCH_NOINLINE std::int64_t cpp_add_i64(std::int64_t a, std::int64_t b) noexcept {
    return pyrabind_bench::add_i64_kernel(a, b);
}

BENCH_NOINLINE double cpp_point_dot(const BenchPoint& a, const BenchPoint& b) noexcept {
    return a.dot(b);
}

BENCH_NOINLINE double cpp_dot_100k(const double* a, const double* b, std::size_t n) noexcept {
    return pyrabind_bench::dot_contiguous_kernel(a, b, n);
}

BENCH_NOINLINE std::vector<double> cpp_scale_100k(const double* a, std::size_t n, double factor,
                                                  double offset) {
    std::vector<double> out(n);
    pyrabind_bench::scale_contiguous_kernel(a, out.data(), n, factor, offset);
    return out;
}

BENCH_NOINLINE double cpp_mat_sum_256(const double* data, std::size_t n) noexcept {
    return pyrabind_bench::sum_contiguous_kernel(data, n);
}

BENCH_NOINLINE pyrabind::data_frame cpp_df_transform(const pyrabind::data_frame& df) {
    const auto& id_vec = std::get<std::vector<std::int64_t>>(df["id"]);
    const auto& x_vec = std::get<std::vector<double>>(df["x"]);
    const auto& tag_vec = std::get<std::vector<std::string>>(df["tag"]);
    const std::size_t n = x_vec.size();
    std::vector<double> y_vec(n);
    for (std::size_t i = 0; i < n; ++i) {
        y_vec[i] = x_vec[i] * 2.0 + static_cast<double>(id_vec[i]);
    }
    pyrabind::data_frame out;
    out.add_column("id", id_vec);
    out.add_column("x", x_vec);
    out.add_column("y", std::move(y_vec));
    out.add_column("tag", tag_vec);
    return out;
}

BENCH_NOINLINE pyrabind::arrow_batch cpp_arrow_transform(const pyrabind::arrow_batch_view& rb) {
    auto sid = rb.column_span<std::int64_t>("id");
    auto sx = rb.column_span<double>("x");
    const std::size_t n = rb.nrows();
    std::vector<double> y_vec(n);
    for (std::size_t i = 0; i < n; ++i) {
        y_vec[i] = sx[i] * 2.0 + static_cast<double>(sid[i]);
    }
    return rb.insert_column(2, "y", std::move(y_vec));
}

struct BenchStats {
    std::string name;
    int inner_iters = 1;
    int trials = 1;
    double median_ns = 0.0;
    double mean_ns = 0.0;
    double min_ns = 0.0;
};

template <typename Fn> BenchStats measure(std::string name, int inner_iters, int trials, Fn&& fn) {
    for (int w = 0; w < 5; ++w) {
        for (int i = 0; i < inner_iters; ++i) {
            fn();
        }
    }

    std::vector<double> samples_ns;
    samples_ns.reserve(static_cast<std::size_t>(trials));
    for (int t = 0; t < trials; ++t) {
        clobber_memory();
        const auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < inner_iters; ++i) {
            fn();
        }
        const auto t1 = std::chrono::steady_clock::now();
        const auto elapsed_ns =
            std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
        samples_ns.push_back(static_cast<double>(elapsed_ns) / static_cast<double>(inner_iters));
    }

    std::sort(samples_ns.begin(), samples_ns.end());
    const double sum = std::accumulate(samples_ns.begin(), samples_ns.end(), 0.0);
    const std::size_t mid = samples_ns.size() / 2;
    const double median = (samples_ns.size() % 2 == 0)
                              ? 0.5 * (samples_ns[mid - 1] + samples_ns[mid])
                              : samples_ns[mid];

    BenchStats st;
    st.name = std::move(name);
    st.inner_iters = inner_iters;
    st.trials = trials;
    st.median_ns = median;
    st.mean_ns = sum / static_cast<double>(samples_ns.size());
    st.min_ns = samples_ns.front();
    return st;
}

} // namespace

int main() {
    constexpr std::size_t kVecLen = 100000;
    constexpr std::size_t kMatSide = 256;

    std::vector<double> a(kVecLen);
    std::vector<double> b(kVecLen);
    for (std::size_t i = 0; i < kVecLen; ++i) {
        a[i] = static_cast<double>(i % 1000) * 0.25;
        b[i] = static_cast<double>((i + 7) % 1000) * 0.5;
    }
    std::vector<double> mat(kMatSide * kMatSide, 1.25);

    std::vector<std::int64_t> df_id(kVecLen);
    std::vector<double> df_x(kVecLen);
    std::vector<std::string> df_tag(kVecLen);
    for (std::size_t i = 0; i < kVecLen; ++i) {
        df_id[i] = static_cast<std::int64_t>(i);
        df_x[i] = static_cast<double>(i) * 0.125;
        df_tag[i] = (i % 2 == 0) ? "even" : "odd";
    }
    pyrabind::data_frame df;
    df.add_column("id", std::move(df_id));
    df.add_column("x", std::move(df_x));
    df.add_column("tag", std::move(df_tag));
    pyrabind::arrow_batch rb_in = pyrabind::export_to_arrow(df);
    auto rb_view = rb_in.view();

    BenchPoint p1(3.0, 4.0);
    BenchPoint p2(-1.5, 2.5);
    std::int64_t arg_a = 17;
    std::int64_t arg_b = 25;

    std::vector<BenchStats> results;
    results.push_back(measure("noop", 200000, 25, [&]() {
        cpp_noop();
        clobber_memory();
    }));
    results.push_back(measure("add_i64", 200000, 25, [&]() {
        do_not_optimize(arg_a);
        do_not_optimize(arg_b);
        auto res = cpp_add_i64(arg_a, arg_b);
        do_not_optimize(res);
    }));
    results.push_back(measure("point_dot", 200000, 25, [&]() {
        do_not_optimize(p1);
        do_not_optimize(p2);
        auto res = cpp_point_dot(p1, p2);
        do_not_optimize(res);
    }));
    results.push_back(measure("dot_100k", 200, 25, [&]() {
        auto res = cpp_dot_100k(a.data(), b.data(), a.size());
        do_not_optimize(res);
    }));
    results.push_back(measure("scale_100k", 200, 25, [&]() {
        auto out = cpp_scale_100k(a.data(), a.size(), 1.5, -2.0);
        do_not_optimize(out.data());
    }));
    results.push_back(measure("mat_sum_256", 500, 25, [&]() {
        auto res = cpp_mat_sum_256(mat.data(), mat.size());
        do_not_optimize(res);
    }));
    results.push_back(measure("df_transform_100k", 10, 15, [&]() {
        auto out = cpp_df_transform(df);
        do_not_optimize(out.nrows());
    }));
    results.push_back(measure("arrow_transform_100k", 100, 19, [&]() {
        auto out = cpp_arrow_transform(rb_view);
        do_not_optimize(out.array.length);
    }));

    std::cout << std::fixed << std::setprecision(2) << "{\n";
    for (std::size_t i = 0; i < results.size(); ++i) {
        const auto& r = results[i];
        std::cout << "  \"" << r.name << "\": {"
                  << "\"median_ns\": " << r.median_ns << ", "
                  << "\"mean_ns\": " << r.mean_ns << ", "
                  << "\"min_ns\": " << r.min_ns << ", "
                  << "\"inner_iters\": " << r.inner_iters << ", "
                  << "\"trials\": " << r.trials << "}";
        if (i + 1 < results.size()) {
            std::cout << ",";
        }
        std::cout << "\n";
    }
    std::cout << "}\n";
    return 0;
}
