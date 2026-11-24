# Overhead & Data-Science Benchmarks

`nanorbind` includes a head-to-head benchmark suite (`benchmarks/`) comparing:

- **Pure C++ `-O3`** (`benchmarks/bench_cpp.cpp`)
- **Python `nanorbind`** (`benchmarks/bench_module.cpp` passing straight through to upstream `nanobind` with `NOMINSIZE` / `-O3`)
- **Raw `nanobind`** (`benchmarks/raw_nb_baseline.cpp` using `NB_MODULE` directly with `NOMINSIZE` / `-O3`, covering all 7 workloads plus `std::vector` copy)
- **Raw Python C-API** (`benchmarks/raw_py_baseline.cpp` using `METH_FASTCALL` + `Py_buffer`)
- **R `nanorbind`** (`benchmarks/bench_module.cpp` compiled against `cpp11` + `.External` + hashed class environments)
- **Raw R `.Call`** (`benchmarks/raw_r_baseline.cpp`)
- **Pure NumPy / pandas & pure base R**

## Test Environment

Measured in full `-O3` Release mode (`-DCMAKE_BUILD_TYPE=Release` with `NOMINSIZE` passed to `nanorbind_add_module` and `nanobind_add_module`) on:

- **Hardware / OS**: `Apple M4`, `macOS-27.0.1-arm64-arm-64bit` (`arm64`)
- **Compiler**: `Apple clang version 21.0.0 (clang-2100.4.1.1)` (`-std=c++20`)
- **Python stack**: Python `3.12.8`, `nanobind 3.1.0`, NumPy `2.4.4`, pandas `3.0.2`
- **R stack**: R `4.5.3`, `cpp11 0.5.2`

