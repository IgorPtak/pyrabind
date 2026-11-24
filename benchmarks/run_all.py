#!/usr/bin/env python3
"""Run C++, Python (nanorbind vs raw nanobind vs raw C-API), and R overhead benchmarks."""

from __future__ import annotations

import argparse
import json
import platform
import subprocess
import sys
from pathlib import Path
from typing import Any


def fmt_ns(ns: float | None) -> str:
    if ns is None:
        return "N/A"
    if ns < 1_000.0:
        return f"{ns:.1f} ns"
    if ns < 1_000_000.0:
        return f"{ns / 1_000.0:.2f} us"
    return f"{ns / 1_000_000.0:.3f} ms"


def fmt_ratio(num: float | None, den: float | None) -> str:
    if num is None or den is None or den <= 0.0:
        return "N/A"
    return f"{num / den:.2f}x"


def find_shlib(directory: Path, stem: str) -> Path:
    for ext in (".so", ".dylib", ".dll"):
        candidate = directory / f"{stem}{ext}"
        if candidate.exists():
            return candidate
    raise FileNotFoundError(f"cannot locate {stem}.* in {directory}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", type=Path, default=Path("build-release"))
    parser.add_argument("--python", type=str, default=sys.executable)
    parser.add_argument("--rscript", type=str, default="Rscript")
    parser.add_argument("--json-out", type=Path, default=None)
    args = parser.parse_args()

    repo_root = Path(__file__).resolve().parent.parent
    build_dir = args.build_dir if args.build_dir.is_absolute() else (repo_root / args.build_dir)

    cpp_bin = build_dir / "benchmarks" / "cpp" / "nanorbind_bench_cpp"
    cpp_res = json.loads(subprocess.check_output([str(cpp_bin)], text=True))

    py_script = repo_root / "benchmarks" / "run_bench.py"
    py_res = json.loads(
        subprocess.check_output(
            [args.python, str(py_script), "--build-dir", str(build_dir)], text=True
        )
    )

    r_script = repo_root / "benchmarks" / "run_bench.R"
    nanorbind_r = repo_root / "r" / "nanorbind.R"
    r_dir = build_dir / "benchmarks" / "r"
    bench_r_shlib = find_shlib(r_dir, "bench_module")
    raw_r_shlib = find_shlib(r_dir, "raw_r_bench")
    r_res = json.loads(
        subprocess.check_output(
            [args.rscript, str(r_script), str(nanorbind_r), str(bench_r_shlib), str(raw_r_shlib)],
            text=True,
        )
    )

    payload: dict[str, Any] = {
        "system": {
            "platform": platform.platform(),
            "machine": platform.machine(),
            "build_dir": str(build_dir.name),
        },
        "cpp": cpp_res,
        "python": py_res,
        "r": r_res,
    }
    if args.json_out is not None:
        args.json_out.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")

    pv = py_res["versions"]
    rv = r_res["versions"]
    print(f"Platform: {platform.platform()} ({platform.machine()})")
    print(
        f"Versions: Python {pv.get('python')}, nanobind {pv.get('nanobind')}, "
        f"NumPy {pv.get('numpy')}, pandas {pv.get('pandas')}, "
        f"R {rv.get('r')}, cpp11 {rv.get('cpp11')}\n"
    )

    print("### Core Dispatch & Scalar/Class Overhead (median latency)\n")
    print(
        "| Benchmark | C++ `-O3` | Python `nanorbind` | Raw `nanobind` | `nanorbind`/`nb` | "
        "Python raw C-API | R `nanorbind` | R raw `.Call` | `nanorbind`/raw (R) |"
    )
    print("| :--- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |")
    for name in ("noop", "add_i64", "point_dot"):
        c_ns = cpp_res[name]["median_ns"]
        py_b = py_res["benchmarks"][name]
        r_b = r_res["benchmarks"][name]
        py_nr = py_b["nanorbind"]["median_ns"]
        py_nb = py_b["raw_nanobind"]["median_ns"]
        py_raw = py_b["raw_capi"]["median_ns"]
        r_nr = r_b["nanorbind"]["median_ns"]
        r_raw = r_b["raw_capi"]["median_ns"]
        print(
            f"| `{name}` | {fmt_ns(c_ns)} | {fmt_ns(py_nr)} | {fmt_ns(py_nb)} | "
            f"{fmt_ratio(py_nr, py_nb)} | {fmt_ns(py_raw)} | {fmt_ns(r_nr)} | "
            f"{fmt_ns(r_raw)} | {fmt_ratio(r_nr, r_raw)} |"
        )
    if "point_dot" in r_res["benchmarks"] and "nanorbind_cached" in r_res["benchmarks"]["point_dot"]:
        r_b = r_res["benchmarks"]["point_dot"]
        print(
            f"\nR cached method closure (`pdot <- p1$dot; pdot(p2)`): "
            f"{fmt_ns(r_b['nanorbind_cached']['median_ns'])} "
            f"({fmt_ratio(r_b['nanorbind_cached']['median_ns'], r_b['raw_capi']['median_ns'])} vs raw `.Call`)"
        )

    print("\n### Array, Matrix & DataFrame Workloads (median latency)\n")
    print(
        "| Workload | C++ `-O3` | Python `nanorbind` (zero-copy) | Raw `nanobind` | "
        "Python `std::vector` copy | Python raw C-API | Pure NumPy/pandas | "
        "R `nanorbind` (zero-copy) | R `std::vector` copy | R raw `.Call` | Pure base R |"
    )
    print("| :--- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |")

    def row_array(key: str, py_zc: str, py_cp: str | None, r_zc: str, r_cp: str | None) -> None:
        c_ns = cpp_res[key]["median_ns"]
        py_b = py_res["benchmarks"].get(key, {})
        r_b = r_res["benchmarks"].get(key, {})
        py_zc_v = py_b.get(py_zc, {}).get("median_ns")
        py_nb_v = py_b.get("raw_nanobind", {}).get("median_ns")
        py_cp_v = py_b.get(py_cp, {}).get("median_ns") if py_cp else None
        py_raw_v = py_b.get("raw_capi", {}).get("median_ns")
        py_pure_v = py_b.get("pure_lang", {}).get("median_ns")
        r_zc_v = r_b.get(r_zc, {}).get("median_ns")
        r_cp_v = r_b.get(r_cp, {}).get("median_ns") if r_cp else None
        r_raw_v = r_b.get("raw_capi", {}).get("median_ns")
        r_pure_v = r_b.get("pure_lang", {}).get("median_ns")
        print(
            f"| `{key}` | {fmt_ns(c_ns)} | {fmt_ns(py_zc_v)} | {fmt_ns(py_nb_v)} | "
            f"{fmt_ns(py_cp_v)} | {fmt_ns(py_raw_v)} | {fmt_ns(py_pure_v)} | "
            f"{fmt_ns(r_zc_v)} | {fmt_ns(r_cp_v)} | {fmt_ns(r_raw_v)} | {fmt_ns(r_pure_v)} |"
        )

    row_array("dot_100k", "nanorbind_view", "nanorbind_vec_copy", "nanorbind_view", "nanorbind_vec_copy")
    row_array(
        "scale_100k",
        "nanorbind_ndarray",
        "nanorbind_vec_copy",
        "nanorbind_ndarray",
        "nanorbind_vec_copy",
    )
    row_array("mat_sum_256", "nanorbind_view", None, "nanorbind_view", None)
    row_array("df_transform_100k", "nanorbind", None, "nanorbind", None)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
