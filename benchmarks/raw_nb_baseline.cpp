#include "bench_kernels.hpp"
#include "pyrabind/arrow_abi.hpp"

#include <nanobind/nanobind.h>
#include <nanobind/ndarray.h>
#include <nanobind/stl/vector.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

namespace nb = nanobind;

namespace {

struct RawBenchPoint {
    double x = 0.0;
    double y = 0.0;

    RawBenchPoint() = default;
    RawBenchPoint(double x_val, double y_val) noexcept : x(x_val), y(y_val) {}

    double dot(const RawBenchPoint& other) const noexcept { return x * other.x + y * other.y; }
};

void noop() {
    pyrabind_bench::noop_kernel();
}

std::int64_t add_i64(std::int64_t a, std::int64_t b) {
    return pyrabind_bench::add_i64_kernel(a, b);
}

double dot_view(nb::ndarray<const double, nb::ndim<1>> a,
                nb::ndarray<const double, nb::ndim<1>> b) {
    const std::size_t n = a.shape(0);
    if (b.shape(0) != n) {
        throw nb::value_error("dot_view requires arrays of equal length");
    }
    if (a.stride(0) == 1 && b.stride(0) == 1 && n > 0) {
        return pyrabind_bench::dot_contiguous_kernel(a.data(), b.data(), n);
    }
    auto va = a.view();
    auto vb = b.view();
    double acc = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        acc += va(i) * vb(i);
    }
    return acc;
}

double dot_vec(const std::vector<double>& a, const std::vector<double>& b) {
    const std::size_t n = a.size();
    if (b.size() != n) {
        throw nb::value_error("dot_vec requires vectors of equal length");
    }
    return pyrabind_bench::dot_contiguous_kernel(a.data(), b.data(), n);
}

nb::ndarray<nb::numpy, double, nb::ndim<1>> scale_ndarray(nb::ndarray<const double, nb::ndim<1>> a,
                                                          double factor, double offset) {
    const std::size_t n = a.shape(0);
    auto* buf = new double[n];
    if (a.stride(0) == 1 && n > 0) {
        pyrabind_bench::scale_contiguous_kernel(a.data(), buf, n, factor, offset);
    } else {
        auto va = a.view();
        for (std::size_t i = 0; i < n; ++i) {
            buf[i] = va(i) * factor + offset;
        }
    }
    nb::capsule owner(buf, [](void* p) noexcept { delete[] static_cast<double*>(p); });
    return nb::ndarray<nb::numpy, double, nb::ndim<1>>(buf, {n}, owner);
}

std::vector<double> scale_vec(const std::vector<double>& a, double factor, double offset) {
    const std::size_t n = a.size();
    std::vector<double> out(n);
    if (n > 0) {
        pyrabind_bench::scale_contiguous_kernel(a.data(), out.data(), n, factor, offset);
    }
    return out;
}

