#pragma once

#include "nanorbind/dataframe_types.hpp"

#include <nanobind/nanobind.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <variant>
#include <vector>

namespace nanorbind::py_detail {

struct PyRefGuard {
    PyObject* ptr = nullptr;

    PyRefGuard() = default;
    explicit PyRefGuard(PyObject* p) noexcept : ptr(p) {}
    ~PyRefGuard() { Py_XDECREF(ptr); }

    PyRefGuard(const PyRefGuard&) = delete;
    PyRefGuard& operator=(const PyRefGuard&) = delete;
    PyRefGuard(PyRefGuard&& other) noexcept : ptr(other.ptr) { other.ptr = nullptr; }
    PyRefGuard& operator=(PyRefGuard&& other) noexcept {
        if (this != &other) {
            Py_XDECREF(ptr);
            ptr = other.ptr;
            other.ptr = nullptr;
        }
        return *this;
    }

    PyObject* release() noexcept {
        PyObject* out = ptr;
        ptr = nullptr;
        return out;
    }
};

struct PyBufGuard {
    Py_buffer view{};
    bool active = false;

    PyBufGuard() = default;
    ~PyBufGuard() {
        if (active) {
            PyBuffer_Release(&view);
        }
    }
    PyBufGuard(const PyBufGuard&) = delete;
    PyBufGuard& operator=(const PyBufGuard&) = delete;
};

struct CachedPyDataApis {
    PyObject* np_empty = nullptr;
    PyObject* pd_dataframe = nullptr;
    bool np_checked = false;
    bool pd_checked = false;
};

inline CachedPyDataApis& cached_apis() noexcept {
    static CachedPyDataApis apis;
    return apis;
}

inline PyObject* get_numpy_empty() noexcept {
    auto& st = cached_apis();
    if (!st.np_checked) {
        st.np_checked = true;
        PyObject* np_mod = PyImport_ImportModule("numpy");
        if (np_mod != nullptr) {
            st.np_empty = PyObject_GetAttrString(np_mod, "empty");
            Py_DECREF(np_mod);
        }
        if (PyErr_Occurred()) {
            PyErr_Clear();
        }
    }
    return st.np_empty;
}

inline PyObject* get_pandas_dataframe_cls() noexcept {
    auto& st = cached_apis();
    if (!st.pd_checked) {
        st.pd_checked = true;
        PyObject* pd_mod = PyImport_ImportModule("pandas");
        if (pd_mod != nullptr) {
            st.pd_dataframe = PyObject_GetAttrString(pd_mod, "DataFrame");
            Py_DECREF(pd_mod);
        }
        if (PyErr_Occurred()) {
            PyErr_Clear();
        }
    }
    return st.pd_dataframe;
}

inline bool py_str_to_std(PyObject* obj, std::string& out) noexcept {
    Py_ssize_t len = 0;
    const char* data = PyUnicode_AsUTF8AndSize(obj, &len);
    if (data == nullptr) {
        PyErr_Clear();
        return false;
    }
    out.assign(data, static_cast<std::size_t>(len));
    return true;
}

inline bool extract_sequence_column(PyObject* seq_obj, column_data& out) noexcept {
    PyRefGuard seq(PySequence_Fast(seq_obj, "expected column sequence"));
    if (seq.ptr == nullptr) {
        PyErr_Clear();
        return false;
    }
    const Py_ssize_t n = PySequence_Fast_GET_SIZE(seq.ptr);
    PyObject** items = PySequence_Fast_ITEMS(seq.ptr);
    if (n == 0) {
        out = std::vector<double>{};
        return true;
    }

    bool all_bool = true;
    bool all_int = true;
    bool all_num = true;
    bool all_str = true;
    for (Py_ssize_t i = 0; i < n; ++i) {
        PyObject* item = items[i];
        if (item == Py_True || item == Py_False) {
            all_str = false;
            continue;
        }
        all_bool = false;
        if (PyLong_Check(item)) {
            all_str = false;
            continue;
        }
        all_int = false;
        if (PyFloat_Check(item)) {
            all_str = false;
            continue;
        }
        all_num = false;
        if (!PyUnicode_Check(item)) {
            all_str = false;
        }
    }

    if (all_bool) {
        std::vector<bool> vec(static_cast<std::size_t>(n), false);
        for (Py_ssize_t i = 0; i < n; ++i) {
            vec[static_cast<std::size_t>(i)] = (items[i] == Py_True);
        }
        out = std::move(vec);
        return true;
    }
    if (all_int) {
        std::vector<std::int64_t> vec(static_cast<std::size_t>(n));
        for (Py_ssize_t i = 0; i < n; ++i) {
            int overflow = 0;
            const long long v = PyLong_AsLongLongAndOverflow(items[i], &overflow);
            if (overflow != 0 || (v == -1 && PyErr_Occurred())) {
                PyErr_Clear();
                return false;
            }
            vec[static_cast<std::size_t>(i)] = static_cast<std::int64_t>(v);
        }
        out = std::move(vec);
        return true;
    }
    if (all_num) {
        std::vector<double> vec(static_cast<std::size_t>(n));
        for (Py_ssize_t i = 0; i < n; ++i) {
            const double v = PyFloat_AsDouble(items[i]);
            if (v == -1.0 && PyErr_Occurred()) {
                PyErr_Clear();
                return false;
            }
            vec[static_cast<std::size_t>(i)] = v;
        }
        out = std::move(vec);
        return true;
    }
    if (all_str) {
        std::vector<std::string> vec(static_cast<std::size_t>(n));
        for (Py_ssize_t i = 0; i < n; ++i) {
            if (!py_str_to_std(items[i], vec[static_cast<std::size_t>(i)])) {
                return false;
            }
        }
        out = std::move(vec);
        return true;
    }
    return false;
}

inline bool extract_column(PyObject* col_obj, column_data& out) noexcept {
    PyRefGuard owned_arr;
    PyObject* target = col_obj;
    if (PyObject_CheckBuffer(col_obj) == 0) {
        if (PyObject_HasAttrString(col_obj, "to_numpy")) {
            owned_arr.ptr = PyObject_CallMethod(col_obj, "to_numpy", nullptr);
            if (owned_arr.ptr == nullptr) {
                PyErr_Clear();
                return false;
            }
            target = owned_arr.ptr;
        } else if (PyObject_HasAttrString(col_obj, "__array__")) {
            owned_arr.ptr = PyObject_CallMethod(col_obj, "__array__", nullptr);
            if (owned_arr.ptr == nullptr) {
                PyErr_Clear();
                return false;
            }
            target = owned_arr.ptr;
        }
    }

    if (PyObject_CheckBuffer(target) != 0) {
        PyBufGuard bg;
        if (PyObject_GetBuffer(target, &bg.view, PyBUF_ND | PyBUF_STRIDES | PyBUF_FORMAT) == 0) {
            bg.active = true;
            if (bg.view.ndim == 1) {
                const Py_ssize_t n = bg.view.shape[0];
                const auto* base = static_cast<const char*>(bg.view.buf);
                const Py_ssize_t stride = bg.view.strides[0];
                const char fmt = bg.view.format != nullptr ? bg.view.format[0] : '\0';
                const Py_ssize_t itemsize = bg.view.itemsize;

                if (fmt == 'd' && itemsize == static_cast<Py_ssize_t>(sizeof(double))) {
                    std::vector<double> vec(static_cast<std::size_t>(n));
                    if (stride == itemsize && n > 0) {
                        std::memcpy(vec.data(), base, static_cast<std::size_t>(n) * sizeof(double));
                    } else {
                        for (Py_ssize_t i = 0; i < n; ++i) {
                            vec[static_cast<std::size_t>(i)] =
                                *reinterpret_cast<const double*>(base + i * stride);
                        }
                    }
                    out = std::move(vec);
                    return true;
                }
                if (fmt == 'f' && itemsize == static_cast<Py_ssize_t>(sizeof(float))) {
                    std::vector<double> vec(static_cast<std::size_t>(n));
                    for (Py_ssize_t i = 0; i < n; ++i) {
                        vec[static_cast<std::size_t>(i)] =
                            static_cast<double>(*reinterpret_cast<const float*>(base + i * stride));
                    }
                    out = std::move(vec);
                    return true;
                }
                if ((fmt == 'q' || (fmt == 'l' && sizeof(long) == 8)) && itemsize == 8) {
                    std::vector<std::int64_t> vec(static_cast<std::size_t>(n));
                    if (stride == itemsize && n > 0) {
                        std::memcpy(vec.data(), base,
                                    static_cast<std::size_t>(n) * sizeof(std::int64_t));
                    } else {
                        for (Py_ssize_t i = 0; i < n; ++i) {
                            vec[static_cast<std::size_t>(i)] =
                                *reinterpret_cast<const std::int64_t*>(base + i * stride);
                        }
                    }
                    out = std::move(vec);
                    return true;
                }
                if ((fmt == 'i' || (fmt == 'l' && sizeof(long) == 4)) && itemsize == 4) {
                    std::vector<std::int32_t> vec(static_cast<std::size_t>(n));
                    if (stride == itemsize && n > 0) {
                        std::memcpy(vec.data(), base,
                                    static_cast<std::size_t>(n) * sizeof(std::int32_t));
                    } else {
                        for (Py_ssize_t i = 0; i < n; ++i) {
                            vec[static_cast<std::size_t>(i)] =
                                *reinterpret_cast<const std::int32_t*>(base + i * stride);
                        }
                    }
                    out = std::move(vec);
                    return true;
                }
                if (fmt == '?' && itemsize == 1) {
                    std::vector<bool> vec(static_cast<std::size_t>(n), false);
                    for (Py_ssize_t i = 0; i < n; ++i) {
                        vec[static_cast<std::size_t>(i)] =
                            (*reinterpret_cast<const unsigned char*>(base + i * stride) != 0);
                    }
                    out = std::move(vec);
                    return true;
                }
                if (fmt == 'O' && itemsize == static_cast<Py_ssize_t>(sizeof(PyObject*))) {
                    std::vector<std::string> vec(static_cast<std::size_t>(n));
                    bool ok = true;
                    for (Py_ssize_t i = 0; i < n; ++i) {
                        PyObject* elt = *reinterpret_cast<PyObject* const*>(base + i * stride);
                        if (elt == nullptr || !PyUnicode_Check(elt) ||
                            !py_str_to_std(elt, vec[static_cast<std::size_t>(i)])) {
                            ok = false;
                            break;
                        }
                    }
                    if (ok) {
                        out = std::move(vec);
                        return true;
                    }
                }
            }
        } else {
            PyErr_Clear();
        }
    }

    return extract_sequence_column(target, out);
}

inline PyObject* column_to_python(const column_data& col) noexcept {
    PyObject* np_empty = get_numpy_empty();
    return std::visit(
        [&](const auto& vec) -> PyObject* {
            using VecT = std::decay_t<decltype(vec)>;
            const Py_ssize_t n = static_cast<Py_ssize_t>(vec.size());
            if constexpr (std::is_same_v<VecT, std::vector<std::string>>) {
                PyRefGuard lst(PyList_New(n));
                if (lst.ptr == nullptr) {
                    return nullptr;
                }
                for (Py_ssize_t i = 0; i < n; ++i) {
                    const auto& s = vec[static_cast<std::size_t>(i)];
                    PyObject* py_s =
                        PyUnicode_FromStringAndSize(s.data(), static_cast<Py_ssize_t>(s.size()));
                    if (py_s == nullptr) {
                        return nullptr;
                    }
                    PyList_SET_ITEM(lst.ptr, i, py_s);
                }
                return lst.release();
            } else if constexpr (std::is_same_v<VecT, std::vector<bool>>) {
                if (np_empty != nullptr) {
                    PyRefGuard arr(PyObject_CallFunction(np_empty, "ns", n, "?"));
                    if (arr.ptr != nullptr) {
                        PyBufGuard bg;
                        if (PyObject_GetBuffer(arr.ptr, &bg.view, PyBUF_WRITABLE | PyBUF_SIMPLE) ==
                            0) {
                            bg.active = true;
                            auto* dst = static_cast<unsigned char*>(bg.view.buf);
                            for (Py_ssize_t i = 0; i < n; ++i) {
                                dst[i] = vec[static_cast<std::size_t>(i)] ? 1 : 0;
                            }
                            return arr.release();
                        }
                    }
                    PyErr_Clear();
                }
                PyRefGuard lst(PyList_New(n));
                if (lst.ptr == nullptr) {
                    return nullptr;
                }
                for (Py_ssize_t i = 0; i < n; ++i) {
                    PyObject* py_b = PyBool_FromLong(vec[static_cast<std::size_t>(i)] ? 1 : 0);
                    PyList_SET_ITEM(lst.ptr, i, py_b);
                }
                return lst.release();
            } else {
                using T = typename VecT::value_type;
                const char* fmt = nullptr;
                if constexpr (std::is_same_v<T, double>) {
                    fmt = "d";
                } else if constexpr (std::is_same_v<T, std::int64_t>) {
                    fmt = "q";
                } else if constexpr (std::is_same_v<T, std::int32_t>) {
                    fmt = "i";
                }
                if (np_empty != nullptr && fmt != nullptr) {
                    PyRefGuard arr(PyObject_CallFunction(np_empty, "ns", n, fmt));
                    if (arr.ptr != nullptr) {
                        PyBufGuard bg;
                        if (PyObject_GetBuffer(arr.ptr, &bg.view, PyBUF_WRITABLE | PyBUF_SIMPLE) ==
                            0) {
                            bg.active = true;
                            if (n > 0) {
                                std::memcpy(bg.view.buf, vec.data(),
                                            static_cast<std::size_t>(n) * sizeof(T));
                            }
                            return arr.release();
                        }
                    }
                    PyErr_Clear();
                }
                PyRefGuard lst(PyList_New(n));
                if (lst.ptr == nullptr) {
                    return nullptr;
                }
                for (Py_ssize_t i = 0; i < n; ++i) {
                    PyObject* item = nullptr;
                    if constexpr (std::is_same_v<T, double>) {
                        item = PyFloat_FromDouble(vec[static_cast<std::size_t>(i)]);
                    } else {
                        item = PyLong_FromLongLong(
                            static_cast<long long>(vec[static_cast<std::size_t>(i)]));
                    }
                    if (item == nullptr) {
                        return nullptr;
                    }
                    PyList_SET_ITEM(lst.ptr, i, item);
                }
                return lst.release();
            }
        },
        col);
}

} // namespace nanorbind::py_detail

