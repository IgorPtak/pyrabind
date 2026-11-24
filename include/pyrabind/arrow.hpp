#pragma once

#include "pyrabind/arrow_abi.hpp"
#include "pyrabind/dataframe_types.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#if defined(PYRABIND_TARGET_PYTHON)
#include <nanobind/nanobind.h>
#elif defined(PYRABIND_TARGET_R)
#include "pyrabind/r/caster.hpp"
#endif

namespace pyrabind {

namespace arrow_detail {

struct ColumnBuffers {
    column_data col_storage;
    std::vector<std::uint8_t> validity_bitmap;
    std::vector<std::uint8_t> bool_bitmap;
    std::vector<std::int32_t> utf8_offsets;
    std::string utf8_data;
    const void* buffer_ptrs[3]{};
};

struct BatchOwner {
    std::vector<std::string> names;
    std::vector<ColumnBuffers> cols;
    std::vector<ArrowSchema> child_schemas;
    std::vector<ArrowSchema*> child_schema_ptrs;
    std::vector<ArrowArray> child_arrays;
    std::vector<ArrowArray*> child_array_ptrs;
    const void* struct_buffers[1]{nullptr};
};

struct ChildSchemaPriv {
    std::shared_ptr<BatchOwner> owner;
};

struct ChildArrayPriv {
    std::shared_ptr<BatchOwner> owner;
};

struct RootSchemaPriv {
    std::shared_ptr<BatchOwner> owner;
};

struct RootArrayPriv {
    std::shared_ptr<BatchOwner> owner;
};

inline void release_child_schema(ArrowSchema* sch) {
    if (sch == nullptr || sch->release == nullptr) {
        return;
    }
    delete static_cast<ChildSchemaPriv*>(sch->private_data);
    sch->private_data = nullptr;
    sch->release = nullptr;
}

inline void release_child_array(ArrowArray* arr) {
    if (arr == nullptr || arr->release == nullptr) {
        return;
    }
    delete static_cast<ChildArrayPriv*>(arr->private_data);
    arr->private_data = nullptr;
    arr->release = nullptr;
}

inline void release_root_schema(ArrowSchema* sch) {
    if (sch == nullptr || sch->release == nullptr) {
        return;
    }
    for (std::int64_t i = 0; i < sch->n_children; ++i) {
        if (sch->children != nullptr && sch->children[i] != nullptr &&
            sch->children[i]->release != nullptr) {
            sch->children[i]->release(sch->children[i]);
        }
    }
    delete static_cast<RootSchemaPriv*>(sch->private_data);
    sch->private_data = nullptr;
    sch->release = nullptr;
}

inline void release_root_array(ArrowArray* arr) {
    if (arr == nullptr || arr->release == nullptr) {
        return;
    }
    for (std::int64_t i = 0; i < arr->n_children; ++i) {
        if (arr->children != nullptr && arr->children[i] != nullptr &&
            arr->children[i]->release != nullptr) {
            arr->children[i]->release(arr->children[i]);
        }
    }
    delete static_cast<RootArrayPriv*>(arr->private_data);
    arr->private_data = nullptr;
    arr->release = nullptr;
}

inline bool bitmap_get(const void* buf, std::int64_t offset, std::int64_t idx) noexcept {
    if (buf == nullptr) {
        return true;
    }
    const auto* bytes = static_cast<const std::uint8_t*>(buf);
    const std::int64_t bit_idx = offset + idx;
    return ((bytes[bit_idx >> 3] >> (bit_idx & 7)) & 1U) != 0;
}

inline void build_validity_bitmap(const std::vector<std::uint8_t>& mask, std::size_t n,
                                  std::vector<std::uint8_t>& out_bitmap,
                                  std::int64_t& out_null_count) {
    out_bitmap.clear();
    out_null_count = 0;
    if (mask.empty() || n == 0) {
        return;
    }
    bool any_null = false;
    for (std::size_t i = 0; i < n; ++i) {
        if (mask[i] == 0) {
            any_null = true;
            ++out_null_count;
        }
    }
    if (!any_null) {
        return;
    }
    out_bitmap.assign((n + 7) / 8, 0);
    for (std::size_t i = 0; i < n; ++i) {
        if (mask[i] != 0) {
            out_bitmap[i >> 3] |= static_cast<std::uint8_t>(1U << (i & 7));
        }
    }
}

} // namespace arrow_detail

struct arrow_batch {
    ArrowSchema schema{};
    ArrowArray array{};

