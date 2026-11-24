# NumPy, `ndarray`, Eigen, Arrow & `data_frame`

`pyrabind` provides high-throughput interfaces for numerical computing, linear algebra, and columnar data workflows:

- **`pyrabind/ndarray.hpp`** (`include/pyrabind/ndarray.hpp`): Zero-copy strided views (`nb::ndarray<...>`) over NumPy arrays and R vectors/matrices, plus ownership-transferred return arrays via `nb::capsule`.
- **`pyrabind/eigen.hpp`** (`include/pyrabind/eigen.hpp`): Dense matrices, vectors, arrays, and zero-copy `Eigen::Map` / `Eigen::Ref` views (`DRef`, `DMap`, `DStride`, `DRef1`, `DMap1`, `DStride1`).
- **`pyrabind/dataframe.hpp`** (`include/pyrabind/dataframe.hpp`): Nullable typed columnar tables (`nb::data_frame`) converting between `pandas.DataFrame` / Python `dict` and R `data.frame` / `tibble`.
- **`pyrabind/arrow.hpp`** (`include/pyrabind/arrow.hpp`): Zero-copy Arrow C Data Interface bridges (`pyrabind::arrow_batch`, `export_to_arrow`, `import_from_arrow`) with PyCapsule (`pyarrow`) and `nanoarrow` integration.

---

## 1. `nb::ndarray`: Zero-Copy Arrays & Matrices

### Template Parameters

Include `#include "pyrabind/ndarray.hpp"`. `nb::ndarray<...>` accepts upstream `nanobind`-compatible template tags in any order:

| Parameter | Meaning |
| :--- | :--- |
| `T` or `const T` (or `nb::ro`) | Element type (`double`, `float`, `std::int32_t`, `std::int64_t`, `std::uint8_t`, `bool`); `const T` / `nb::ro` marks input read-only |
| `nb::ndim<N>` | Require exact dimensionality `N` (`1`, `2`, ...) |
| `nb::shape<D0, D1, ...>` | Require dimensionality and fixed extents (use `-1` for dynamic dimensions, e.g., `nb::shape<-1, 3>`) |
| `nb::c_contig` / `nb::f_contig` / `nb::any_contig` | Memory contiguity constraint (note: R matrices/arrays >= 2D are column-major and reject `c_contig`) |
| `nb::numpy`, `nb::pytorch`, `nb::no_framework`, `nb::device::cpu` | Framework / device tags forwarded directly to `nanobind` on Python |

### Reading Input Arrays with `.view()` and Contiguous Fast Paths

Using `a.view()` gives safe, bounds-agnostic strided indexing (`va(i)` or `vm(r, c)`) that handles both **row-major NumPy arrays** and **column-major R matrices** identically, as well as strided slices like `a[::2]`. For peak throughput on contiguous buffers, pair a stride fast path with `.view()` fallback:

```cpp
double dot_view(nb::ndarray<const double, nb::ndim<1>> a,
                nb::ndarray<const double, nb::ndim<1>> b) {
    const std::size_t n = a.shape(0);
    if (b.shape(0) != n) {
        throw nb::value_error("vectors must have identical length");
    }
    if (a.stride(0) == 1 && b.stride(0) == 1 && n > 0) {
        double acc = 0.0;
        const double* pa = a.data();
        const double* pb = b.data();
        for (std::size_t i = 0; i < n; ++i) {
            acc += pa[i] * pb[i];
        }
        return acc;
    }
    auto va = a.view();
    auto vb = b.view();
    double acc = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        acc += va(i) * vb(i);
    }
    return acc;
}
```

Checking either C-contiguous or F-contiguous layout on 2D matrices avoids any copy or transpose on both NumPy (`row_stride == cols && col_stride == 1`) and R (`row_stride == 1 && col_stride == rows`):

```cpp
double mat_sum_view(nb::ndarray<const double, nb::ndim<2>> m) {
    const std::size_t rows = m.shape(0);
    const std::size_t cols = m.shape(1);
    if ((m.stride(0) == static_cast<std::int64_t>(cols) && m.stride(1) == 1) ||
        (m.stride(0) == 1 && m.stride(1) == static_cast<std::int64_t>(rows))) {
        double acc = 0.0;
        const double* p = m.data();
        for (std::size_t i = 0; i < rows * cols; ++i) {
            acc += p[i];
        }
        return acc;
    }
    auto vm = m.view();
    double acc = 0.0;
    for (std::size_t r = 0; r < rows; ++r) {
        for (std::size_t c = 0; c < cols; ++c) {
            acc += vm(r, c);
        }
    }
    return acc;
}
```