namespace nanobind::detail {

template <> struct type_caster<nanorbind::data_frame> {
    NB_TYPE_CASTER(nanorbind::data_frame, const_name("pandas.DataFrame"))

    bool from_python(handle src, uint32_t, cleanup_list*) noexcept {
        value.names.clear();
        value.columns.clear();
        PyObject* obj = src.ptr();

        if (PyDict_Check(obj)) {
            PyObject* key = nullptr;
            PyObject* val = nullptr;
            Py_ssize_t pos = 0;
            std::size_t expected_rows = 0;
            bool have_rows = false;
            while (PyDict_Next(obj, &pos, &key, &val)) {
                if (!PyUnicode_Check(key)) {
                    return false;
                }
                std::string col_name;
                nanorbind::column_data col;
                if (!nanorbind::py_detail::py_str_to_std(key, col_name) ||
                    !nanorbind::py_detail::extract_column(val, col)) {
                    return false;
                }
                const std::size_t len = nanorbind::column_size(col);
                if (!have_rows) {
                    expected_rows = len;
                    have_rows = true;
                } else if (len != expected_rows) {
                    return false;
                }
                value.names.push_back(std::move(col_name));
                value.columns.push_back(std::move(col));
            }
            return true;
        }

        if (PyObject_HasAttrString(obj, "columns") && PyMapping_Check(obj)) {
            nanorbind::py_detail::PyRefGuard cols_attr(PyObject_GetAttrString(obj, "columns"));
            if (cols_attr.ptr == nullptr) {
                PyErr_Clear();
                return false;
            }
            nanorbind::py_detail::PyRefGuard cols_seq(
                PySequence_Fast(cols_attr.ptr, "expected DataFrame.columns sequence"));
            if (cols_seq.ptr == nullptr) {
                PyErr_Clear();
                return false;
            }
            const Py_ssize_t ncols = PySequence_Fast_GET_SIZE(cols_seq.ptr);
            PyObject** col_keys = PySequence_Fast_ITEMS(cols_seq.ptr);
            std::size_t expected_rows = 0;
            bool have_rows = false;

            for (Py_ssize_t i = 0; i < ncols; ++i) {
                std::string col_name;
                if (!nanorbind::py_detail::py_str_to_std(col_keys[i], col_name)) {
                    return false;
                }
                nanorbind::py_detail::PyRefGuard col_obj(PyObject_GetItem(obj, col_keys[i]));
                if (col_obj.ptr == nullptr) {
                    PyErr_Clear();
                    return false;
                }
                nanorbind::column_data col;
                if (!nanorbind::py_detail::extract_column(col_obj.ptr, col)) {
                    return false;
                }
                const std::size_t len = nanorbind::column_size(col);
                if (!have_rows) {
                    expected_rows = len;
                    have_rows = true;
                } else if (len != expected_rows) {
                    return false;
                }
                value.names.push_back(std::move(col_name));
                value.columns.push_back(std::move(col));
            }
            return true;
        }

        return false;
    }

    static handle from_cpp(const nanorbind::data_frame& df, rv_policy, cleanup_list*) noexcept {
        if (df.names.size() != df.columns.size()) {
            PyErr_SetString(PyExc_ValueError, "data_frame names and columns size mismatch");
            return {};
        }

        nanorbind::py_detail::PyRefGuard py_dict(PyDict_New());
        if (py_dict.ptr == nullptr) {
            return {};
        }
        for (std::size_t i = 0; i < df.columns.size(); ++i) {
            nanorbind::py_detail::PyRefGuard col_py(
                nanorbind::py_detail::column_to_python(df.columns[i]));
            if (col_py.ptr == nullptr) {
                return {};
            }
            if (PyDict_SetItemString(py_dict.ptr, df.names[i].c_str(), col_py.ptr) < 0) {
                return {};
            }
        }

        PyObject* pd_cls = nanorbind::py_detail::get_pandas_dataframe_cls();
        if (pd_cls != nullptr) {
            PyObject* pd_obj = PyObject_CallFunctionObjArgs(pd_cls, py_dict.ptr, nullptr);
            if (pd_obj != nullptr) {
                return pd_obj;
            }
            return {};
        }
        return py_dict.release();
    }
};

} // namespace nanobind::detail
