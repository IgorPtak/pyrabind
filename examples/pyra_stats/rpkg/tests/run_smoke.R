args <- commandArgs(trailingOnly = TRUE)
source(args[[1]])
ps <- pyrabind_load(args[[2]])

st <- ps$robust_summary(c(1.0, 2.0, 3.0, 4.0, 100.0))
stopifnot(st$n == 5L, abs(st$median - 3.0) < 1e-12, abs(st$mean - 22.0) < 1e-12)

a <- matrix(c(0.0, 3.0, 0.0, 4.0), nrow = 2L, ncol = 2L)
b <- matrix(c(0.0, 6.0, 0.0, 8.0), nrow = 2L, ncol = 2L)
d <- ps$pairwise_euclidean(a, b)
stopifnot(identical(dim(d), c(2L, 2L)), abs(d[1L, 2L] - 10.0) < 1e-12, abs(d[2L, 1L] - 5.0) < 1e-12)

set.seed(42)
X <- matrix(rnorm(80L * 3L), nrow = 80L, ncol = 3L)
true_w <- c(2.5, -1.5, 0.8)
y <- 1.2 + as.numeric(X %*% true_w)
model <- ps$RidgeRegressor(alpha = 1e-6)$fit(X, y)
stopifnot(isTRUE(model$is_fitted), model$score_r2(X, y) > 0.9999)
stopifnot(max(abs(model$coef - true_w)) < 1e-4, abs(model$intercept - 1.2) < 1e-4)

df <- data.frame(
    val = c(1.0, 2.0, NA_real_, 4.0),
    grp = c("a", NA_character_, "c", "d"),
    stringsAsFactors = FALSE
)
std_df <- ps$standardize_table(df, "val")
stopifnot(is.na(std_df$val[[3L]]), is.na(std_df$grp[[2L]]))
stopifnot(abs(mean(std_df$val, na.rm = TRUE)) < 1e-12)

abatch <- ps$stats_to_arrow(std_df)
stopifnot(inherits(abatch, "nanoarrow_array"))

cat("pyra_stats R smoke test passed\n")