    arrow_batch() = default;
    ~arrow_batch() { release(); }

    arrow_batch(const arrow_batch&) = delete;
    arrow_batch& operator=(const arrow_batch&) = delete;

    arrow_batch(arrow_batch&& other) noexcept : schema(other.schema), array(other.array) {
        other.schema = ArrowSchema{};
        other.array = ArrowArray{};
    }

    arrow_batch& operator=(arrow_batch&& other) noexcept {
        if (this != &other) {
            release();
            schema = other.schema;
            array = other.array;
            other.schema = ArrowSchema{};
            other.array = ArrowArray{};
        }
        return *this;
    }

    bool is_valid() const noexcept { return schema.release != nullptr && array.release != nullptr; }

    void release() noexcept {
        if (array.release != nullptr) {
            array.release(&array);
        }
        array = ArrowArray{};
        if (schema.release != nullptr) {
            schema.release(&schema);
        }
        schema = ArrowSchema{};
    }
};

inline arrow_batch export_to_arrow(data_frame df) {
    if (df.names.size() != df.columns.size()) {
        throw std::invalid_argument("export_to_arrow: data_frame names and columns size mismatch");
    }

    const std::size_t ncols = df.ncols();
    const std::size_t nrows = df.nrows();

    auto owner = std::make_shared<arrow_detail::BatchOwner>();
    owner->names = std::move(df.names);
    owner->cols.resize(ncols);
    owner->child_schemas.resize(ncols);
    owner->child_schema_ptrs.resize(ncols);
    owner->child_arrays.resize(ncols);
    owner->child_array_ptrs.resize(ncols);

    for (std::size_t c = 0; c < ncols; ++c) {
        auto& cb = owner->cols[c];
        cb.col_storage = std::move(df.columns[c]);
        std::vector<std::uint8_t> val_mask;
        if (c < df.validity.size()) {
            val_mask = std::move(df.validity[c]);
        }

        std::int64_t null_count = 0;
        arrow_detail::build_validity_bitmap(val_mask, nrows, cb.validity_bitmap, null_count);

        ArrowSchema& cs = owner->child_schemas[c];
        ArrowArray& ca = owner->child_arrays[c];
        cs.name = owner->names[c].c_str();
        cs.flags = null_count > 0 ? ARROW_FLAG_NULLABLE : 0;
        cs.release = &arrow_detail::release_child_schema;
        cs.private_data = new arrow_detail::ChildSchemaPriv{owner};

        ca.length = static_cast<std::int64_t>(nrows);
        ca.null_count = null_count;
        ca.offset = 0;
        ca.release = &arrow_detail::release_child_array;
        ca.private_data = new arrow_detail::ChildArrayPriv{owner};

        cb.buffer_ptrs[0] = cb.validity_bitmap.empty() ? nullptr : cb.validity_bitmap.data();

        std::visit(
            [&](const auto& vec) {
                using VecT = std::decay_t<decltype(vec)>;
                if constexpr (std::is_same_v<VecT, std::vector<double>>) {
                    cs.format = "g";
                    ca.n_buffers = 2;
                    cb.buffer_ptrs[1] = vec.data();
                } else if constexpr (std::is_same_v<VecT, std::vector<std::int64_t>>) {
                    cs.format = "l";
                    ca.n_buffers = 2;
                    cb.buffer_ptrs[1] = vec.data();
                } else if constexpr (std::is_same_v<VecT, std::vector<std::int32_t>>) {
                    cs.format = "i";
                    ca.n_buffers = 2;
                    cb.buffer_ptrs[1] = vec.data();
                } else if constexpr (std::is_same_v<VecT, std::vector<bool>>) {
                    cs.format = "b";
                    ca.n_buffers = 2;
                    cb.bool_bitmap.assign((nrows + 7) / 8, 0);
                    for (std::size_t r = 0; r < nrows; ++r) {
                        if (vec[r]) {
                            cb.bool_bitmap[r >> 3] |= static_cast<std::uint8_t>(1U << (r & 7));
                        }
                    }
                    cb.buffer_ptrs[1] = cb.bool_bitmap.data();
                } else {
                    cs.format = "u";
                    ca.n_buffers = 3;
                    std::size_t total_bytes = 0;
                    for (std::size_t r = 0; r < nrows; ++r) {
                        total_bytes += vec[r].size();
                    }
                    if (total_bytes >
                        static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max())) {
                        throw std::overflow_error(
                            "export_to_arrow: utf8 column exceeds int32 size");
                    }
                    cb.utf8_offsets.resize(nrows + 1);
                    cb.utf8_data.reserve(total_bytes);
                    std::int32_t cur_off = 0;
                    for (std::size_t r = 0; r < nrows; ++r) {
                        cb.utf8_offsets[r] = cur_off;
                        cb.utf8_data.append(vec[r]);
                        cur_off += static_cast<std::int32_t>(vec[r].size());
                    }
                    cb.utf8_offsets[nrows] = cur_off;
                    cb.buffer_ptrs[1] = cb.utf8_offsets.data();
                    cb.buffer_ptrs[2] = cb.utf8_data.data();
                }
            },
            cb.col_storage);

        ca.buffers = cb.buffer_ptrs;
        owner->child_schema_ptrs[c] = &cs;
        owner->child_array_ptrs[c] = &ca;
    }

