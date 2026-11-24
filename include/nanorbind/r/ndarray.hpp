#pragma once

#include "nanorbind/r/caster.hpp"
#include "nanorbind/r/traits.hpp"
#include "nanorbind/r/types.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

namespace nanorbind {

struct c_contig {
    static constexpr char value = 'C';
    static constexpr bool is_order = true;
};

struct f_contig {
    static constexpr char value = 'F';
    static constexpr bool is_order = true;
};

struct any_contig {
    static constexpr char value = 'A';
    static constexpr bool is_order = true;
};

struct numpy {
    static constexpr bool is_framework = true;
};

struct pytorch {
    static constexpr bool is_framework = true;
};

struct no_framework {
    static constexpr bool is_framework = true;
};

struct ro {};

namespace device {
struct cpu {
    static constexpr int value = 1;
    static constexpr bool is_device_type = true;
};
} // namespace device

namespace r_detail {

template <std::ptrdiff_t... Is> struct shape {
    static_assert(((Is >= 0 || Is == -1) && ...), "ndarray::shape dimensions must be >= 0 or -1");

    static void put(std::int64_t* out) noexcept {
        std::size_t idx = 0;
        ((out[idx++] = static_cast<std::int64_t>(Is)), ...);
    }

    static void put(std::size_t* out) noexcept {
        std::size_t idx = 0;
        ((out[idx++] = static_cast<std::size_t>(Is < 0 ? 0 : Is)), ...);
    }
};

template <typename> struct ndim_shape;
template <std::size_t... Is> struct ndim_shape<std::index_sequence<Is...>> {
    using type = shape<((void)Is, static_cast<std::ptrdiff_t>(-1))...>;
};

enum class nr_dtype : std::uint8_t { f64 = 0, f32, i32, i64, u8, bool8, any };

template <typename T> struct scalar_dtype_traits {
    using CleanT = std::remove_cv_t<T>;
    static constexpr bool is_valid =
        std::is_same_v<CleanT, double> || std::is_same_v<CleanT, float> ||
        std::is_same_v<CleanT, std::int32_t> || std::is_same_v<CleanT, std::int64_t> ||
        std::is_same_v<CleanT, std::uint8_t> || std::is_same_v<CleanT, bool>;

    static constexpr nr_dtype value() noexcept {
        if constexpr (std::is_same_v<CleanT, double>) {
            return nr_dtype::f64;
        } else if constexpr (std::is_same_v<CleanT, float>) {
            return nr_dtype::f32;
        } else if constexpr (std::is_same_v<CleanT, std::int32_t>) {
            return nr_dtype::i32;
        } else if constexpr (std::is_same_v<CleanT, std::int64_t>) {
            return nr_dtype::i64;
        } else if constexpr (std::is_same_v<CleanT, std::uint8_t>) {
            return nr_dtype::u8;
        } else if constexpr (std::is_same_v<CleanT, bool>) {
            return nr_dtype::bool8;
        } else {
            return nr_dtype::any;
        }
    }
};

struct r_ndarray_desc {
    void* data = nullptr;
    std::vector<std::int64_t> shape;
    std::vector<std::int64_t> strides;
    nr_dtype dtype = nr_dtype::any;
    std::shared_ptr<void> lease;
};

struct r_ndarray_export {
    const void* data = nullptr;
    nr_dtype dtype = nr_dtype::f64;
    const std::int64_t* shape = nullptr;
    const std::int64_t* strides = nullptr;
    std::size_t ndim = 0;
    char order = 'C';
};

bool r_acquire_ndarray(SEXP x, nr_dtype req_dtype, bool require_writable, std::int32_t req_ndim,
                       const std::int64_t* req_shape, char req_order, r_ndarray_desc& out);
SEXP r_export_ndarray(const r_ndarray_export& info);

struct unused_tag {
    using type = void;
    static constexpr char value = '\0';
};

template <typename Enable = int, typename... Args> struct ndarray_config_t;

template <> struct ndarray_config_t<int> {
    using Scalar = void;
    using Shape = unused_tag;
    using Order = unused_tag;
    static constexpr std::int32_t N = -1;
};

template <typename... Args>
struct ndarray_config_t<int, ro, Args...> : ndarray_config_t<int, Args...> {
    using Scalar = std::add_const_t<typename ndarray_config_t<int, Args...>::Scalar>;
};

template <typename Arg, typename... Args>
struct ndarray_config_t<std::enable_if_t<scalar_dtype_traits<Arg>::is_valid, int>, Arg, Args...>
    : ndarray_config_t<int, Args...> {
    using Scalar =
        std::conditional_t<std::is_const_v<typename ndarray_config_t<int, Args...>::Scalar>,
                           std::add_const_t<Arg>, Arg>;
};

template <typename Arg, typename... Args>
struct ndarray_config_t<std::enable_if_t<Arg::is_device_type, int>, Arg, Args...>
    : ndarray_config_t<int, Args...> {};

template <typename Arg, typename... Args>
struct ndarray_config_t<std::enable_if_t<Arg::is_framework, int>, Arg, Args...>
    : ndarray_config_t<int, Args...> {};

template <typename Arg, typename... Args>
struct ndarray_config_t<std::enable_if_t<Arg::is_order, int>, Arg, Args...>
    : ndarray_config_t<int, Args...> {
    using Order = Arg;
};

template <std::ptrdiff_t... Is, typename... Args>
struct ndarray_config_t<int, shape<Is...>, Args...> : ndarray_config_t<int, Args...> {
    using Shape = shape<Is...>;
    static constexpr std::int32_t N = static_cast<std::int32_t>(sizeof...(Is));
};

template <typename Scalar, std::size_t Dim, char Order> struct ndarray_view {
    ndarray_view() = default;

    ndarray_view(Scalar* ptr, const std::int64_t* shp, const std::int64_t* str) noexcept
        : data_(ptr) {
        for (std::size_t i = 0; i < Dim; ++i) {
            shape_[i] = shp[i];
            strides_[i] = str[i];
        }
    }

    template <typename... Indices> Scalar& operator()(Indices... indices) const noexcept {
        static_assert(sizeof...(Indices) == Dim, "invalid number of indices for ndarray_view");
        const std::int64_t idx[] = {static_cast<std::int64_t>(indices)...};
        std::int64_t offset = 0;
        for (std::size_t i = 0; i < Dim; ++i) {
            offset += idx[i] * strides_[i];
        }
        return data_[offset];
    }

    std::size_t ndim() const noexcept { return Dim; }
    std::size_t shape(std::size_t i) const noexcept { return static_cast<std::size_t>(shape_[i]); }
    std::int64_t stride(std::size_t i) const noexcept { return strides_[i]; }
    Scalar* data() const noexcept { return data_; }

private:
    Scalar* data_ = nullptr;
    std::int64_t shape_[Dim]{};
    std::int64_t strides_[Dim]{};
};

} // namespace r_detail

using r_detail::shape;

template <std::size_t N>
using ndim = typename r_detail::ndim_shape<std::make_index_sequence<N>>::type;

template <typename... Args> class ndarray {
public:
    using Config = r_detail::ndarray_config_t<int, Args...>;
    using Scalar = typename Config::Scalar;
    using CleanScalar = std::remove_const_t<Scalar>;
    static constexpr bool ReadOnly = std::is_const_v<Scalar>;
    static constexpr char Order = Config::Order::value;
    using VoidPtr = std::conditional_t<ReadOnly, const void*, void*>;
    using DataPtr = std::conditional_t<std::is_void_v<Scalar>, void*, Scalar*>;

    ndarray() = default;

    ndarray(VoidPtr ptr, std::size_t ndim_val, const std::size_t* shp, capsule owner = {},
            const std::int64_t* str = nullptr)
        : data_(const_cast<void*>(ptr)), owner_(owner.owner()) {
        shape_.resize(ndim_val);
        for (std::size_t i = 0; i < ndim_val; ++i) {
            shape_[i] = static_cast<std::int64_t>(shp[i]);
        }
        init_strides(ndim_val, str);
        dtype_ = r_detail::scalar_dtype_traits<CleanScalar>::value();
    }

    ndarray(VoidPtr ptr, std::initializer_list<std::size_t> shp, capsule owner = {},
            std::initializer_list<std::int64_t> str = {})
        : data_(const_cast<void*>(ptr)), owner_(owner.owner()) {
        shape_.reserve(shp.size());
        for (std::size_t d : shp) {
            shape_.push_back(static_cast<std::int64_t>(d));
        }
        init_strides(shape_.size(), str.size() == 0 ? nullptr : str.begin());
        dtype_ = r_detail::scalar_dtype_traits<CleanScalar>::value();
    }

    explicit ndarray(r_detail::r_ndarray_desc desc) noexcept
        : data_(desc.data), shape_(std::move(desc.shape)), strides_(std::move(desc.strides)),
          owner_(std::move(desc.lease)), dtype_(desc.dtype) {}

    bool is_valid() const noexcept { return data_ != nullptr || !shape_.empty(); }
    std::size_t ndim() const noexcept { return shape_.size(); }
    std::size_t shape(std::size_t i) const noexcept { return static_cast<std::size_t>(shape_[i]); }
    std::int64_t stride(std::size_t i) const noexcept { return strides_[i]; }
    const std::int64_t* shape_ptr() const noexcept { return shape_.data(); }
    const std::int64_t* stride_ptr() const noexcept { return strides_.data(); }
    r_detail::nr_dtype dtype_code() const noexcept { return dtype_; }

    std::size_t size() const noexcept {
        if (shape_.empty()) {
            return 0;
        }
        std::size_t n = 1;
        for (std::int64_t d : shape_) {
            n *= static_cast<std::size_t>(d);
        }
        return n;
    }

    std::size_t nbytes() const noexcept {
        if constexpr (!std::is_void_v<Scalar>) {
            return size() * sizeof(CleanScalar);
        } else {
            return size() * 8;
        }
    }

    DataPtr data() const noexcept { return static_cast<DataPtr>(data_); }

    template <typename... Indices> auto& operator()(Indices... indices) const noexcept {
        static_assert(!std::is_void_v<Scalar>, "ndarray::operator() requires a Scalar type");
        static_assert(Config::N < 0 || sizeof...(Indices) == static_cast<std::size_t>(Config::N),
                      "ndarray::operator() index count mismatch");
        const std::int64_t idx[] = {static_cast<std::int64_t>(indices)...};
        std::int64_t offset = 0;
        for (std::size_t i = 0; i < sizeof...(Indices); ++i) {
            offset += idx[i] * strides_[i];
        }
        return data()[offset];
    }

    template <typename... Args2> auto view() const noexcept {
        using Config2 = r_detail::ndarray_config_t<int, Args2..., Args...>;
        using Scalar2 = typename Config2::Scalar;
        constexpr std::size_t N = Config2::N >= 0 ? static_cast<std::size_t>(Config2::N) : 1;
        static_assert(!std::is_void_v<Scalar2>, "ndarray::view requires a Scalar type");
        static_assert(Config2::N >= 0, "ndarray::view requires ndim<> or shape<>");
        return r_detail::ndarray_view<Scalar2, N, Config2::Order::value>(
            static_cast<Scalar2*>(data_), shape_.data(), strides_.data());
    }

private:
    void init_strides(std::size_t n, const std::int64_t* custom) {
        strides_.resize(n);
        if (custom != nullptr) {
            for (std::size_t i = 0; i < n; ++i) {
                strides_[i] = custom[i];
            }
            return;
        }
        if (n == 0) {
            return;
        }
        if constexpr (Order == 'F') {
            strides_[0] = 1;
            for (std::size_t i = 1; i < n; ++i) {
                strides_[i] = strides_[i - 1] * shape_[i - 1];
            }
        } else {
            strides_[n - 1] = 1;
            for (std::size_t i = n - 1; i > 0; --i) {
                strides_[i - 1] = strides_[i] * shape_[i];
            }
        }
    }

    void* data_ = nullptr;
    std::vector<std::int64_t> shape_;
    std::vector<std::int64_t> strides_;
    std::shared_ptr<void> owner_;
    r_detail::nr_dtype dtype_ = r_detail::nr_dtype::any;
};

namespace r_detail {

template <typename... Args> struct is_custom_value_type<ndarray<Args...>> : std::true_type {};

template <typename... Args> struct type_caster<ndarray<Args...>> {
    using ArrT = ndarray<Args...>;
    using Config = typename ArrT::Config;
    using Scalar = typename ArrT::Scalar;

    ArrT val;

    bool from_r(SEXP x, bool) {
        std::int64_t req_shape_buf[Config::N > 0 ? Config::N : 1]{};
        const std::int64_t* req_shape_ptr = nullptr;
        if constexpr (Config::N > 0) {
            Config::Shape::put(req_shape_buf);
            req_shape_ptr = req_shape_buf;
        }

        r_ndarray_desc desc;
        if (!r_acquire_ndarray(x, scalar_dtype_traits<std::remove_const_t<Scalar>>::value(),
                               !std::is_void_v<Scalar> && !std::is_const_v<Scalar>, Config::N,
                               req_shape_ptr, Config::Order::value, desc)) {
            return false;
        }
        val = ArrT(std::move(desc));
        return true;
    }

    ArrT& get() noexcept { return val; }

    static SEXP to_r(const ArrT& arr, rv_policy, SEXP) {
        r_ndarray_export exp;
        exp.data = arr.data();
        exp.dtype = !std::is_void_v<Scalar>
                        ? scalar_dtype_traits<std::remove_const_t<Scalar>>::value()
                        : arr.dtype_code();
        exp.shape = arr.shape_ptr();
        exp.strides = arr.stride_ptr();
        exp.ndim = arr.ndim();
        exp.order = Config::Order::value == 'F' ? 'F' : 'C';
        return r_export_ndarray(exp);
    }
};

} // namespace r_detail

} // namespace nanorbind
