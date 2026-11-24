#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include "bench_kernels.hpp"

#include <cstdint>
#include <cstring>

namespace {

template <typename FnPtr> PyCFunction as_pycfunction(FnPtr fn) noexcept {
    static_assert(sizeof(FnPtr) == sizeof(PyCFunction));
    PyCFunction out = nullptr;
    std::memcpy(&out, &fn, sizeof(out));
    return out;
}

template <typename FnPtr> void* as_slot_ptr(FnPtr fn) noexcept {
    static_assert(sizeof(FnPtr) == sizeof(void*));
    void* out = nullptr;
    std::memcpy(&out, &fn, sizeof(out));
    return out;
}

struct BufferGuard {
    Py_buffer view{};
    bool active = false;

    BufferGuard() = default;
    ~BufferGuard() { release(); }

    BufferGuard(const BufferGuard&) = delete;
    BufferGuard& operator=(const BufferGuard&) = delete;

    bool acquire(PyObject* obj, int flags) {
        release();
        if (PyObject_GetBuffer(obj, &view, flags) < 0) {
            return false;
        }
        active = true;
        return true;
    }

    void release() {
        if (active) {
            PyBuffer_Release(&view);
            active = false;
        }
    }
};

struct RawPointObject {
    PyObject_HEAD double x;
    double y;
};

struct RawModuleState {
    PyTypeObject* point_type = nullptr;
    PyObject* np_empty = nullptr;
    PyObject* pd_dataframe = nullptr;
    bool np_checked = false;
    bool pd_checked = false;
};

RawModuleState& raw_state() {
    static RawModuleState st;
    return st;
}

PyObject* get_np_empty() {
    auto& st = raw_state();
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

PyObject* get_pd_dataframe() {
    auto& st = raw_state();
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

int raw_point_init(PyObject* self_obj, PyObject* args, PyObject* kwds) {
    if (kwds != nullptr && PyDict_Size(kwds) > 0) {
        PyErr_SetString(PyExc_TypeError, "BenchPoint does not accept keyword arguments");
        return -1;
    }
    double x = 0.0;
    double y = 0.0;
    if (!PyArg_ParseTuple(args, "dd", &x, &y)) {
        return -1;
    }
    auto* self = reinterpret_cast<RawPointObject*>(self_obj);
    self->x = x;
    self->y = y;
    return 0;
}

PyObject* raw_point_dot(PyObject* self_obj, PyObject* other_obj) {
    if (!PyObject_TypeCheck(other_obj, raw_state().point_type)) {
        PyErr_SetString(PyExc_TypeError, "expected BenchPoint");
        return nullptr;
    }
    const auto* a = reinterpret_cast<const RawPointObject*>(self_obj);
    const auto* b = reinterpret_cast<const RawPointObject*>(other_obj);
    return PyFloat_FromDouble(a->x * b->x + a->y * b->y);
}

bool acquire_double_buffer(PyObject* obj, BufferGuard& guard, int ndim) {
    PyObject* target = obj;
    PyObject* owned_arr = nullptr;
    if (PyObject_CheckBuffer(obj) == 0) {
        if (PyObject_HasAttrString(obj, "__array__")) {
            owned_arr = PyObject_CallMethod(obj, "__array__", nullptr);
            if (owned_arr == nullptr) {
                return false;
            }
            target = owned_arr;
        } else {
            PyErr_SetString(PyExc_TypeError, "object does not expose buffer protocol");
            return false;
        }
    }
    const bool ok = guard.acquire(target, PyBUF_ND | PyBUF_STRIDES | PyBUF_FORMAT);
    Py_XDECREF(owned_arr);
    if (!ok) {
        return false;
    }
    if (guard.view.ndim != ndim) {
        PyErr_SetString(PyExc_ValueError, "unexpected buffer dimensionality");
        return false;
    }
    if (guard.view.itemsize != static_cast<Py_ssize_t>(sizeof(double)) ||
        guard.view.format == nullptr || guard.view.format[0] != 'd') {
        PyErr_SetString(PyExc_TypeError, "expected float64 buffer");
        return false;
    }
    return true;
}

PyObject* raw_py_noop(PyObject*, PyObject* const*, Py_ssize_t nargs) {
    if (nargs != 0) {
        PyErr_SetString(PyExc_TypeError, "noop() takes 0 arguments");
        return nullptr;
    }
    pyrabind_bench::noop_kernel();
    Py_RETURN_NONE;
}

PyObject* raw_py_add_i64(PyObject*, PyObject* const* args, Py_ssize_t nargs) {
    if (nargs != 2) {
        PyErr_SetString(PyExc_TypeError, "add_i64() takes 2 arguments");
        return nullptr;
    }
    const long long a = PyLong_AsLongLong(args[0]);
    if (a == -1 && PyErr_Occurred()) {
        return nullptr;
    }
    const long long b = PyLong_AsLongLong(args[1]);
    if (b == -1 && PyErr_Occurred()) {
        return nullptr;
    }
    return PyLong_FromLongLong(static_cast<long long>(pyrabind_bench::add_i64_kernel(a, b)));
}

PyObject* raw_py_dot_view(PyObject*, PyObject* const* args, Py_ssize_t nargs) {
    if (nargs != 2) {
        PyErr_SetString(PyExc_TypeError, "dot_view() takes 2 arguments");
        return nullptr;
    }
    BufferGuard ga;
    BufferGuard gb;
    if (!acquire_double_buffer(args[0], ga, 1) || !acquire_double_buffer(args[1], gb, 1)) {
        return nullptr;
    }
    const Py_ssize_t n = ga.view.shape[0];
    if (gb.view.shape[0] != n) {
        PyErr_SetString(PyExc_ValueError, "arrays must have identical length");
        return nullptr;
    }
    constexpr Py_ssize_t kStride = static_cast<Py_ssize_t>(sizeof(double));
    double acc = 0.0;
    if (ga.view.strides[0] == kStride && gb.view.strides[0] == kStride && n > 0) {
        acc = pyrabind_bench::dot_contiguous_kernel(static_cast<const double*>(ga.view.buf),
                                                    static_cast<const double*>(gb.view.buf),
                                                    static_cast<std::size_t>(n));
    } else {
        const auto* pa = static_cast<const char*>(ga.view.buf);
        const auto* pb = static_cast<const char*>(gb.view.buf);
        const Py_ssize_t sa = ga.view.strides[0];
        const Py_ssize_t sb = gb.view.strides[0];
        for (Py_ssize_t i = 0; i < n; ++i) {
            const double va = *reinterpret_cast<const double*>(pa + i * sa);
            const double vb = *reinterpret_cast<const double*>(pb + i * sb);
            acc += va * vb;
        }
    }
    return PyFloat_FromDouble(acc);
}

PyObject* make_empty_f64_array(Py_ssize_t n) {
    PyObject* np_empty = get_np_empty();
    if (np_empty == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "numpy is required for raw_py_bench ndarray output");
        return nullptr;
    }
    return PyObject_CallFunction(np_empty, "ns", n, "d");
}

PyObject* raw_py_scale_ndarray(PyObject*, PyObject* const* args, Py_ssize_t nargs) {
    if (nargs != 3) {
        PyErr_SetString(PyExc_TypeError, "scale_ndarray() takes 3 arguments");
        return nullptr;
    }
    BufferGuard ga;
    if (!acquire_double_buffer(args[0], ga, 1)) {
        return nullptr;
    }
    const double factor = PyFloat_AsDouble(args[1]);
    if (factor == -1.0 && PyErr_Occurred()) {
        return nullptr;
    }
    const double offset = PyFloat_AsDouble(args[2]);
    if (offset == -1.0 && PyErr_Occurred()) {
        return nullptr;
    }

    const Py_ssize_t n = ga.view.shape[0];
    PyObject* out = make_empty_f64_array(n);
    if (out == nullptr) {
        return nullptr;
    }
    BufferGuard gout;
    if (!gout.acquire(out, PyBUF_WRITABLE | PyBUF_SIMPLE)) {
        Py_DECREF(out);
        return nullptr;
    }
    auto* dst = static_cast<double*>(gout.view.buf);
    constexpr Py_ssize_t kStride = static_cast<Py_ssize_t>(sizeof(double));
    if (ga.view.strides[0] == kStride && n > 0) {
        pyrabind_bench::scale_contiguous_kernel(static_cast<const double*>(ga.view.buf), dst,
                                                static_cast<std::size_t>(n), factor, offset);
    } else {
        const auto* pa = static_cast<const char*>(ga.view.buf);
        const Py_ssize_t sa = ga.view.strides[0];
        for (Py_ssize_t i = 0; i < n; ++i) {
            dst[i] = (*reinterpret_cast<const double*>(pa + i * sa)) * factor + offset;
        }
    }
    gout.release();
    return out;
}

PyObject* raw_py_mat_sum_view(PyObject*, PyObject* arg) {
    BufferGuard gm;
    if (!acquire_double_buffer(arg, gm, 2)) {
        return nullptr;
    }
    const Py_ssize_t rows = gm.view.shape[0];
    const Py_ssize_t cols = gm.view.shape[1];
    const Py_ssize_t rs = gm.view.strides[0];
    const Py_ssize_t cs = gm.view.strides[1];
    constexpr Py_ssize_t kStride = static_cast<Py_ssize_t>(sizeof(double));
    double acc = 0.0;
    if (rs == cols * kStride && cs == kStride) {
        acc = pyrabind_bench::sum_contiguous_kernel(static_cast<const double*>(gm.view.buf),
                                                    static_cast<std::size_t>(rows * cols));
    } else {
        const auto* base = static_cast<const char*>(gm.view.buf);
        for (Py_ssize_t r = 0; r < rows; ++r) {
            const char* row_ptr = base + r * rs;
            for (Py_ssize_t c = 0; c < cols; ++c) {
                acc += *reinterpret_cast<const double*>(row_ptr + c * cs);
            }
        }
    }
    return PyFloat_FromDouble(acc);
}

PyObject* extract_col_numpy(PyObject* col_obj) {
    if (PyObject_CheckBuffer(col_obj)) {
        Py_INCREF(col_obj);
        return col_obj;
    }
    if (PyObject_HasAttrString(col_obj, "to_numpy")) {
        return PyObject_CallMethod(col_obj, "to_numpy", nullptr);
    }
    if (PyObject_HasAttrString(col_obj, "__array__")) {
        return PyObject_CallMethod(col_obj, "__array__", nullptr);
    }
    Py_INCREF(col_obj);
    return col_obj;
}

PyObject* raw_py_df_transform(PyObject*, PyObject* df) {
    PyObject* pd_cls = get_pd_dataframe();
    if (pd_cls == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "pandas is required for raw_py_bench df_transform");
        return nullptr;
    }
    PyObject* id_series = PyMapping_GetItemString(df, "id");
    if (id_series == nullptr) {
        return nullptr;
    }
    PyObject* x_series = PyMapping_GetItemString(df, "x");
    if (x_series == nullptr) {
        Py_DECREF(id_series);
        return nullptr;
    }
    PyObject* tag_series = PyMapping_GetItemString(df, "tag");
    if (tag_series == nullptr) {
        Py_DECREF(id_series);
        Py_DECREF(x_series);
        return nullptr;
    }

    PyObject* id_arr = extract_col_numpy(id_series);
    PyObject* x_arr = extract_col_numpy(x_series);
    Py_DECREF(id_series);
    Py_DECREF(x_series);
    if (id_arr == nullptr || x_arr == nullptr) {
        Py_XDECREF(id_arr);
        Py_XDECREF(x_arr);
        Py_DECREF(tag_series);
        return nullptr;
    }

    BufferGuard gid;
    BufferGuard gx;
    if (!gid.acquire(id_arr, PyBUF_ND | PyBUF_STRIDES | PyBUF_FORMAT) ||
        !acquire_double_buffer(x_arr, gx, 1)) {
        Py_DECREF(id_arr);
        Py_DECREF(x_arr);
        Py_DECREF(tag_series);
        return nullptr;
    }
    const Py_ssize_t n = gx.view.shape[0];
    if (gid.view.ndim != 1 || gid.view.shape[0] != n) {
        Py_DECREF(id_arr);
        Py_DECREF(x_arr);
        Py_DECREF(tag_series);
        PyErr_SetString(PyExc_ValueError, "mismatched column lengths");
        return nullptr;
    }

    PyObject* y_arr = make_empty_f64_array(n);
    if (y_arr == nullptr) {
        Py_DECREF(id_arr);
        Py_DECREF(x_arr);
        Py_DECREF(tag_series);
        return nullptr;
    }
    BufferGuard gy;
    if (!gy.acquire(y_arr, PyBUF_WRITABLE | PyBUF_SIMPLE)) {
        Py_DECREF(id_arr);
        Py_DECREF(x_arr);
        Py_DECREF(tag_series);
        Py_DECREF(y_arr);
        return nullptr;
    }

    auto* dst = static_cast<double*>(gy.view.buf);
    const auto* x_base = static_cast<const char*>(gx.view.buf);
    const Py_ssize_t xs = gx.view.strides[0];
    const auto* id_base = static_cast<const char*>(gid.view.buf);
    const Py_ssize_t ids = gid.view.strides[0];
    const char fmt = gid.view.format != nullptr ? gid.view.format[0] : '\0';

    if ((fmt == 'q' || (fmt == 'l' && sizeof(long) == 8)) && gid.view.itemsize == 8) {
        if (xs == static_cast<Py_ssize_t>(sizeof(double)) &&
            ids == static_cast<Py_ssize_t>(sizeof(std::int64_t))) {
            const auto* xp = static_cast<const double*>(gx.view.buf);
            const auto* ip = static_cast<const std::int64_t*>(gid.view.buf);
            for (Py_ssize_t i = 0; i < n; ++i) {
                dst[i] = xp[i] * 2.0 + static_cast<double>(ip[i]);
            }
        } else {
            for (Py_ssize_t i = 0; i < n; ++i) {
                const double xv = *reinterpret_cast<const double*>(x_base + i * xs);
                const std::int64_t iv = *reinterpret_cast<const std::int64_t*>(id_base + i * ids);
                dst[i] = xv * 2.0 + static_cast<double>(iv);
            }
        }
    } else if (fmt == 'i' && gid.view.itemsize == 4) {
        for (Py_ssize_t i = 0; i < n; ++i) {
            const double xv = *reinterpret_cast<const double*>(x_base + i * xs);
            const std::int32_t iv = *reinterpret_cast<const std::int32_t*>(id_base + i * ids);
            dst[i] = xv * 2.0 + static_cast<double>(iv);
        }
    } else {
        gy.release();
        Py_DECREF(id_arr);
        Py_DECREF(x_arr);
        Py_DECREF(tag_series);
        Py_DECREF(y_arr);
        PyErr_SetString(PyExc_TypeError, "unsupported id dtype in raw_py_bench");
        return nullptr;
    }

    gy.release();
    gid.release();
    gx.release();

    PyObject* py_dict = PyDict_New();
    if (py_dict == nullptr) {
        Py_DECREF(id_arr);
        Py_DECREF(x_arr);
        Py_DECREF(tag_series);
        Py_DECREF(y_arr);
        return nullptr;
    }
    PyDict_SetItemString(py_dict, "id", id_arr);
    PyDict_SetItemString(py_dict, "x", x_arr);
    PyDict_SetItemString(py_dict, "y", y_arr);
    PyDict_SetItemString(py_dict, "tag", tag_series);
    Py_DECREF(id_arr);
    Py_DECREF(x_arr);
    Py_DECREF(tag_series);
    Py_DECREF(y_arr);

    PyObject* res = PyObject_CallOneArg(pd_cls, py_dict);
    Py_DECREF(py_dict);
    return res;
}

void raw_module_free(void*) {
    auto& st = raw_state();
    Py_CLEAR(st.np_empty);
    Py_CLEAR(st.pd_dataframe);
    Py_XDECREF(reinterpret_cast<PyObject*>(st.point_type));
    st.point_type = nullptr;
}

PyTypeObject* build_point_type() {
    static PyMethodDef point_methods[] = {
        {"dot", nullptr, METH_O, "Compute dot product with another BenchPoint"},
        {nullptr, nullptr, 0, nullptr},
    };
    point_methods[0].ml_meth = as_pycfunction(&raw_point_dot);

    PyType_Slot slots[] = {
        {Py_tp_new, as_slot_ptr(&PyType_GenericNew)},
        {Py_tp_init, as_slot_ptr(&raw_point_init)},
        {Py_tp_methods, point_methods},
        {0, nullptr},
    };

    PyType_Spec spec{
        "raw_py_bench.BenchPoint", sizeof(RawPointObject), 0, Py_TPFLAGS_DEFAULT, slots,
    };
    PyObject* t = PyType_FromSpec(&spec);
    return reinterpret_cast<PyTypeObject*>(t);
}

} // namespace