### Returning Newly Allocated Arrays via `nb::capsule`

```cpp
nb::ndarray<nb::numpy, double, nb::ndim<2>> nd_make_2d(std::size_t rows, std::size_t cols) {
    auto* buf = new double[rows * cols];
    for (std::size_t r = 0; r < rows; ++r) {
        for (std::size_t c = 0; c < cols; ++c) {
            buf[r * cols + c] = static_cast<double>(r * 10 + c + 1);
        }
    }
    nb::capsule owner(buf, [](void* p) noexcept { delete[] static_cast<double*>(p); });
    return nb::ndarray<nb::numpy, double, nb::ndim<2>>(buf, {rows, cols}, owner);
}
```

- **Python**: `nanobind` transfers `buf` and `owner` straight into a `numpy.ndarray` with **zero copy** (`scale_100k`: **10.21 us** vs **1.032 ms** for `std::vector<double>`).
- **R**: `r_export_ndarray` allocates a native R vector (`REALSXP`, `INTSXP`, `LGLSXP`, `RAWSXP`) or column-major matrix/array (`Rf_allocMatrix` / `R_DimSymbol`), transposing 2D+ strides into column-major R storage before C++ releases the capsule (`out2[1L, 3L] == 3.0`, `out2[2L, 1L] == 11.0`).

### R `ndarray` Mapping & ALTREP Details

- **Zero-copy input dtypes in R**: `REALSXP` -> `double`, `INTSXP` -> `std::int32_t`, `RAWSXP` -> `std::uint8_t`.
- **ALTREP guard on mutable arrays**: Requests for non-`const` writable arrays reject `ALTREP(x)` R objects (e.g., compact sequences like `1:100`), preventing illegal in-place mutation of lazy R ranges; read-only `const T` / `nb::ro` accepts `REAL_RO` / `INTEGER_RO` / `RAW_RO`.
- **Exported dtypes to R**: `double`, `float`, and `std::int64_t` export as `REALSXP`; `std::int32_t` exports as `INTSXP`; `bool` exports as `LGLSXP`; `std::uint8_t` exports as `RAWSXP`.

---

## 2. `nb::data_frame`: Columnar Tables (`pandas.DataFrame` & R `data.frame`)

### C++ Structure

Defined in `include/pyrabind/dataframe_types.hpp`:

```cpp
using column_data = std::variant<
    std::vector<double>,
    std::vector<std::int64_t>,
    std::vector<std::int32_t>,
    std::vector<bool>,
    std::vector<std::string>
>;

struct data_frame {
    std::vector<std::string> names;
    std::vector<column_data> columns;
    // Optional per-column validity masks aligned with `columns`:
    // empty inner vector = all rows valid; otherwise 1 = valid, 0 = null/NA.
    std::vector<std::vector<std::uint8_t>> validity;

    std::size_t ncols() const noexcept;
    std::size_t nrows() const noexcept;
    bool empty() const noexcept;
    bool contains(std::string_view name) const noexcept;
    std::size_t index_of(std::string_view name) const;
    column_data& operator[](std::string_view name);
    const column_data& operator[](std::string_view name) const;
    bool has_nulls(std::size_t col_idx) const noexcept;
    bool any_nulls() const noexcept;
    bool is_null(std::size_t col_idx, std::size_t row_idx) const noexcept;
    void set_null(std::size_t col_idx, std::size_t row_idx, bool null_val = true);
    void add_column(std::string name, column_data col, std::vector<std::uint8_t> val_mask = {});
};
```

`add_column` validates unique column names, consistent row counts, and optional `val_mask` lengths (throwing `std::invalid_argument` on duplicates or length mismatches). Existing callers that ignore `validity` incur no extra allocation (all-valid columns keep an empty mask).

### Host Conversions & Nullability

- **Python (`include/pyrabind/python/dataframe.hpp`)**:
  - **Input**: Accepts `pandas.DataFrame` (or any mapping exposing `.columns`) as well as plain `dict[str, sequence_or_array]`. Missing values (`NaN`, `None`, `pd.NA`) populate per-column `validity` masks (`0 = null`), while `float32` buffers widen cleanly to `double` and `pandas` 3.x `'O'` string arrays convert directly.
  - **Output**: Constructs a `pandas.DataFrame` when `pandas` is importable (emitting `NaN`, nullable integer/boolean `pd.array(..., dtype="Int64"/"boolean")` or object `None` when nulls are present), or gracefully falls back to a Python `dict` if `pandas` is unavailable.
