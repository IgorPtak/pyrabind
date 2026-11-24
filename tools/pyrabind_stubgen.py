#!/usr/bin/env python3
"""Generate PEP 484 (.pyi) stubs and roxygen2 R wrappers from a compiled pyrabind Python module."""

from __future__ import annotations

import argparse
import ast
import enum
import importlib
import inspect
import keyword
import pathlib
import re
import subprocess
import sys
import types
from typing import Any


def sanitize_ident(name: str) -> str:
    if keyword.iskeyword(name) or not name.isidentifier():
        return f"{name}_"
    return name


def sanitize_r_ident(name: str) -> str:
    reserved = {
        "if", "else", "repeat", "while", "function", "for", "in", "next", "break",
        "TRUE", "FALSE", "NULL", "Inf", "NaN", "NA", "NA_integer_", "NA_real_",
        "NA_complex_", "NA_character_", "...",
    }
    if name in reserved or not re.match(r"^[A-Za-z.][A-Za-z0-9_.]*$", name):
        return f"`{name}`"
    return name


def parse_signatures(obj: Any, fallback_name: str, is_method: bool = False) -> list[str]:
    doc = getattr(obj, "__doc__", None) or ""
    sigs: list[str] = []
    for raw_line in doc.splitlines():
        line = raw_line.strip()
        if not line:
            continue
        if "(" in line and line.split("(", 1)[0].strip() in {fallback_name, "__init__"}:
            sig = line
            if is_method and not sig.startswith(f"{fallback_name}(self") and not sig.startswith(f"__init__(self"):
                sig = re.sub(r"^([^(]+)\(", r"\1(self, ", sig).replace("(self, )", "(self)")
            sigs.append(sig)
    if not sigs:
        try:
            sig_obj = inspect.signature(obj)
            sigs.append(f"{fallback_name}{sig_obj}")
        except (TypeError, ValueError):
            args = "self, *args: Any, **kwargs: Any" if is_method else "*args: Any, **kwargs: Any"
            sigs.append(f"{fallback_name}({args}) -> Any")
    deduped: list[str] = []
    for s in sigs:
        if s not in deduped:
            deduped.append(s)
    return deduped


def simplify_type_str(s: str) -> str:
    s = s.replace("nanobind.", "").replace("collections.abc.", "")
    s = re.sub(r"\bndarray\[[^\]]*\]", "Any", s)
    return s


def emit_pyi_function(
    sigs: list[str], fallback_name: str, indent: str = "", static: bool = False, is_method: bool = False
) -> list[str]:
    valid_lines: list[str] = []
    parsed_sigs: list[str] = []
    for sig in sigs:
        clean_sig = simplify_type_str(sig)
        if "->" not in clean_sig:
            clean_sig += " -> Any"
        try:
            ast.parse(f"def {clean_sig}: pass")
            parsed_sigs.append(clean_sig)
        except SyntaxError:
            continue
    if not parsed_sigs:
        args = "self, *args: Any, **kwargs: Any" if is_method else "*args: Any, **kwargs: Any"
        parsed_sigs = [f"{fallback_name}({args}) -> Any"]

    use_overload = len(parsed_sigs) > 1
    for clean_sig in parsed_sigs:
        if use_overload:
            valid_lines.append(f"{indent}@overload")
        if static:
            valid_lines.append(f"{indent}@staticmethod")
        valid_lines.append(f"{indent}def {clean_sig}: ...")
    return valid_lines


def is_exception_class(obj: Any) -> bool:
    return isinstance(obj, type) and issubclass(obj, BaseException)


def is_enum_class(obj: Any) -> bool:
    return isinstance(obj, type) and issubclass(obj, enum.Enum)


def render_module_pyi(mod: Any, indent: str = "") -> list[str]:
    lines: list[str] = []
    names = [n for n in dir(mod) if not n.startswith("__")]

    for name in sorted(names):
        obj = getattr(mod, name)
        if isinstance(obj, types.ModuleType) and obj is not mod:
            lines.append(f"{indent}class {name}:")
            sub_lines = render_module_pyi(obj, indent + "    ")
            lines.extend(sub_lines or [f"{indent}    ..."])
            lines.append("")
        elif is_exception_class(obj):
            base = obj.__bases__[0].__name__ if obj.__bases__ else "Exception"
            lines.append(f"{indent}class {name}({base}): ...")
            lines.append("")
        elif is_enum_class(obj):
            lines.append(f"{indent}class {name}(enum.Enum):")
            for mem_name in obj.__members__:
                lines.append(f"{indent}    {mem_name}: {name}")
            lines.append("")
        elif isinstance(obj, type):
            bases = [b.__name__ for b in obj.__bases__ if b is not object]
            base_str = f"({', '.join(bases)})" if bases else ""
            lines.append(f"{indent}class {name}{base_str}:")
            cls_body: list[str] = []
            for attr_name in sorted(obj.__dict__.keys()):
                if attr_name.startswith("__") and attr_name not in {
                    "__init__", "__iter__", "__len__", "__getitem__", "__setitem__",
                    "__contains__", "__repr__", "__abs__", "__hash__", "__bool__",
                    "__add__", "__radd__", "__sub__", "__rsub__", "__mul__", "__rmul__",
                    "__truediv__", "__rtruediv__", "__mod__", "__and__", "__or__",
                    "__xor__", "__lshift__", "__rshift__", "__invert__", "__neg__",
                    "__pos__", "__iadd__", "__imod__", "__eq__", "__ne__",
                    "__lt__", "__le__", "__gt__", "__ge__",
                }:
                    continue
                raw_attr = obj.__dict__[attr_name]
                if isinstance(raw_attr, property) or type(raw_attr).__name__ in {
                    "nb_getset", "getset_descriptor",
                }:
                    cls_body.append(f"{indent}    @property")
                    cls_body.append(f"{indent}    def {attr_name}(self) -> Any: ...")
                    if getattr(raw_attr, "fset", None) is not None:
                        cls_body.append(f"{indent}    @{attr_name}.setter")
                        cls_body.append(f"{indent}    def {attr_name}(self, value: Any) -> None: ...")
                elif isinstance(raw_attr, staticmethod):
                    sigs = parse_signatures(raw_attr.__func__, attr_name, is_method=False)
                    cls_body.extend(
                        emit_pyi_function(sigs, attr_name, indent + "    ", static=True, is_method=False)
                    )
                elif callable(raw_attr) or type(raw_attr).__name__ in {"nb_method", "nb_func"}:
                    sigs = parse_signatures(raw_attr, attr_name, is_method=True)
                    cls_body.extend(
                        emit_pyi_function(sigs, attr_name, indent + "    ", static=False, is_method=True)
                    )
                else:
                    cls_body.append(f"{indent}    {attr_name}: Any")
            lines.extend(cls_body or [f"{indent}    ..."])
            lines.append("")
        elif callable(obj):
            sigs = parse_signatures(obj, name, is_method=False)
            lines.extend(emit_pyi_function(sigs, name, indent, static=False, is_method=False))
            lines.append("")
        else:
            tp_name = type(obj).__name__
            if tp_name not in {"int", "float", "bool", "str"}:
                tp_name = "Any"
            lines.append(f"{indent}{sanitize_ident(name)}: {tp_name}")

    return lines


