# Overhead & Data-Science Benchmarks

`pyrabind` includes a head-to-head benchmark suite (`benchmarks/`) comparing:

- **Pure C++ `-O3`** (`benchmarks/bench_cpp.cpp`)
- **Python `pyrabind`** (`benchmarks/bench_module.cpp` passing straight through to upstream `nanobind` with `NOMINSIZE` / `-O3`)
- **Raw `nanobind`** (`benchmarks/raw_nb_baseline.cpp` using `NB_MODULE` directly with `NOMINSIZE` / `-O3`, covering all 7 workloads plus `std::vector` copy)
- **Raw Python C-API** (`benchmarks/raw_py_baseline.cpp` using `METH_FASTCALL` + `Py_buffer`)
- **R `pyrabind`** (`benchmarks/bench_module.cpp` compiled against `cpp11` + `.External` + hashed class environments)
- **Raw R `.Call`** (`benchmarks/raw_r_baseline.cpp`)
- **Pure NumPy / pandas & pure base R**

## Test Environment

Measured in full `-O3` Release mode (`-DCMAKE_BUILD_TYPE=Release` with `NOMINSIZE` passed to `pyrabind_add_module` and `nanobind_add_module`) on:

- **Hardware / OS**: `Apple M4`, `macOS-27.0.1-arm64-arm-64bit` (`arm64`)
- **Compiler**: `Apple clang version 21.0.0 (clang-2100.4.1.1)` (`-std=c++20`)
- **Python stack**: Python `3.12.8`, `nanobind 3.1.0`, NumPy `2.4.4`, pandas `3.0.2`
- **R stack**: R `4.5.3`, `cpp11 0.5.2`

*(Note: passing `NOMINSIZE` overrides `nanobind`'s default `-Os` size-optimization flag so scalar loops and C++ dispatch compile with `-O3` identically across `pyrabind_bench_cpp`, `bench_module`, `raw_nb_bench`, `raw_py_bench`, and `raw_r_bench`.)*

---

## 1. Core FFI Dispatch & Class Methods (Median Latency)

| Benchmark | C++ `-O3` | Python `pyrabind` | Raw `nanobind` | `pyrabind` / `nb` | Python raw C-API | Pure Python | R `pyrabind` | R raw `.Call` | `pyrabind` / raw (R) | Pure base R |
| :--- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| `noop()` | 0.5 ns | 15.8 ns | 16.5 ns | 0.96x | 10.6 ns | 17.2 ns | 311.3 ns | 115.5 ns | 2.69x | 121.3 ns |
| `add_i64(17, 25)` | 0.4 ns | 33.1 ns | 33.2 ns | 1.00x | 28.2 ns | 32.7 ns | 408.7 ns | 146.9 ns | 2.78x | 186.4 ns |
| `BenchPoint.dot` | 1.2 ns | 38.4 ns | 36.7 ns | 1.05x | 28.3 ns | 50.3 ns | 2.36 us | 188.8 ns | 12.52x | 288.0 ns |

Caching the S3 `$` method lookup once in R (`pdot <- p1$dot; pdot(p2)`) reduces R `BenchPoint.dot` from **2.36 us** to **487.2 ns** (**2.58x** vs raw `.Call`).

**Takeaway on Python**: Because `pyrabind` re-exports `::nanobind` directly without wrapper objects or extra indirection, dispatch latency sits at **0.96x–1.05x** of raw `nanobind` (**15.8 ns** `noop`, **33.1 ns** `add_i64`, **38.4 ns** `point_dot`), within **5–10 ns** of raw hand-written `METH_FASTCALL` CPython C-API.

---

## 2. NumPy, `ndarray`, Matrices & DataFrames (Median Latency)

| Workload | C++ `-O3` | Python `pyrabind` (zero-copy) | Raw `nanobind` (zero-copy) | `pyrabind` / `nb` | `std::vector` copy (`pb` / `nb`) | Python raw C-API | Pure NumPy / pandas | R `pyrabind` (zero-copy) | R `std::vector` copy | R raw `.Call` | Pure base R |
| :--- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| `dot_100k` | 51.53 us | 51.72 us | 51.73 us | 1.00x | 489.75 us / 490.64 us | 51.67 us | 3.24 us | 52.98 us | 80.95 us | 52.66 us | 148.91 us |
| `scale_100k` | 16.63 us | 10.21 us | 13.79 us | 0.74x | 1.032 ms / 1.034 ms | 13.57 us | 25.75 us | 30.05 us | 53.61 us | 16.30 us | 54.28 us |
| `mat_sum_256` | 33.10 us | 32.85 us | 32.83 us | 1.00x | — | 32.92 us | 8.09 us | 32.94 us | — | 32.61 us | 75.81 us |
| `df_transform_100k` | 135.47 us | 3.539 ms | 346.19 us | 10.22x | — | 310.83 us | 432.22 us | 1.970 ms | — | 19.72 us | 179.11 us |

### Key Findings

1. **Identical Python performance to upstream `nanobind` across every numerical workload**:
   - With `-O3` (`NOMINSIZE`), `dot_100k` reaches **51.72 us** in `pyrabind` vs **51.73 us** in raw `nanobind` (`1.00x`) and **51.53 us** in pure C++ `-O3` (`np.dot` at **3.24 us** delegates to vectorized BLAS/NEON).
   - `scale_100k` runs in **10.21 us** (`pyrabind`) / **13.79 us** (raw `nanobind`) via zero-copy `nb::ndarray` + `nb::capsule` ownership transfer — faster than pure vectorized NumPy (**25.75 us**) and **~100x faster** than returning `std::vector<double>` (**1.032 ms** / **1.034 ms**).
   - `mat_sum_256` is identical to raw `nanobind` and raw CPython C-API (**32.85 us** vs **32.83 us** / **32.92 us**, `1.00x`).
2. **Zero-copy R matrices & vectors match raw `.Call`**:
   - `dot_100k` (**52.98 us** vs **52.66 us** raw `.Call`, **2.81x faster** than base R `148.91 us`) and column-major `mat_sum_256` (**32.94 us** vs **32.61 us** raw `.Call`, **2.30x faster** than base R `75.81 us`) achieve native C speed with zero transposition.
3. **`data_frame` materialization trade-off**:
   - In `df_transform_100k`, raw `nanobind` (**346.19 us**) passes existing `id`, `x`, and `tag` `pandas.Series` column references straight through to the new DataFrame and materializes only the computed `y` `ndarray` via `nb::capsule` without copying 100k strings.
   - `pyrabind::data_frame` eagerly converts all 4 columns across C++/Python (**3.539 ms**) and C++/R (**1.970 ms**) so pure C++ business logic can inspect every column uniformly without Python/R API calls. For hot inner loops that only touch numeric columns, pass `nb::ndarray<const double, nb::ndim<1>>` directly.

---

## Reproducing Locally

```bash
cmake --preset release
cmake --build build-release -j
python3 benchmarks/run_all.py --build-dir build-release
```

Optionally pass `--python /path/to/python3` and `--json-out results.json`.