- **R (`src/r/conversions.cpp`)**:
  - **Input**: Accepts R `data.frame` and `tibble` objects in Pass 1 (`convert = false`) and named lists of equal-length column vectors (`VECSXP` with `names`) in Pass 2 (`convert = true`). Supports `REALSXP`, `INTSXP`, `LGLSXP`, and `STRSXP`, translating `NA_real_` / `NaN`, `NA_integer_`, `NA_LOGICAL`, and `NA_STRING` into `validity` masks (`0 = null`).
  - **Output**: Returns a native base R `data.frame` with compact `c(NA_integer_, -nrows)` row names and preserves `NA` entries (`NA_real_`, `NA_integer_`, `NA_LOGICAL`, `NA_STRING`) wherever `validity[c][r] == 0`.

### Performance Guidance

`nb::data_frame` eagerly materializes all columns across the C++/host boundary (including `std::string` allocations per string element), taking **3.539 ms** in Python and **1.970 ms** in R on 100k rows (`id`, `x`, `y`, `tag`). When only numeric columns are needed in a hot loop, extract columns in host code and pass individual `nb::ndarray<const T, nb::ndim<1>>` parameters instead for sub-100 us latency.

---

## 3. Eigen Dense Matrices & Zero-Copy `Map`/`Ref`

Include `#include "pyrabind/eigen.hpp"` to exchange Eigen matrices, vectors, arrays, and strided views across both backends:

- **Python**: Forwards directly to `<nanobind/eigen/dense.h>`.
- **R (`include/pyrabind/r/eigen.hpp`)**: Supports owning `Eigen::Matrix<...>` / `Eigen::Array<...>` values, zero-copy `Eigen::Map<...>` inputs (const contiguous or strided), mutable `Eigen::Ref<MatrixT>` / `DRef<MatrixT>` in-place updates on non-`ALTREP` R matrices/vectors, and convenient dynamic stride aliases (`pyrabind::DStride`, `DRef`, `DMap`, `DStride1`, `DRef1`, `DMap1`).

```cpp
#include "pyrabind/eigen.hpp"

void add_scaled_inplace(nb::DRef<Eigen::MatrixXd> mat, double scale) {
    mat.array() += scale;
}

Eigen::VectorXd matvec(const nb::DRef<const Eigen::MatrixXd>& a,
                       const nb::DRef<const Eigen::VectorXd>& x) {
    return a * x;
}
```

Use dynamic-stride aliases (`nb::DRef<Eigen::MatrixXd>` / `nb::DRef<const Eigen::MatrixXd>`) in dual-target signatures so both row-major NumPy arrays (`c_contig`) and column-major R matrices (`f_contig`) bind zero-copy without forcing a transpose. Returning owning `Eigen::MatrixXd` or `Eigen::VectorXd` automatically converts to NumPy arrays on Python and native column-major `matrix`/vectors on R.

---

## 4. Zero-Copy Arrow C Data Interface (`pyrabind/arrow.hpp`)

Include `#include "pyrabind/arrow.hpp"` (which bundles self-contained Arrow C Data definitions in `pyrabind/arrow_abi.hpp`) to pass columnar struct batches cleanly between C++, `pyarrow`, and R `nanoarrow`:

- **`pyrabind::arrow_batch`**: Move-only RAII wrapper managing paired `ArrowSchema` and `ArrowArray` structs (`is_valid()`, `release()`).
- **`export_to_arrow(data_frame)`**: Builds a zero-copy struct (`"+s"`) `arrow_batch` with Arrow validity bitmaps and UTF-8 offset/byte buffers kept alive via shared ownership. Supports `double` (`"g"`), `int64` (`"l"`), `int32` (`"i"`), `bool` (`"b"`), and `std::string` (`"u"`).
- **`import_from_arrow(const arrow_batch&)`**: Reads struct (`"+s"`) batches back into `pyrabind::data_frame` (`"g"`, `"f"`, `"l"`, `"i"`, `"b"`, `"u"`, `"U"` plus validity bitmaps).
- **Host interop**:
  - **Python**: Accepts any object exposing `__arrow_c_array__` (such as `pyarrow.RecordBatch`) and returns a capsule holder implementing the Arrow PyCapsule Protocol (`__arrow_c_array__`) consumable directly by `pyarrow.record_batch(...)`.
  - **R**: Accepts `nanoarrow_array` external pointers (reading the paired `nanoarrow_schema` stored in `R_ExternalPtrTag` or `"schema"` attribute) and returns an R `nanoarrow_array` external pointer with its `nanoarrow_schema` attached on `R_ExternalPtrTag`.
