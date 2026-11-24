#pragma once

#include "pyrabind/r/ndarray.hpp"

#include <Eigen/Core>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <type_traits>

namespace pyrabind {

using DStride = Eigen::Stride<Eigen::Dynamic, Eigen::Dynamic>;
template <typename T> using DRef = Eigen::Ref<T, 0, DStride>;
template <typename T> using DMap = Eigen::Map<T, 0, DStride>;

using DStride1 = Eigen::Stride<Eigen::Dynamic, 1>;
template <typename T> using DRef1 = Eigen::Ref<T, 0, DStride1>;
template <typename T> using DMap1 = Eigen::Map<T, 0, DStride1>;

namespace r_detail {

template <typename T> struct is_eigen_plain_impl : std::false_type {};
template <typename Scalar, int Rows, int Cols, int Options, int MaxRows, int MaxCols>
struct is_eigen_plain_impl<Eigen::Matrix<Scalar, Rows, Cols, Options, MaxRows, MaxCols>>
    : std::true_type {};
template <typename Scalar, int Rows, int Cols, int Options, int MaxRows, int MaxCols>
struct is_eigen_plain_impl<Eigen::Array<Scalar, Rows, Cols, Options, MaxRows, MaxCols>>
    : std::true_type {};

template <typename T> struct is_eigen_map_impl : std::false_type {
    using plain_t = void;
    using stride_t = Eigen::Stride<0, 0>;
    static constexpr bool is_const = false;
    static constexpr int map_options = 0;
};
template <typename Plain, int Options, typename StrideType>
struct is_eigen_map_impl<Eigen::Map<Plain, Options, StrideType>> : std::true_type {
    using plain_t = std::remove_const_t<Plain>;
    using stride_t = StrideType;
    static constexpr bool is_const = std::is_const_v<Plain>;
    static constexpr int map_options = Options;
};

template <typename T> struct is_eigen_ref_impl : std::false_type {
    using plain_t = void;
    static constexpr bool is_const = false;
    static constexpr int map_options = 0;
};
template <typename Plain, int Options, typename StrideType>
struct is_eigen_ref_impl<Eigen::Ref<Plain, Options, StrideType>> : std::true_type {
    using plain_t = std::remove_const_t<Plain>;
    static constexpr bool is_const = std::is_const_v<Plain>;
    static constexpr int map_options = Options;
};

template <typename T, typename Enable = void> struct is_eigen_dense : std::false_type {};

template <typename T>
struct is_eigen_dense<
    T, std::enable_if_t<(is_eigen_plain_impl<T>::value || is_eigen_map_impl<T>::value ||
                         is_eigen_ref_impl<T>::value) &&
                        scalar_dtype_traits<typename T::Scalar>::is_valid>> : std::true_type {};

template <typename T> inline constexpr bool is_eigen_dense_v = is_eigen_dense<T>::value;

template <typename T> struct eigen_plain_helper {
    using type = T;
};
template <typename Plain, int Opts, typename StrideT>
struct eigen_plain_helper<Eigen::Map<Plain, Opts, StrideT>> {
    using type = std::remove_const_t<Plain>;
};
template <typename Plain, int Opts, typename StrideT>
struct eigen_plain_helper<Eigen::Ref<Plain, Opts, StrideT>> {
    using type = std::remove_const_t<Plain>;
};

template <typename StrideT>
inline StrideT make_eigen_stride(Eigen::Index outer_s, Eigen::Index inner_s) noexcept {
    if constexpr (StrideT::OuterStrideAtCompileTime == 0 &&
                  StrideT::InnerStrideAtCompileTime == 0) {
        (void)outer_s;
        (void)inner_s;
        return StrideT();
    } else if constexpr (StrideT::InnerStrideAtCompileTime == 1 &&
                         StrideT::OuterStrideAtCompileTime == Eigen::Dynamic) {
        (void)inner_s;
        return StrideT(outer_s, 1);
    } else {
        return StrideT(outer_s, inner_s);
    }
}

template <typename DstScalar>
inline DstScalar read_r_scalar(const void* data, pb_dtype src_dt, std::size_t idx) noexcept {
    switch (src_dt) {
    case pb_dtype::f64:
        return static_cast<DstScalar>(static_cast<const double*>(data)[idx]);
    case pb_dtype::i32:
        return static_cast<DstScalar>(static_cast<const std::int32_t*>(data)[idx]);
    case pb_dtype::u8:
        return static_cast<DstScalar>(static_cast<const std::uint8_t*>(data)[idx]);
    default:
        return DstScalar{};
    }
}

template <typename T> struct type_caster<T, std::enable_if_t<is_eigen_dense_v<T>>> {
    using PlainT = typename eigen_plain_helper<T>::type;
    using Scalar = std::remove_const_t<typename T::Scalar>;
    static constexpr bool IsMap = is_eigen_map_impl<T>::value;
    static constexpr bool IsRef = is_eigen_ref_impl<T>::value;
    static constexpr bool IsView = IsMap || IsRef;
    static constexpr bool IsConstView =
        IsMap ? is_eigen_map_impl<T>::is_const : (IsRef ? is_eigen_ref_impl<T>::is_const : false);
    static constexpr bool RequireWritable = IsView && !IsConstView;
    static constexpr bool IsVec = bool(PlainT::IsVectorAtCompileTime);
    static constexpr bool IsRowMajor = bool(PlainT::IsRowMajor);
    static constexpr int CompileRows = PlainT::RowsAtCompileTime;
    static constexpr int CompileCols = PlainT::ColsAtCompileTime;

    std::optional<T> val;
    PlainT backing;
    std::shared_ptr<void> lease;

    bool from_r(SEXP x, bool convert) {
        val.reset();
        lease.reset();

        r_ndarray_desc desc;
        if (!r_acquire_ndarray(x, pb_dtype::any, RequireWritable, -1, nullptr, 'A', desc)) {
            return false;
        }
        if (desc.dtype != pb_dtype::f64 && desc.dtype != pb_dtype::i32) {
            return false;
        }

        const pb_dtype expected_dt = scalar_dtype_traits<Scalar>::value();
        const bool exact_dtype = (desc.dtype == expected_dt);
        if (!exact_dtype && (!convert || IsMap || RequireWritable)) {
            return false;
        }
        if constexpr (std::is_floating_point_v<Scalar>) {
            // Integers may convert to floating point in Pass 2 only.
        } else {
            if (!exact_dtype && !convert) {
                return false;
            }
        }

        Eigen::Index rows = 0;
        Eigen::Index cols = 0;
        if (desc.shape.size() == 1) {
            if (!IsVec) {
                return false;
            }
            if constexpr (CompileRows == 1 && CompileCols != 1) {
                rows = 1;
                cols = static_cast<Eigen::Index>(desc.shape[0]);
            } else {
                rows = static_cast<Eigen::Index>(desc.shape[0]);
                cols = 1;
            }
        } else if (desc.shape.size() == 2) {
            rows = static_cast<Eigen::Index>(desc.shape[0]);
            cols = static_cast<Eigen::Index>(desc.shape[1]);
            if constexpr (IsVec) {
                if (CompileRows == 1 && CompileCols != 1 && rows != 1) {
                    return false;
                }
                if (CompileCols == 1 && CompileRows != 1 && cols != 1) {
                    return false;
                }
            }
        } else {
            return false;
        }

        if (CompileRows != Eigen::Dynamic && rows != CompileRows) {
            return false;
        }
        if (CompileCols != Eigen::Dynamic && cols != CompileCols) {
            return false;
        }

        const bool layout_compat = IsVec || !IsRowMajor || rows <= 1 || cols <= 1;

        if constexpr (IsMap) {
            using MapStride = typename is_eigen_map_impl<T>::stride_t;
            constexpr int MapOpts = is_eigen_map_impl<T>::map_options;
            if (!exact_dtype || !layout_compat) {
                return false;
            }
            if constexpr (MapOpts != 0 && MapOpts != Eigen::Unaligned) {
                if ((reinterpret_cast<std::uintptr_t>(desc.data) % 16) != 0) {
                    return false;
                }
            }
            if constexpr (MapStride::InnerStrideAtCompileTime == 0 &&
                          MapStride::OuterStrideAtCompileTime == 0) {
                if (!desc.strides.empty() && desc.strides[0] != 1) {
                    return false;
                }
            }
            lease = std::move(desc.lease);
            using MapScalar = std::conditional_t<IsConstView, const Scalar, Scalar>;
            const Eigen::Index outer_s = IsVec ? (rows * cols) : (IsRowMajor ? cols : rows);
            auto stride_obj = make_eigen_stride<MapStride>(outer_s, 1);
            if constexpr (IsVec) {
                val.emplace(static_cast<MapScalar*>(desc.data), rows * cols, stride_obj);
            } else {
                val.emplace(static_cast<MapScalar*>(desc.data), rows, cols, stride_obj);
            }
            return true;
        } else if constexpr (IsRef) {
            using MapTarget = std::conditional_t<IsConstView, const PlainT, PlainT>;
            using MapScalar = std::conditional_t<IsConstView, const Scalar, Scalar>;
            if (exact_dtype && layout_compat) {
                lease = std::move(desc.lease);
                if constexpr (IsVec) {
                    Eigen::Map<MapTarget> mapped(static_cast<MapScalar*>(desc.data), rows * cols);
                    val.emplace(mapped);
                } else {
                    Eigen::Map<MapTarget> mapped(static_cast<MapScalar*>(desc.data), rows, cols);
                    val.emplace(mapped);
                }
                return true;
            }
            if constexpr (RequireWritable) {
                return false;
            } else {
                fill_plain(backing, rows, cols, desc);
                val.emplace(backing);
                return true;
            }
        } else {
            val.emplace();
            fill_plain(*val, rows, cols, desc);
            return true;
        }
    }

    T& get() noexcept { return *val; }

    static SEXP to_r(const T& mat, rv_policy, SEXP) {
        r_ndarray_export exp;
        exp.data = mat.data();
        exp.dtype = scalar_dtype_traits<Scalar>::value();
        std::int64_t shp[2]{};
        std::int64_t str[2]{};
        if constexpr (IsVec) {
            exp.ndim = 1;
            shp[0] = static_cast<std::int64_t>(mat.size());
            str[0] = static_cast<std::int64_t>(mat.innerStride());
        } else {
            exp.ndim = 2;
            shp[0] = static_cast<std::int64_t>(mat.rows());
            shp[1] = static_cast<std::int64_t>(mat.cols());
            str[0] = static_cast<std::int64_t>(mat.rowStride());
            str[1] = static_cast<std::int64_t>(mat.colStride());
        }
        exp.shape = shp;
        exp.strides = str;
        return r_export_ndarray(exp);
    }

private:
    static void fill_plain(PlainT& dst, Eigen::Index rows, Eigen::Index cols,
                           const r_ndarray_desc& desc) {
        dst.resize(rows, cols);
        const pb_dtype expected_dt = scalar_dtype_traits<Scalar>::value();
        if (desc.dtype == expected_dt && (!IsRowMajor || IsVec || rows <= 1 || cols <= 1)) {
            const std::size_t n = static_cast<std::size_t>(rows * cols);
            if (n > 0) {
                std::memcpy(dst.data(), desc.data, n * sizeof(Scalar));
            }
            return;
        }
        if (desc.shape.size() == 1) {
            const std::size_t n = static_cast<std::size_t>(desc.shape[0]);
            for (std::size_t i = 0; i < n; ++i) {
                dst(static_cast<Eigen::Index>(i)) = read_r_scalar<Scalar>(desc.data, desc.dtype, i);
            }
            return;
        }
        const std::size_t r_cnt = static_cast<std::size_t>(rows);
        const std::size_t c_cnt = static_cast<std::size_t>(cols);
        for (std::size_t c = 0; c < c_cnt; ++c) {
            for (std::size_t r = 0; r < r_cnt; ++r) {
                dst(static_cast<Eigen::Index>(r), static_cast<Eigen::Index>(c)) =
                    read_r_scalar<Scalar>(desc.data, desc.dtype, r + c * r_cnt);
            }
        }
    }
};

} // namespace r_detail

} // namespace pyrabind