    arrow_batch batch;
    batch.schema.format = "+s";
    batch.schema.name = "";
    batch.schema.flags = 0;
    batch.schema.n_children = static_cast<std::int64_t>(ncols);
    batch.schema.children = ncols > 0 ? owner->child_schema_ptrs.data() : nullptr;
    batch.schema.release = &arrow_detail::release_root_schema;
    batch.schema.private_data = new arrow_detail::RootSchemaPriv{owner};

    batch.array.length = static_cast<std::int64_t>(nrows);
    batch.array.null_count = 0;
    batch.array.offset = 0;
    batch.array.n_buffers = 1;
    batch.array.buffers = owner->struct_buffers;
    batch.array.n_children = static_cast<std::int64_t>(ncols);
    batch.array.children = ncols > 0 ? owner->child_array_ptrs.data() : nullptr;
    batch.array.release = &arrow_detail::release_root_array;
    batch.array.private_data = new arrow_detail::RootArrayPriv{owner};

    return batch;
}

inline data_frame import_from_arrow(const arrow_batch& batch) {
    if (!batch.is_valid() || batch.schema.format == nullptr ||
        std::strcmp(batch.schema.format, "+s") != 0) {
        throw std::invalid_argument("import_from_arrow: expected valid Arrow struct batch ('+s')");
    }
    if (batch.schema.n_children != batch.array.n_children) {
        throw std::invalid_argument("import_from_arrow: schema and array child count mismatch");
    }

    const std::int64_t ncols = batch.schema.n_children;
    const std::int64_t nrows = batch.array.length;
    const std::int64_t root_off = batch.array.offset;

    data_frame out;
    for (std::int64_t c = 0; c < ncols; ++c) {
        const ArrowSchema* cs = batch.schema.children[c];
        const ArrowArray* ca = batch.array.children[c];
        if (cs == nullptr || ca == nullptr || cs->format == nullptr) {
            throw std::invalid_argument("import_from_arrow: null child schema or array");
        }
        std::string col_name = cs->name != nullptr ? cs->name : ("col" + std::to_string(c));
        const std::int64_t off = root_off + ca->offset;

        std::vector<std::uint8_t> mask;
        if (ca->null_count != 0 && ca->n_buffers > 0 && ca->buffers[0] != nullptr) {
            mask.resize(static_cast<std::size_t>(nrows));
            for (std::int64_t r = 0; r < nrows; ++r) {
                mask[static_cast<std::size_t>(r)] =
                    arrow_detail::bitmap_get(ca->buffers[0], off, r) ? 1 : 0;
            }
        }

        const char fmt0 = cs->format[0];
        const char fmt1 = fmt0 != '\0' ? cs->format[1] : '\0';
        column_data col;

        if (fmt0 == 'g' && fmt1 == '\0') {
            std::vector<double> vec(static_cast<std::size_t>(nrows));
            if (nrows > 0) {
                const auto* src = static_cast<const double*>(ca->buffers[1]) + off;
                std::memcpy(vec.data(), src, static_cast<std::size_t>(nrows) * sizeof(double));
            }
            col = std::move(vec);
        } else if (fmt0 == 'f' && fmt1 == '\0') {
            std::vector<double> vec(static_cast<std::size_t>(nrows));
            if (nrows > 0) {
                const auto* src = static_cast<const float*>(ca->buffers[1]) + off;
                for (std::int64_t r = 0; r < nrows; ++r) {
                    vec[static_cast<std::size_t>(r)] = static_cast<double>(src[r]);
                }
            }
            col = std::move(vec);
        } else if (fmt0 == 'l' && fmt1 == '\0') {
            std::vector<std::int64_t> vec(static_cast<std::size_t>(nrows));
            if (nrows > 0) {
                const auto* src = static_cast<const std::int64_t*>(ca->buffers[1]) + off;
                std::memcpy(vec.data(), src,
                            static_cast<std::size_t>(nrows) * sizeof(std::int64_t));
            }
            col = std::move(vec);
        } else if (fmt0 == 'i' && fmt1 == '\0') {
            std::vector<std::int32_t> vec(static_cast<std::size_t>(nrows));
            if (nrows > 0) {
                const auto* src = static_cast<const std::int32_t*>(ca->buffers[1]) + off;
                std::memcpy(vec.data(), src,
                            static_cast<std::size_t>(nrows) * sizeof(std::int32_t));
            }
            col = std::move(vec);
        } else if (fmt0 == 'b' && fmt1 == '\0') {
            std::vector<bool> vec(static_cast<std::size_t>(nrows), false);
            for (std::int64_t r = 0; r < nrows; ++r) {
                vec[static_cast<std::size_t>(r)] = arrow_detail::bitmap_get(ca->buffers[1], off, r);
            }
            col = std::move(vec);
        } else if (fmt0 == 'u' && fmt1 == '\0') {
            std::vector<std::string> vec(static_cast<std::size_t>(nrows));
            if (nrows > 0) {
                const auto* offsets = static_cast<const std::int32_t*>(ca->buffers[1]) + off;
                const auto* bytes = static_cast<const char*>(ca->buffers[2]);
                for (std::int64_t r = 0; r < nrows; ++r) {
                    const std::int32_t start = offsets[r];
                    const std::int32_t end = offsets[r + 1];
                    vec[static_cast<std::size_t>(r)].assign(bytes + start,
                                                            static_cast<std::size_t>(end - start));
                }
            }
            col = std::move(vec);
        } else if (fmt0 == 'U' && fmt1 == '\0') {
            std::vector<std::string> vec(static_cast<std::size_t>(nrows));
            if (nrows > 0) {
                const auto* offsets = static_cast<const std::int64_t*>(ca->buffers[1]) + off;
                const auto* bytes = static_cast<const char*>(ca->buffers[2]);
                for (std::int64_t r = 0; r < nrows; ++r) {
                    const std::int64_t start = offsets[r];
                    const std::int64_t end = offsets[r + 1];
                    vec[static_cast<std::size_t>(r)].assign(bytes + start,
                                                            static_cast<std::size_t>(end - start));
                }
            }
            col = std::move(vec);
        } else {
            throw std::invalid_argument("import_from_arrow: unsupported Arrow format '" +
                                        std::string(cs->format) + "'");
        }

        out.add_column(std::move(col_name), std::move(col), std::move(mask));
    }
    return out;
}

} // namespace pyrabind