double mat_sum_view(nb::ndarray<const double, nb::ndim<2>> m) {
    const std::size_t rows = m.shape(0);
    const std::size_t cols = m.shape(1);
    const std::int64_t rs = m.stride(0);
    const std::int64_t cs = m.stride(1);
    if ((rs == static_cast<std::int64_t>(cols) && cs == 1) ||
        (rs == 1 && cs == static_cast<std::int64_t>(rows))) {
        return pyrabind_bench::sum_contiguous_kernel(m.data(), rows * cols);
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

nb::object df_transform(nb::object df) {
    nb::object id_col = df["id"];
    nb::object x_col = df["x"];
    nb::object tag_col = df["tag"];

    auto id_arr = nb::cast<nb::ndarray<const std::int64_t, nb::ndim<1>>>(id_col.attr("to_numpy")());
    auto x_arr = nb::cast<nb::ndarray<const double, nb::ndim<1>>>(x_col.attr("to_numpy")());
    const std::size_t n = x_arr.shape(0);
    if (id_arr.shape(0) != n) {
        throw nb::value_error("column length mismatch");
    }

    auto* y_buf = new double[n];
    if (id_arr.stride(0) == 1 && x_arr.stride(0) == 1 && n > 0) {
        const std::int64_t* ip = id_arr.data();
        const double* xp = x_arr.data();
        for (std::size_t i = 0; i < n; ++i) {
            y_buf[i] = xp[i] * 2.0 + static_cast<double>(ip[i]);
        }
    } else {
        auto vid = id_arr.view();
        auto vx = x_arr.view();
        for (std::size_t i = 0; i < n; ++i) {
            y_buf[i] = vx(i) * 2.0 + static_cast<double>(vid(i));
        }
    }

    nb::capsule owner(y_buf, [](void* p) noexcept { delete[] static_cast<double*>(p); });
    nb::ndarray<nb::numpy, double, nb::ndim<1>> y_arr(y_buf, {n}, owner);

    nb::dict out;
    out["id"] = id_col;
    out["x"] = x_col;
    out["y"] = nb::cast(std::move(y_arr));
    out["tag"] = tag_col;
    return nb::module_::import_("pandas").attr("DataFrame")(out);
}

struct RawArrowOwner {
    ArrowSchema upstream_sch{};
    ArrowArray upstream_arr{};
    std::vector<double> y_vec;
    ArrowSchema y_sch{};
    ArrowArray y_arr{};
    const void* y_bufs[2]{nullptr, nullptr};
    ArrowSchema child_schs[4]{};
    ArrowArray child_arrs[4]{};
    ArrowSchema* child_sch_ptrs[4]{};
    ArrowArray* child_arr_ptrs[4]{};
    const void* root_bufs[1]{nullptr};

    ~RawArrowOwner() {
        if (upstream_arr.release != nullptr) {
            upstream_arr.release(&upstream_arr);
        }
        if (upstream_sch.release != nullptr) {
            upstream_sch.release(&upstream_sch);
        }
    }
};

struct RawArrowPriv {
    std::shared_ptr<RawArrowOwner> owner;
};

inline void raw_release_schema(ArrowSchema* s) {
    if (s == nullptr || s->release == nullptr) {
        return;
    }
    for (std::int64_t i = 0; i < s->n_children; ++i) {
        if (s->children != nullptr && s->children[i] != nullptr &&
            s->children[i]->release != nullptr) {
            s->children[i]->release(s->children[i]);
        }
    }
    delete static_cast<RawArrowPriv*>(s->private_data);
    s->private_data = nullptr;
    s->release = nullptr;
}

inline void raw_release_array(ArrowArray* a) {
    if (a == nullptr || a->release == nullptr) {
        return;
    }
    for (std::int64_t i = 0; i < a->n_children; ++i) {
        if (a->children != nullptr && a->children[i] != nullptr &&
            a->children[i]->release != nullptr) {
            a->children[i]->release(a->children[i]);
        }
    }
    delete static_cast<RawArrowPriv*>(a->private_data);
    a->private_data = nullptr;
    a->release = nullptr;
}

struct RawArrowBatch {
    ArrowSchema schema{};
    ArrowArray array{};

    RawArrowBatch() = default;
    ~RawArrowBatch() {
        if (array.release != nullptr) {
            array.release(&array);
        }
        if (schema.release != nullptr) {
            schema.release(&schema);
        }
    }
    RawArrowBatch(const RawArrowBatch&) = delete;
    RawArrowBatch& operator=(const RawArrowBatch&) = delete;
    RawArrowBatch(RawArrowBatch&& o) noexcept : schema(o.schema), array(o.array) {
        o.schema = ArrowSchema{};
        o.array = ArrowArray{};
    }

    nb::tuple arrow_c_array(nb::object) {
        if (schema.release == nullptr || array.release == nullptr) {
            throw nb::value_error("arrow batch already consumed");
        }
        auto* out_s = new ArrowSchema(schema);
        auto* out_a = new ArrowArray(array);
        schema = ArrowSchema{};
        array = ArrowArray{};
        PyObject* sc = PyCapsule_New(out_s, "arrow_schema", [](PyObject* c) noexcept {
            auto* p = static_cast<ArrowSchema*>(PyCapsule_GetPointer(c, "arrow_schema"));
            if (p != nullptr) {
                if (p->release != nullptr) {
                    p->release(p);
                }
                delete p;
            } else {
                PyErr_Clear();
            }
        });
        PyObject* ac = PyCapsule_New(out_a, "arrow_array", [](PyObject* c) noexcept {
            auto* p = static_cast<ArrowArray*>(PyCapsule_GetPointer(c, "arrow_array"));
            if (p != nullptr) {
                if (p->release != nullptr) {
                    p->release(p);
                }
                delete p;
            } else {
                PyErr_Clear();
            }
        });
        return nb::make_tuple(nb::steal<nb::object>(sc), nb::steal<nb::object>(ac));
    }
};

RawArrowBatch arrow_transform(nb::object rb) {
    static PyObject* attr_name = PyUnicode_InternFromString("__arrow_c_array__");
    PyObject* tup = PyObject_CallMethodNoArgs(rb.ptr(), attr_name);
    if (tup == nullptr) {
        throw nb::python_error();
    }
    nb::tuple caps = nb::steal<nb::tuple>(tup);
    auto* in_s = static_cast<ArrowSchema*>(PyCapsule_GetPointer(caps[0].ptr(), "arrow_schema"));
    auto* in_a = static_cast<ArrowArray*>(PyCapsule_GetPointer(caps[1].ptr(), "arrow_array"));
    if (in_s == nullptr || in_a == nullptr || in_s->release == nullptr ||
        in_a->release == nullptr) {
        throw nb::value_error("invalid Arrow capsules");
    }

    auto own = std::make_shared<RawArrowOwner>();
    own->upstream_sch = *in_s;
    own->upstream_arr = *in_a;
    *in_s = ArrowSchema{};
    *in_a = ArrowArray{};

    if (own->upstream_arr.n_children != 3 || own->upstream_sch.n_children != 3) {
        throw nb::value_error("expected 3-column RecordBatch");
    }
    const std::size_t n = static_cast<std::size_t>(own->upstream_arr.length);
    const std::int64_t root_off = own->upstream_arr.offset;
    const ArrowArray* id_a = own->upstream_arr.children[0];
    const ArrowArray* x_a = own->upstream_arr.children[1];
    const auto* ip = static_cast<const std::int64_t*>(id_a->buffers[1]) + root_off + id_a->offset;
    const auto* xp = static_cast<const double*>(x_a->buffers[1]) + root_off + x_a->offset;

    own->y_vec.resize(n);
    for (std::size_t i = 0; i < n; ++i) {
        own->y_vec[i] = xp[i] * 2.0 + static_cast<double>(ip[i]);
    }
    own->y_bufs[1] = own->y_vec.data();

    const int src_map[3] = {0, 1, 2};
    const int dst_pos[3] = {0, 1, 3};
    for (int k = 0; k < 3; ++k) {
        const int si = src_map[k];
        const int di = dst_pos[k];
        own->child_schs[di] = *own->upstream_sch.children[si];
        own->child_schs[di].children = nullptr;
        own->child_schs[di].dictionary = nullptr;
        own->child_schs[di].release = &raw_release_schema;
        own->child_schs[di].private_data = new RawArrowPriv{own};

        own->child_arrs[di] = *own->upstream_arr.children[si];
        own->child_arrs[di].length = own->upstream_arr.length;
        own->child_arrs[di].offset = own->upstream_arr.children[si]->offset + root_off;
        own->child_arrs[di].children = nullptr;
        own->child_arrs[di].dictionary = nullptr;
        own->child_arrs[di].release = &raw_release_array;
        own->child_arrs[di].private_data = new RawArrowPriv{own};
    }

    own->child_schs[2].format = "g";
    own->child_schs[2].name = "y";
    own->child_schs[2].flags = 0;
    own->child_schs[2].release = &raw_release_schema;
    own->child_schs[2].private_data = new RawArrowPriv{own};

    own->child_arrs[2].length = own->upstream_arr.length;
    own->child_arrs[2].null_count = 0;
    own->child_arrs[2].offset = 0;
    own->child_arrs[2].n_buffers = 2;
    own->child_arrs[2].buffers = own->y_bufs;
    own->child_arrs[2].release = &raw_release_array;
    own->child_arrs[2].private_data = new RawArrowPriv{own};

    for (int i = 0; i < 4; ++i) {
        own->child_sch_ptrs[i] = &own->child_schs[i];
        own->child_arr_ptrs[i] = &own->child_arrs[i];
    }

    RawArrowBatch out;
    out.schema.format = "+s";
    out.schema.name = "";
    out.schema.n_children = 4;
    out.schema.children = own->child_sch_ptrs;
    out.schema.release = &raw_release_schema;
    out.schema.private_data = new RawArrowPriv{own};

    out.array.length = own->upstream_arr.length;
    out.array.null_count = 0;
    out.array.offset = 0;
    out.array.n_buffers = 1;
    out.array.buffers = own->root_bufs;
    out.array.n_children = 4;
    out.array.children = own->child_arr_ptrs;
    out.array.release = &raw_release_array;
    out.array.private_data = new RawArrowPriv{own};
    return out;
}

} // namespace

NB_MODULE(raw_nb_bench, m) {
    using namespace nb::literals;
    m.doc() = "Raw upstream nanobind benchmark baseline";

    nb::class_<RawArrowBatch>(m, "RawArrowBatch")
        .def("__arrow_c_array__", &RawArrowBatch::arrow_c_array, "requested_schema"_a = nb::none());

    nb::class_<RawBenchPoint>(m, "BenchPoint")
        .def(nb::init<double, double>(), "x"_a, "y"_a)
        .def_rw("x", &RawBenchPoint::x)
        .def_rw("y", &RawBenchPoint::y)
        .def("dot", &RawBenchPoint::dot, "other"_a);

    m.def("noop", &noop);
    m.def("add_i64", &add_i64, "a"_a, "b"_a);
    m.def("dot_view", &dot_view, "a"_a, "b"_a);
    m.def("dot_vec", &dot_vec, "a"_a, "b"_a);
    m.def("scale_ndarray", &scale_ndarray, "a"_a, "factor"_a, "offset"_a);
    m.def("scale_vec", &scale_vec, "a"_a, "factor"_a, "offset"_a);
    m.def("mat_sum_view", &mat_sum_view, "m"_a);
    m.def("df_transform", &df_transform, "df"_a);
    m.def("arrow_transform", &arrow_transform, "rb"_a);
}
