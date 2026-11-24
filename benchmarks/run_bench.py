#!/usr/bin/env python3
"""Python overhead benchmarks: nanorbind vs raw nanobind vs raw C-API vs pure Python/NumPy/pandas."""

from __future__ import annotations

import argparse
import gc
import json
import math
import statistics
import sys
import time
from pathlib import Path
from typing import Any, Callable


def measure(
    fn: Callable[[], Any],
    *,
    target_trial_ms: float = 15.0,
    trials: int = 19,
    min_iters: int = 1,
    max_iters: int = 200_000,
) -> dict[str, Any]:
    fn()
    iters = min_iters
    while iters < max_iters:
        t0 = time.perf_counter_ns()
        for _ in range(iters):
            fn()
        elapsed_ns = time.perf_counter_ns() - t0
        if elapsed_ns >= target_trial_ms * 1_000_000.0:
            break
        if elapsed_ns <= 0:
            iters = min(iters * 10, max_iters)
        else:
            factor = max(2, int((target_trial_ms * 1_000_000.0) / elapsed_ns) + 1)
            iters = min(iters * factor, max_iters)

    gc.collect()
    samples_ns: list[float] = []
    for _ in range(trials):
        t0 = time.perf_counter_ns()
        for _ in range(iters):
            fn()
        dt_ns = time.perf_counter_ns() - t0
        samples_ns.append(dt_ns / float(iters))

    samples_ns.sort()
    return {
        "median_ns": statistics.median(samples_ns),
        "mean_ns": statistics.fmean(samples_ns),
        "min_ns": samples_ns[0],
        "p95_ns": samples_ns[min(len(samples_ns) - 1, int(0.95 * len(samples_ns)))],
        "inner_iters": iters,
        "trials": trials,
    }


def pure_noop() -> None:
    return None


def pure_add_i64(a: int, b: int) -> int:
    return a + b