def generate_pyi(mod: Any) -> str:
    header = [
        "# Generated by pyrabind_stubgen.py",
        "from __future__ import annotations",
        "",
        "import enum",
        "from typing import Any, overload",
        "",
    ]
    return "\n".join(header + render_module_pyi(mod)).rstrip() + "\n"


def generate_r_wrappers(mod: Any, mod_name: str) -> str:
    lines: list[str] = [
        f"# Generated by pyrabind_stubgen.py for module '{mod_name}'",
        "#",
        f"#' Internal loaded environment for {mod_name}",
        ".pb_mod_env <- new.env(parent = emptyenv())",
        "",
        f"#' Initialize {mod_name} bindings from shared library",
        "#' @param runtime_path Path to pyrabind.R",
        "#' @param lib_path Path to compiled shared library",
        "#' @export",
        f"{sanitize_r_ident(mod_name + '_init')} <- function(runtime_path, lib_path) {{",
        "  source(runtime_path, local = TRUE)",
        "  .pb_mod_env$ns <- pyrabind_load(lib_path)",
        "  invisible(.pb_mod_env$ns)",
        "}",
        "",
    ]

    for name in sorted(n for n in dir(mod) if not n.startswith("__")):
        obj = getattr(mod, name)
        r_sym = sanitize_r_ident(name)
        if callable(obj) and not is_exception_class(obj) and not is_enum_class(obj):
            doc = (getattr(obj, "__doc__", None) or f"Wrapper for {name}").splitlines()[0]
            lines.extend([
                f"#' {doc}",
                "#' @param ... Arguments forwarded to C++ binding.",
                "#' @export",
                f"{r_sym} <- function(...) {{",
                f'  if (is.null(.pb_mod_env$ns)) stop("Call {mod_name}_init() first")',
                f'  .pb_mod_env$ns[["{name}"]](...)',
                "}",
                "",
            ])
        elif not isinstance(obj, types.ModuleType) and not isinstance(obj, type):
            lines.extend([
                f"#' Constant {name}",
                "#' @export",
                f"{r_sym} <- function() {{",
                f'  if (is.null(.pb_mod_env$ns)) stop("Call {mod_name}_init() first")',
                f'  .pb_mod_env$ns[["{name}"]]',
                "}",
                "",
            ])
    return "\n".join(lines).rstrip() + "\n"


def main() -> int:
    parser = argparse.ArgumentParser(description="Generate .pyi stubs and R wrappers for pyrabind")
    parser.add_argument("--module", required=True, help="Compiled Python module name")
    parser.add_argument("--python-path", default=None, help="Directory added to sys.path")
    parser.add_argument("--pyi-out", default=None, help="Output .pyi stub path")
    parser.add_argument("--r-out", default=None, help="Output .R wrapper path")
    parser.add_argument("--verify", action="store_true", help="Parse generated .pyi and .R outputs")
    parser.add_argument("--rscript", default=None, help="Optional Rscript path to parse .R output")
    args = parser.parse_args()

    if args.python_path:
        sys.path.insert(0, args.python_path)
    mod = importlib.import_module(args.module)

    if args.pyi_out:
        p = pathlib.Path(args.pyi_out)
        p.parent.mkdir(parents=True, exist_ok=True)
        pyi_text = generate_pyi(mod)
        p.write_text(pyi_text, encoding="utf-8")
        if args.verify:
            ast.parse(pyi_text)

    if args.r_out:
        p = pathlib.Path(args.r_out)
        p.parent.mkdir(parents=True, exist_ok=True)
        r_text = generate_r_wrappers(mod, args.module)
        p.write_text(r_text, encoding="utf-8")
        if args.verify and args.rscript:
            subprocess.check_call(
                [args.rscript, "-e", f"parse(file = '{p.as_posix()}')"],
                stdout=subprocess.DEVNULL,
            )

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