#if defined(PYRABIND_TARGET_PYTHON)

namespace pyrabind::py_arrow_detail {

struct ArrowPyObj {
    PyObject_HEAD ArrowSchema schema;
    ArrowArray array;
};

inline void capsule_destroy_schema(PyObject* cap) noexcept {
    auto* sch = static_cast<ArrowSchema*>(PyCapsule_GetPointer(cap, "arrow_schema"));
    if (sch == nullptr) {
        PyErr_Clear();
        return;
    }
    if (sch->release != nullptr) {
        sch->release(sch);
    }
    delete sch;
}

inline void capsule_destroy_array(PyObject* cap) noexcept {
    auto* arr = static_cast<ArrowArray*>(PyCapsule_GetPointer(cap, "arrow_array"));
    if (arr == nullptr) {
        PyErr_Clear();
        return;
    }
    if (arr->release != nullptr) {
        arr->release(arr);
    }
    delete arr;
}

inline void arrow_obj_dealloc(PyObject* self) noexcept {
    auto* obj = reinterpret_cast<ArrowPyObj*>(self);
    if (obj->array.release != nullptr) {
        obj->array.release(&obj->array);
    }
    if (obj->schema.release != nullptr) {
        obj->schema.release(&obj->schema);
    }
    Py_TYPE(self)->tp_free(self);
}

inline PyObject* arrow_obj_c_array(PyObject* self, PyObject* args, PyObject* kwargs) {
    static const char* kwlist[] = {"requested_schema", nullptr};
    PyObject* req = Py_None;
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "|O:__arrow_c_array__",
                                     const_cast<char**>(kwlist), &req)) {
        return nullptr;
    }
    (void)req;
    auto* obj = reinterpret_cast<ArrowPyObj*>(self);
    if (obj->schema.release == nullptr || obj->array.release == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "Arrow batch has already been consumed");
        return nullptr;
    }
    auto* out_sch = new ArrowSchema(obj->schema);
    auto* out_arr = new ArrowArray(obj->array);
    obj->schema = ArrowSchema{};
    obj->array = ArrowArray{};

    PyObject* sch_cap = PyCapsule_New(out_sch, "arrow_schema", &capsule_destroy_schema);
    if (sch_cap == nullptr) {
        if (out_sch->release != nullptr) {
            out_sch->release(out_sch);
        }
        if (out_arr->release != nullptr) {
            out_arr->release(out_arr);
        }
        delete out_sch;
        delete out_arr;
        return nullptr;
    }
    PyObject* arr_cap = PyCapsule_New(out_arr, "arrow_array", &capsule_destroy_array);
    if (arr_cap == nullptr) {
        Py_DECREF(sch_cap);
        if (out_arr->release != nullptr) {
            out_arr->release(out_arr);
        }
        delete out_arr;
        return nullptr;
    }
    PyObject* tup = PyTuple_Pack(2, sch_cap, arr_cap);
    Py_DECREF(sch_cap);
    Py_DECREF(arr_cap);
    return tup;
}