class PurePoint:
    __slots__ = ("x", "y")

    def __init__(self, x: float, y: float) -> None:
        self.x = x
        self.y = y

    def dot(self, other: PurePoint) -> float:
        return self.x * other.x + self.y * other.y


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", type=Path, default=None)
    args = parser.parse_args()

    if args.build_dir is not None:
        sys.path.insert(0, str(args.build_dir / "benchmarks" / "python"))

    import bench_module as nr
    import raw_nb_bench as raw_nb
    import raw_py_bench as raw_py
    import nanobind as nb_pkg
    import numpy as np
    import pandas as pd

    results: dict[str, Any] = {
        "versions": {
            "python": sys.version.split()[0],
            "nanobind": getattr(nb_pkg, "__version__", None),
            "numpy": getattr(np, "__version__", None),
            "pandas": getattr(pd, "__version__", None),
        },
        "benchmarks": {},
    }
    benches = results["benchmarks"]

    assert nr.add_i64(17, 25) == 42
    assert raw_nb.add_i64(17, 25) == 42
    assert raw_py.add_i64(17, 25) == 42

    p_nr_1 = nr.BenchPoint(3.0, 4.0)
    p_nr_2 = nr.BenchPoint(-1.5, 2.5)
    p_nb_1 = raw_nb.BenchPoint(3.0, 4.0)
    p_nb_2 = raw_nb.BenchPoint(-1.5, 2.5)
    p_raw_1 = raw_py.BenchPoint(3.0, 4.0)
    p_raw_2 = raw_py.BenchPoint(-1.5, 2.5)
    p_py_1 = PurePoint(3.0, 4.0)
    p_py_2 = PurePoint(-1.5, 2.5)
    assert math.isclose(p_nr_1.dot(p_nr_2), 5.5)
    assert math.isclose(p_nb_1.dot(p_nb_2), 5.5)
    assert math.isclose(p_raw_1.dot(p_raw_2), 5.5)
    assert math.isclose(p_py_1.dot(p_py_2), 5.5)

    benches["noop"] = {
        "nanorbind": measure(nr.noop),
        "raw_nanobind": measure(raw_nb.noop),
        "raw_capi": measure(raw_py.noop),
        "pure_lang": measure(pure_noop),
    }

    benches["add_i64"] = {
        "nanorbind": measure(lambda: nr.add_i64(17, 25)),
        "raw_nanobind": measure(lambda: raw_nb.add_i64(17, 25)),
        "raw_capi": measure(lambda: raw_py.add_i64(17, 25)),
        "pure_lang": measure(lambda: pure_add_i64(17, 25)),
    }

    benches["point_dot"] = {
        "nanorbind": measure(lambda: p_nr_1.dot(p_nr_2)),
        "raw_nanobind": measure(lambda: p_nb_1.dot(p_nb_2)),
        "raw_capi": measure(lambda: p_raw_1.dot(p_raw_2)),
        "pure_lang": measure(lambda: p_py_1.dot(p_py_2)),
    }

    n = 100_000
    a = (np.arange(n, dtype=np.float64) % 1000) * 0.25
    b = ((np.arange(n, dtype=np.float64) + 7) % 1000) * 0.5
    a_list = a.tolist()
    b_list = b.tolist()

    expected_dot = float(np.dot(a, b))
    assert math.isclose(nr.dot_view(a, b), expected_dot, rel_tol=1e-12)
    assert math.isclose(raw_nb.dot_view(a, b), expected_dot, rel_tol=1e-12)
    assert math.isclose(raw_py.dot_view(a, b), expected_dot, rel_tol=1e-12)
    assert math.isclose(nr.dot_view(a[::2], b[::2]), float(np.dot(a[::2], b[::2])), rel_tol=1e-12)
    assert math.isclose(nr.dot_vec(a_list, b_list), expected_dot, rel_tol=1e-12)

    benches["dot_100k"] = {
        "nanorbind_view": measure(lambda: nr.dot_view(a, b)),
        "raw_nanobind": measure(lambda: raw_nb.dot_view(a, b)),
        "nanorbind_vec_copy": measure(lambda: nr.dot_vec(a_list, b_list), min_iters=5),
        "raw_capi": measure(lambda: raw_py.dot_view(a, b)),
        "pure_lang": measure(lambda: float(np.dot(a, b))),
    }

    expected_scale = a * 1.5 - 2.0
    assert np.allclose(nr.scale_ndarray(a, 1.5, -2.0), expected_scale)
    assert np.allclose(raw_nb.scale_ndarray(a, 1.5, -2.0), expected_scale)
    assert np.allclose(raw_py.scale_ndarray(a, 1.5, -2.0), expected_scale)
    assert np.allclose(nr.scale_vec(a_list, 1.5, -2.0), expected_scale)

    benches["scale_100k"] = {
        "nanorbind_ndarray": measure(lambda: nr.scale_ndarray(a, 1.5, -2.0)),
        "raw_nanobind": measure(lambda: raw_nb.scale_ndarray(a, 1.5, -2.0)),
        "nanorbind_vec_copy": measure(lambda: nr.scale_vec(a_list, 1.5, -2.0), min_iters=5),
        "raw_capi": measure(lambda: raw_py.scale_ndarray(a, 1.5, -2.0)),
        "pure_lang": measure(lambda: a * 1.5 - 2.0),
    }

    mat = np.full((256, 256), 1.25, dtype=np.float64)
    expected_sum = 256.0 * 256.0 * 1.25
    assert math.isclose(nr.mat_sum_view(mat), expected_sum)
    assert math.isclose(raw_nb.mat_sum_view(mat), expected_sum)
    assert math.isclose(raw_py.mat_sum_view(mat), expected_sum)

    benches["mat_sum_256"] = {
        "nanorbind_view": measure(lambda: nr.mat_sum_view(mat)),
        "raw_nanobind": measure(lambda: raw_nb.mat_sum_view(mat)),
        "raw_capi": measure(lambda: raw_py.mat_sum_view(mat)),
        "pure_lang": measure(lambda: float(mat.sum())),
    }

    tags = np.where((np.arange(n) % 2) == 0, "even", "odd")
    df = pd.DataFrame(
        {
            "id": np.arange(n, dtype=np.int64),
            "x": np.arange(n, dtype=np.float64) * 0.125,
            "tag": pd.Series(tags, dtype=object),
        }
    )
    out_nr = nr.df_transform(df)
    out_raw = raw_py.df_transform(df)
    assert list(out_nr.columns) == ["id", "x", "y", "tag"]
    assert len(out_nr) == n
    assert np.allclose(out_nr["y"].to_numpy(), out_raw["y"].to_numpy())

    def pure_pandas_transform() -> Any:
        return pd.DataFrame(
            {
                "id": df["id"],
                "x": df["x"],
                "y": df["x"] * 2.0 + df["id"],
                "tag": df["tag"],
            }
        )

    benches["df_transform_100k"] = {
        "nanorbind": measure(lambda: nr.df_transform(df), min_iters=2, max_iters=200),
        "raw_capi": measure(lambda: raw_py.df_transform(df), min_iters=2, max_iters=200),
        "pure_lang": measure(pure_pandas_transform, min_iters=2, max_iters=200),
    }

    print(json.dumps(results, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
