args <- commandArgs(trailingOnly = TRUE)
source(args[[1]])
em <- pyrabind_load(args[[2]])

a <- matrix(c(1.0, 3.0, 2.0, 4.0), nrow = 2L, ncol = 2L)
b <- matrix(c(10.0, 30.0, 20.0, 40.0), nrow = 2L, ncol = 2L)
res <- em$mat_add_scaled(a, b, scale = 0.5)
stopifnot(identical(dim(res), c(2L, 2L)))
stopifnot(max(abs(res - (a + 0.5 * b))) < 1e-12)

stopifnot(abs(em$vec_dot(c(1.0, 2.0, 3.0), c(4.0, 5.0, 6.0)) - 32.0) < 1e-12)
stopifnot(max(abs(em$row_scale(c(2.0, -3.0), 3.0) - c(6.0, -9.0))) < 1e-12)
stopifnot(abs(em$fixed_det2(matrix(c(2.0, 1.0, 1.0, 3.0), nrow = 2L, ncol = 2L)) - 5.0) < 1e-12)
stopifnot(em$vec_int_sum(c(1L, 2L, 3L, 4L)) == 10L)

arr <- matrix(c(2.0, 3.0), nrow = 1L, ncol = 2L)
arr_out <- em$array_square_plus(arr, 1.0)
stopifnot(max(abs(arr_out - matrix(c(5.0, 10.0), nrow = 1L, ncol = 2L))) < 1e-12)

rm <- em$make_row_major(2L, 3L)
stopifnot(identical(dim(rm), c(2L, 3L)))
stopifnot(abs(rm[2L, 3L] - 22.0) < 1e-12)
stopifnot(abs(em$row_major_trace_like(rm) - 31.0) < 1e-12)
stopifnot(abs(em$ref_col_sum(rm, 1L) - 32.0) < 1e-12)

# Non-ALTREP writable vector mutated in place via Eigen::Ref<Eigen::VectorXd>
v <- c(1.0, 2.0, 3.0)
em$scale_vec_inplace(v, 4.0)
stopifnot(max(abs(v - c(4.0, 8.0, 12.0))) < 1e-12)

# Writable column-major matrix mutated in place via DRef<Eigen::MatrixXd>
mf <- matrix(c(1.0, 3.0, 2.0, 4.0), nrow = 2L, ncol = 2L)
em$add_dref_inplace(mf, 10.0)
stopifnot(max(abs(mf - matrix(c(11.0, 13.0, 12.0, 14.0), nrow = 2L, ncol = 2L))) < 1e-12)
stopifnot(abs(em$map_const_sum(mf) - 50.0) < 1e-12)

# Mutable Ref must reject compact ALTREP sequences
altrep_rejected <- tryCatch({
    em$scale_vec_inplace(seq_len(4) * 1.0, 2.0)
    TRUE
}, error = function(e) TRUE)
stopifnot(altrep_rejected)

cat("All pyrabind R Eigen tests passed\n")