PyMODINIT_FUNC PyInit_raw_py_bench(void) {
    static PyModuleDef module_def{
        PyModuleDef_HEAD_INIT,
        "raw_py_bench",
        "Raw Python C-API benchmark baseline",
        -1,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        &raw_module_free,
    };

    PyObject* mod = PyModule_Create(&module_def);
    if (mod == nullptr) {
        return nullptr;
    }

    static PyMethodDef methods[] = {
        {"noop", nullptr, METH_FASTCALL, "No-op benchmark"},
        {"add_i64", nullptr, METH_FASTCALL, "Add two integers"},
        {"dot_view", nullptr, METH_FASTCALL, "Buffer-protocol 1D dot product"},
        {"scale_ndarray", nullptr, METH_FASTCALL, "Scale buffer into new ndarray"},
        {"mat_sum_view", nullptr, METH_O, "Sum 2D buffer"},
        {"df_transform", nullptr, METH_O, "Transform pandas DataFrame via C-API"},
        {nullptr, nullptr, 0, nullptr},
    };
    methods[0].ml_meth = as_pycfunction(&raw_py_noop);
    methods[1].ml_meth = as_pycfunction(&raw_py_add_i64);
    methods[2].ml_meth = as_pycfunction(&raw_py_dot_view);
    methods[3].ml_meth = as_pycfunction(&raw_py_scale_ndarray);
    methods[4].ml_meth = as_pycfunction(&raw_py_mat_sum_view);
    methods[5].ml_meth = as_pycfunction(&raw_py_df_transform);

    if (PyModule_AddFunctions(mod, methods) < 0) {
        Py_DECREF(mod);
        return nullptr;
    }

    PyTypeObject* pt_type = build_point_type();
    if (pt_type == nullptr) {
        Py_DECREF(mod);
        return nullptr;
    }
    raw_state().point_type = pt_type;
    if (PyModule_AddObjectRef(mod, "BenchPoint", reinterpret_cast<PyObject*>(pt_type)) < 0) {
        Py_DECREF(mod);
        return nullptr;
    }
    return mod;
}