inline PyTypeObject* get_arrow_wrapper_type() noexcept {
    static PyMethodDef methods[] = {
        {"__arrow_c_array__",
         reinterpret_cast<PyCFunction>(reinterpret_cast<void (*)(void)>(&arrow_obj_c_array)),
         METH_VARARGS | METH_KEYWORDS, "Export Arrow C Data Interface capsules"},
        {nullptr, nullptr, 0, nullptr},
    };

    static PyTypeObject type_obj = []() {
        PyTypeObject t{};
        Py_SET_REFCNT(reinterpret_cast<PyObject*>(&t), 1);
        t.tp_name = "pyrabind.ArrowBatch";
        t.tp_basicsize = sizeof(ArrowPyObj);
        t.tp_flags = Py_TPFLAGS_DEFAULT;
        t.tp_dealloc = &arrow_obj_dealloc;
        t.tp_methods = methods;
        return t;
    }();

    static bool ready = false;
    if (!ready) {
        if (PyType_Ready(&type_obj) == 0) {
            ready = true;
        }
    }
    return &type_obj;
}

} // namespace pyrabind::py_arrow_detail

namespace nanobind::detail {

template <> struct type_caster<pyrabind::arrow_batch> {
    NB_TYPE_CASTER(pyrabind::arrow_batch, const_name("ArrowBatch"))

    bool from_python(handle src, uint32_t, cleanup_list*) noexcept {
        value.release();
        PyObject* obj = src.ptr();
        if (!PyObject_HasAttrString(obj, "__arrow_c_array__")) {
            return false;
        }
        PyObject* tup = PyObject_CallMethod(obj, "__arrow_c_array__", nullptr);
        if (tup == nullptr) {
            PyErr_Clear();
            return false;
        }
        if (!PyTuple_Check(tup) || PyTuple_GET_SIZE(tup) != 2) {
            Py_DECREF(tup);
            return false;
        }
        PyObject* sch_cap = PyTuple_GET_ITEM(tup, 0);
        PyObject* arr_cap = PyTuple_GET_ITEM(tup, 1);
        auto* sch = static_cast<ArrowSchema*>(PyCapsule_GetPointer(sch_cap, "arrow_schema"));
        auto* arr = static_cast<ArrowArray*>(PyCapsule_GetPointer(arr_cap, "arrow_array"));
        if (sch == nullptr || arr == nullptr || sch->release == nullptr ||
            arr->release == nullptr) {
            PyErr_Clear();
            Py_DECREF(tup);
            return false;
        }
        value.schema = *sch;
        value.array = *arr;
        *sch = ArrowSchema{};
        *arr = ArrowArray{};
        Py_DECREF(tup);
        return true;
    }

    static handle from_cpp(pyrabind::arrow_batch&& batch, rv_policy, cleanup_list*) noexcept {
        PyTypeObject* tp = pyrabind::py_arrow_detail::get_arrow_wrapper_type();
        auto* obj = PyObject_New(pyrabind::py_arrow_detail::ArrowPyObj, tp);
        if (obj == nullptr) {
            return {};
        }
        obj->schema = batch.schema;
        obj->array = batch.array;
        batch.schema = ArrowSchema{};
        batch.array = ArrowArray{};
        return handle(reinterpret_cast<PyObject*>(obj));
    }

    static handle from_cpp(const pyrabind::arrow_batch&, rv_policy, cleanup_list*) noexcept {
        PyErr_SetString(PyExc_TypeError, "arrow_batch is move-only and cannot be copied");
        return {};
    }
};

} // namespace nanobind::detail

