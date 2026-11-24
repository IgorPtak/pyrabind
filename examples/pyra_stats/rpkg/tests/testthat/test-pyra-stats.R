test_that("robust_summary and pairwise_euclidean compute accurate values", {
    st <- robust_summary(c(1.0, 2.0, 3.0, 4.0, 100.0))
    expect_equal(st$n, 5L)
    expect_equal(st$median, 3.0)
    expect_equal(st$mean, 22.0)
    expect_equal(st$q25, 2.0)
    expect_equal(st$q75, 4.0)

    a <- matrix(c(0.0, 3.0, 0.0, 4.0), nrow = 2L, ncol = 2L)
    b <- matrix(c(0.0, 6.0, 0.0, 8.0), nrow = 2L, ncol = 2L)
    d <- pairwise_euclidean(a, b)
    expect_equal(dim(d), c(2L, 2L))
    expect_equal(d[1L, 2L], 10.0)
    expect_equal(d[2L, 1L], 5.0)
})

test_that("RidgeRegressor fits linear model via Cholesky and enforces kw_only alpha", {
    set.seed(42)
    X <- matrix(rnorm(80L * 3L), nrow = 80L, ncol = 3L)
    w <- c(2.5, -1.5, 0.8)
    y <- 1.2 + as.numeric(X %*% w)

    m <- RidgeRegressor(alpha = 1e-6)$fit(X, y)
    expect_true(m$is_fitted)
    expect_gt(m$score_r2(X, y), 0.9999)
    expect_lt(max(abs(m$coef - w)), 1e-4)
    expect_lt(abs(m$intercept - 1.2), 1e-4)
    expect_error(RidgeRegressor(1.0))
})

test_that("standardize_table preserves NAs and exports Arrow nanoarrow_array", {
    df <- data.frame(
        val = c(1.0, 2.0, NA_real_, 4.0),
        grp = c("a", NA_character_, "c", "d"),
        stringsAsFactors = FALSE
    )
    out <- standardize_table(df, "val")
    expect_true(is.na(out$val[[3L]]))
    expect_true(is.na(out$grp[[2L]]))
    expect_lt(abs(mean(out$val, na.rm = TRUE)), 1e-12)

    ab <- stats_to_arrow(out)
    expect_s3_class(ab, "nanoarrow_array")
})
