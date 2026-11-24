# Overhead & Data-Science Benchmarks

`nanorbind` includes a head-to-head benchmark suite (`benchmarks/`) comparing:

- **Pure C++ `-O3`** (`benchmarks/bench_cpp.cpp`)
- **Python `nanorbind`** (`benchmarks/bench_module.cpp` passing straight through to upstream `nanobind`)
- **Raw `nanobind`** (`benchmarks/raw_nb_baseline.cpp` using `NB_MODULE` directly)
- **Raw Python C-API** (`benchmarks/raw_py_baseline.cpp` using `METH_FASTCALL` + `Py_buffer`)
- **R `nanorbind`** (`benchmarks/bench_module.cpp` compiled against `cpp11` + `.External` + hashed class environments)
- **Raw R `.Call`** (`benchmarks/raw_r_baseline.cpp`)
- **Pure NumPy / pandas & pure base R**

## Test Environment

Measured in Release mode (`-DCMAKE_BUILD_TYPE=Release`) on:

- **Hardware / OS**: `Apple M4`, `macOS-27.0.1-arm64-arm-64bit` (`arm64`)
- **Compiler**: `Apple clang version 21.0.0 (clang-2100.4.1.1)` (`-std=c++20`)
- **Python stack**: Python `3.12.8`, `nanobind 3.1.0`, NumPy `2.4.4`, pandas `3.0.2`
- **R stack**: R `4.5.3`, `cpp11 0.5.2`

*(Note: upstream `nanobind_add_module` enables `-Os` by default unless `NOMINSIZE` is set, while `nanobind_bench_cpp`, `raw_py_bench`, `raw_r_bench`, and `bench_module_r` compile with `-O3`.)*

---

## 1. Core FFI Dispatch & Class Methods (Median Latency)

| Benchmark | C++ `-O3` | Python `nanorbind` | Raw `nanobind` | `nanorbind` / `nb` | Python raw C-API | Pure Python | R `nanorbind` | R raw `.Call` | `nanorbind` / raw (R) | Pure base R |
| :--- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| `noop()` | 0.2 ns | 14.5 ns | 14.8 ns | 0.98x | 10.5 ns | 17.3 ns | 316.4 ns | 115.9 ns | 2.73x | 120.1 ns |
| `add_i64(17, 25)` | 0.2 ns | 33.1 ns | 33.1 ns | 1.00x | 27.6 ns | 32.9 ns | 404.8 ns | 145.5 ns | 2.78x | 186.5 ns |
| `BenchPoint.dot` | 0.8 ns | 37.1 ns | 36.7 ns | 1.01x | 28.2 ns | 49.5 ns | 2.43 us | 190.6 ns | 12.73x | 290.4 ns |

Caching the S3 `$` method lookup once in R (`pdot <- p1$dot; pdot(p2)`) reduces R `BenchPoint.dot` from **2.43 us** to **490.0 ns** (**2.57x** vs raw `.Call`).

**Takeaway on Python**: Because `nanorbind` re-exports `::nanobind` directly without wrapper objects or extra indirection, dispatch latency sits at **0.98x–1.01x** of raw `nanobind` (**14.5 ns** `noop`, **33.1 ns** `add_i64`, **37.1 ns** `point_dot`), just **4–9 ns** above raw hand-written `METH_FASTCALL` CPython C-API.

---

## 2. NumPy, `ndarray`, Matrices & DataFrames (Median Latency)

| Workload | C++ `-O3` | Python `nanorbind` (zero-copy) | Raw `nanobind` | Python `std::vector` copy | Python raw C-API | Pure NumPy / pandas | R `nanorbind` (zero-copy) | R `std::vector` copy | R raw `.Call` | Pure base R |
| :--- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| `dot_100k` | 51.47 us | 79.76 us | 79.59 us | 550.95 us | 51.58 us | 3.22 us | 52.31 us | 78.89 us | 51.81 us | 150.51 us |
| `scale_100k` | 14.59 us | 25.38 us | 25.46 us | 1.057 ms | 13.48 us | 25.62 us | 30.71 us | 53.95 us | 16.46 us | 55.01 us |
| `mat_sum_256` | 32.56 us | 38.19 us | 38.02 us | — | 32.85 us | 8.19 us | 33.27 us | — | 32.90 us | 76.28 us |
| `df_transform_100k` | 133.08 us | 3.574 ms | — | — | 311.04 us | 436.92 us | 2.027 ms | — | 18.83 us | 188.59 us |

### Key Findings

1. **Identical Python performance to upstream `nanobind`**:
   - Across `dot_100k` (**79.76 us** vs **79.59 us**), `scale_100k` (**25.38 us** vs **25.46 us**), and `mat_sum_256` (**38.19 us** vs **38.02 us**), `nanorbind` adds zero measurable overhead over raw `nanobind`.
   - Zero-copy `nb::ndarray` + capsule return makes `scale_100k` **41.6x faster** than returning `std::vector<double>` (`1.057 ms`) and matches pure vectorized NumPy (`25.62 us`).
   - *(Why `dot_100k` is ~79.7 us in both `nanobind` targets vs ~51.5 us in C++ `-O3` / raw C-API: `nanobind_add_module` compiles module sources with `-Os` unless `NOMINSIZE` is supplied, which disables aggressive scalar loop unrolling; `np.dot` at `3.22 us` delegates to vectorized BLAS/NEON.)*
2. **Zero-copy R matrices & vectors match raw `.Call`**:
   - `dot_100k` (**52.31 us** vs **51.81 us** raw `.Call`, **2.88x faster** than base R `150.51 us`) and column-major `mat_sum_256` (**33.27 us** vs **32.90 us** raw `.Call`, **2.29x faster** than base R `76.28 us`) achieve native C speed with zero transposition.
3. **`data_frame` materialization trade-off**:
   - `df_transform_100k` eagerly materializes all 100k rows (including `std::string` columns) across C++/Python (**3.574 ms**) and C++/R (**2.027 ms**). Fast UTF-8/ASCII `CHAR` checks avoid per-element `R_UnwindProtect` jumps in R, while passing numeric columns directly as individual `nb::ndarray<const double, nb::ndim<1>>` arguments avoids string conversion altogether on hot loops.

---

## 3. Comparison vs. Custom From-Scratch Dual Backend (`pyrbind` v0.2)

Measured on the same Apple M4 machine under Release builds:

| Benchmark | `pyrbind` v0.2 (custom Py/R) | `nanorbind` (`nanobind` + `cpp11`/`.External`) | Speedup |
| :--- | ---: | ---: | ---: |
| Python `noop()` | 49.7 ns | **14.5 ns** | **3.43x** |
| Python `add_i64(17, 25)` | 103.7 ns | **33.1 ns** | **3.13x** |
| Python `BenchPoint.dot` | 136.6 ns | **37.1 ns** | **3.68x** |
| Python `scale_100k` (`ndarray`) | 41.83 us | **25.38 us** | **1.65x** |
| R `noop()` | 488.8 ns | **316.4 ns** | **1.54x** |
| R `add_i64(17, 25)` | 743.9 ns | **404.8 ns** | **1.84x** |
| R `BenchPoint.dot` (cached) | 967.5 ns | **490.0 ns** | **1.97x** |
| R `scale_100k` (`ndarray`) | 52.87 us | **30.71 us** | **1.72x** |
| R `df_transform_100k` | 8.250 ms | **2.027 ms** | **4.07x** |

---

## Reproducing Locally

```bash
cmake --preset release
cmake --build build-release -j
python3 benchmarks/run_all.py --build-dir build-release
```