#elif defined(PYRABIND_TARGET_R)

namespace pyrabind::r_detail {

inline void r_finalize_arrow_schema(SEXP xptr) noexcept {
    auto* sch = static_cast<ArrowSchema*>(R_ExternalPtrAddr(xptr));
    if (sch != nullptr) {
        if (sch->release != nullptr) {
            sch->release(sch);
        }
        delete sch;
        R_ClearExternalPtr(xptr);
    }
}

inline void r_finalize_arrow_array(SEXP xptr) noexcept {
    auto* arr = static_cast<ArrowArray*>(R_ExternalPtrAddr(xptr));
    if (arr != nullptr) {
        if (arr->release != nullptr) {
            arr->release(arr);
        }
        delete arr;
        R_ClearExternalPtr(xptr);
    }
}

template <> struct type_caster<arrow_batch> {
    arrow_batch val;

    bool from_r(SEXP x, bool) {
        val.release();
        if (x == nullptr || x == R_NilValue || TYPEOF(x) != EXTPTRSXP) {
            return false;
        }
        SEXP sch_xptr = R_ExternalPtrTag(x);
        if (sch_xptr == nullptr || sch_xptr == R_NilValue || TYPEOF(sch_xptr) != EXTPTRSXP) {
            sch_xptr = Rf_getAttrib(x, Rf_install("schema"));
            if (sch_xptr == nullptr || sch_xptr == R_NilValue || TYPEOF(sch_xptr) != EXTPTRSXP) {
                return false;
            }
        }
        auto* arr = static_cast<ArrowArray*>(R_ExternalPtrAddr(x));
        auto* sch = static_cast<ArrowSchema*>(R_ExternalPtrAddr(sch_xptr));
        if (arr == nullptr || sch == nullptr || arr->release == nullptr ||
            sch->release == nullptr) {
            return false;
        }
        val.schema = *sch;
        val.array = *arr;
        *sch = ArrowSchema{};
        *arr = ArrowArray{};
        return true;
    }

    arrow_batch& get() noexcept { return val; }

    static SEXP to_r(arrow_batch batch, rv_policy, SEXP) {
        auto* heap_sch = new ArrowSchema(batch.schema);
        auto* heap_arr = new ArrowArray(batch.array);
        batch.schema = ArrowSchema{};
        batch.array = ArrowArray{};

        SEXP sch_xptr = PROTECT(R_MakeExternalPtr(heap_sch, R_NilValue, R_NilValue));
        R_RegisterCFinalizerEx(sch_xptr, &r_finalize_arrow_schema, TRUE);
        SEXP sch_cls = PROTECT(Rf_mkString("nanoarrow_schema"));
        Rf_setAttrib(sch_xptr, R_ClassSymbol, sch_cls);

        SEXP arr_xptr = PROTECT(R_MakeExternalPtr(heap_arr, sch_xptr, R_NilValue));
        R_RegisterCFinalizerEx(arr_xptr, &r_finalize_arrow_array, TRUE);
        SEXP arr_cls = PROTECT(Rf_mkString("nanoarrow_array"));
        Rf_setAttrib(arr_xptr, R_ClassSymbol, arr_cls);

        UNPROTECT(4);
        return arr_xptr;
    }
};

} // namespace pyrabind::r_detail

#endif
