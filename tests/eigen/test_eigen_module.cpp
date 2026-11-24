#include "pyrabind/eigen.hpp"
#include "pyrabind/pyrabind.hpp"

#include <Eigen/LU>

#include <cstdint>

namespace nb = pyrabind;

namespace {

using RowMatrixXd = Eigen::Matrix<double, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor>;

Eigen::MatrixXd mat_add_scaled(const Eigen::MatrixXd& a, const Eigen::MatrixXd& b, double scale) {
    if (a.rows() != b.rows() || a.cols() != b.cols()) {
        throw nb::value_error("matrix dimensions must match");
    }
    return a + scale * b;
}

double vec_dot(const Eigen::VectorXd& a, const Eigen::VectorXd& b) {
    if (a.size() != b.size()) {
        throw nb::value_error("vector lengths must match");
    }
    return a.dot(b);
}

Eigen::RowVectorXd row_scale(const Eigen::RowVectorXd& v, double s) {
    return v * s;
}

double fixed_det2(const Eigen::Matrix2d& m2) {
    return m2.determinant();
}

std::int32_t vec_int_sum(const Eigen::VectorXi& v) {
    return v.sum();
}

Eigen::ArrayXXd array_square_plus(const Eigen::ArrayXXd& a, double c) {
    return a.square() + c;
}

RowMatrixXd make_row_major(std::int64_t rows, std::int64_t cols) {
    RowMatrixXd out(rows, cols);
    for (Eigen::Index r = 0; r < rows; ++r) {
        for (Eigen::Index c = 0; c < cols; ++c) {
            out(r, c) = static_cast<double>((r + 1) * 10 + c);
        }
    }
    return out;
}

double row_major_trace_like(Eigen::Ref<const RowMatrixXd> mat) {
    const Eigen::Index d = mat.rows() < mat.cols() ? mat.rows() : mat.cols();
    double s = 0.0;
    for (Eigen::Index i = 0; i < d; ++i) {
        s += mat(i, i);
    }
    return s;
}

double ref_col_sum(Eigen::Ref<const Eigen::MatrixXd> mat, Eigen::Index col) {
    if (col < 0 || col >= mat.cols()) {
        throw nb::index_error("column index out of range");
    }
    return mat.col(col).sum();
}

void scale_vec_inplace(Eigen::Ref<Eigen::VectorXd> v, double factor) {
    v *= factor;
}

void add_dref_inplace(nb::DRef<Eigen::MatrixXd> mat, double delta) {
    mat.array() += delta;
}

double map_const_sum(nb::DMap<const Eigen::MatrixXd> mat) {
    return mat.sum();
}

} // namespace

PYRABIND_MODULE(test_eigen_module, m) {
    using namespace nb::literals;
    m.doc() = "Dual-target Eigen bindings test module";

    m.def("mat_add_scaled", &mat_add_scaled, "a"_a, "b"_a, "scale"_a = 1.0);
    m.def("vec_dot", &vec_dot, "a"_a, "b"_a);
    m.def("row_scale", &row_scale, "v"_a, "s"_a);
    m.def("fixed_det2", &fixed_det2, "m"_a);
    m.def("vec_int_sum", &vec_int_sum, "v"_a);
    m.def("array_square_plus", &array_square_plus, "a"_a, "c"_a);
    m.def("make_row_major", &make_row_major, "rows"_a, "cols"_a);
    m.def("row_major_trace_like", &row_major_trace_like, "mat"_a);
    m.def("ref_col_sum", &ref_col_sum, "mat"_a, "col"_a);
    m.def("scale_vec_inplace", &scale_vec_inplace, "v"_a, "factor"_a);
    m.def("add_dref_inplace", &add_dref_inplace, "mat"_a, "delta"_a);
    m.def("map_const_sum", &map_const_sum, "mat"_a);
}