*(Note: passing `NOMINSIZE` overrides `nanobind`'s default `-Os` size-optimization flag so scalar loops and C++ dispatch compile with `-O3` identically across `bench_cpp`, `bench_module`, `raw_nb_bench`, `raw_py_bench`, and `raw_r_bench`.)*

---

## 1. Core FFI Dispatch & Class Methods (Median Latency)

| Benchmark | C++ `-O3` | Python `nanorbind` | Raw `nanobind` | `nanorbind` / `nb` | Python raw C-API | Pure Python | R `nanorbind` | R raw `.Call` | `nanorbind` / raw (R) | Pure base R |
| :--- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| `noop()` | 0.2 ns | 15.8 ns | 16.4 ns | 0.97x | 10.7 ns | 17.4 ns | 336.6 ns | 118.8 ns | 2.83x | 122.8 ns |
| `add_i64(17, 25)` | 0.2 ns | 34.7 ns | 33.4 ns | 1.04x | 28.3 ns | 32.7 ns | 414.7 ns | 146.6 ns | 2.83x | 186.7 ns |
| `BenchPoint.dot` | 0.7 ns | 37.9 ns | 37.9 ns | 1.00x | 28.1 ns | 49.3 ns | 2.47 us | 209.8 ns | 11.76x | 323.4 ns |

Caching the S3 `$` method lookup once in R (`pdot <- p1$dot; pdot(p2)`) reduces R `BenchPoint.dot` from **2.47 us** to **527.5 ns** (**2.51x** vs raw `.Call`).

**Takeaway on Python**: Because `nanorbind` re-exports `::nanobind` directly without wrapper objects or extra indirection, dispatch latency sits at **0.97x–1.04x** of raw `nanobind` (**15.8 ns** `noop`, **34.7 ns** `add_i64`, **37.9 ns** `point_dot`), within **5–10 ns** of raw hand-written `METH_FASTCALL` CPython C-API.

---

## 2. NumPy, `ndarray`, Matrices & DataFrames (Median Latency)

| Workload | C++ `-O3` | Python `nanorbind` (zero-copy) | Raw `nanobind` (zero-copy) | `nanorbind` / `nb` | `std::vector` copy (`nr` / `nb`) | Python raw C-API | Pure NumPy / pandas | R `nanorbind` (zero-copy) | R `std::vector` copy | R raw `.Call` | Pure base R |
| :--- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| `dot_100k` | 51.57 us | 52.85 us | 55.38 us | 0.95x | 492.17 us / 524.96 us | 57.47 us | 3.21 us | 58.29 us | 88.84 us | 54.01 us | 162.73 us |
| `scale_100k` | 17.04 us | 12.71 us | 12.01 us | 1.06x | 1.063 ms / 1.038 ms | 12.56 us | 30.48 us | 30.66 us | 61.43 us | 16.53 us | 57.64 us |
| `mat_sum_256` | 32.82 us | 39.49 us | 37.30 us | 1.06x | — | 35.26 us | 8.95 us | 33.55 us | — | 32.95 us | 86.84 us |
| `df_transform_100k` | 135.20 us | 4.483 ms | 350.62 us | 12.79x | — | 404.60 us | 1.536 ms | 2.039 ms | — | 16.32 us | 210.33 us |

### Key Findings

1. **Identical Python performance to upstream `nanobind` across every numerical workload**:
   - With `-O3` (`NOMINSIZE`), `dot_100k` reaches **52.85 us** in `nanorbind` vs **55.38 us** in raw `nanobind` (`0.95x`) and **51.57 us** in pure C++ `-O3` (`np.dot` at **3.21 us** delegates to vectorized BLAS/NEON).
   - `scale_100k` reaches **12.71 us** in `nanorbind` vs **12.01 us** in raw `nanobind` (`1.06x`) and **12.56 us** in raw CPython C-API — **2.40x faster** than pure vectorized NumPy (**30.48 us**) and **83.6x faster** than returning `std::vector<double>` (**1.063 ms** in `nanorbind`, **1.038 ms** in raw `nanobind`).
   - `mat_sum_256` matches raw `nanobind` (**39.49 us** vs **37.30 us**, `1.06x`).
2. **Zero-copy R matrices & vectors match raw `.Call`**:
   - `dot_100k` (**58.29 us** vs **54.01 us** raw `.Call`, **2.79x faster** than base R `162.73 us`) and column-major `mat_sum_256` (**33.55 us** vs **32.95 us** raw `.Call`, **2.59x faster** than base R `86.84 us`) achieve native C speed with zero transposition.
3. **`data_frame` materialization trade-off**:
   - In `df_transform_100k`, raw `nanobind` (**350.62 us**) passes existing `id`, `x`, and `tag` `pandas.Series` column references straight through to the new DataFrame and materializes only the computed `y` `ndarray` via `nb::capsule` without copying 100k strings.
   - `nanorbind::data_frame` eagerly converts all 4 columns across C++/Python (**4.483 ms**) and C++/R (**2.039 ms**) so pure C++ business logic can inspect every column uniformly without Python/R API calls. For hot inner loops that only touch numeric columns, pass `nb::ndarray<const double, nb::ndim<1>>` directly.

---

## 3. Comparison vs. Custom From-Scratch Dual Backend (`pyrbind`)

Measured on the same Apple M4 machine in full `-O3` Release builds:

| Benchmark | `pyrbind` (custom Py/R) | `nanorbind` (`nanobind` + `cpp11`/`.External`) | Raw `nanobind` | Speedup (`nanorbind` / `pyrbind`) |
| :--- | ---: | ---: | ---: | ---: |
| Python `noop()` | 40.3 ns | **15.8 ns** | 16.4 ns | **2.55x** |
| Python `add_i64(17, 25)` | 92.7 ns | **34.7 ns** | 33.4 ns | **2.67x** |
| Python `BenchPoint.dot` | 112.9 ns | **37.9 ns** | 37.9 ns | **2.98x** |
| Python `dot_100k` (zero-copy) | 52.85 us | **52.85 us** | 55.38 us | **1.00x** |
| Python `scale_100k` (`ndarray`) | 33.36 us | **12.71 us** | 12.01 us | **2.62x** |
| R `noop()` | 417.9 ns | **336.6 ns** | — | **1.24x** |
| R `add_i64(17, 25)` | 625.1 ns | **414.7 ns** | — | **1.51x** |
| R `BenchPoint.dot` (cached) | 785.3 ns | **527.5 ns** | — | **1.49x** |
| R `scale_100k` (`ndarray`) | 38.82 us | **30.66 us** | — | **1.27x** |
| R `df_transform_100k` | 7.884 ms | **2.039 ms** | 350.62 us (Py pass-through) | **3.87x** |

---

## Reproducing Locally

```bash
cmake --preset release
cmake --build build-release -j
python3 benchmarks/run_all.py --build-dir build-release
```
