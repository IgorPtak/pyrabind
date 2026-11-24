#!/usr/bin/env Rscript
# Pure base-R overhead benchmark runner (zero external R packages required).

args <- commandArgs(trailingOnly = TRUE)
if (length(args) < 3) {
    stop("usage: run_bench.R <nanorbind.R> <bench_module.so> <raw_r_bench.so>")
}

nanorbind_r_path <- args[[1]]
bench_mod_path <- args[[2]]
raw_r_path <- args[[3]]

source(nanorbind_r_path)
mod <- nanorbind_load(bench_mod_path, "bench_module")

raw_dll <- dyn.load(raw_r_path)
sym_noop <- getNativeSymbolInfo("raw_r_noop", raw_dll)
sym_add_i64 <- getNativeSymbolInfo("raw_r_add_i64", raw_dll)
sym_point_new <- getNativeSymbolInfo("raw_r_point_new", raw_dll)
sym_point_dot <- getNativeSymbolInfo("raw_r_point_dot", raw_dll)
sym_dot_vec <- getNativeSymbolInfo("raw_r_dot_vec", raw_dll)
sym_scale_vec <- getNativeSymbolInfo("raw_r_scale_vec", raw_dll)
sym_mat_sum <- getNativeSymbolInfo("raw_r_mat_sum", raw_dll)
sym_df_transform <- getNativeSymbolInfo("raw_r_df_transform", raw_dll)

measure_r <- function(fn, target_trial_ms = 20.0, trials = 19L, min_iters = 1L, max_iters = 200000L) {
    fn()
    iters <- as.integer(min_iters)
    while (iters < max_iters) {
        t0 <- as.numeric(Sys.time()) * 1e9
        i <- 1L
        while (i <= iters) {
            fn()
            i <- i + 1L
        }
        elapsed_ns <- (as.numeric(Sys.time()) * 1e9) - t0
        if (elapsed_ns >= target_trial_ms * 1e6) {
            break
        }
        if (elapsed_ns <= 1000.0) {
            iters <- as.integer(min(iters * 10L, max_iters))
        } else {
            factor <- max(2L, as.integer((target_trial_ms * 1e6) / elapsed_ns) + 1L)
            iters <- as.integer(min(iters * factor, max_iters))
        }
    }

    gc(FALSE)
    samples <- numeric(trials)
    for (t in seq_len(trials)) {
        t0 <- as.numeric(Sys.time()) * 1e9
        i <- 1L
        while (i <= iters) {
            fn()
            i <- i + 1L
        }
        dt_ns <- (as.numeric(Sys.time()) * 1e9) - t0
        samples[[t]] <- dt_ns / as.numeric(iters)
    }

    samples <- sort(samples)
    p95_idx <- min(length(samples), max(1L, as.integer(floor(0.95 * length(samples)))))
    list(
        median_ns = stats::median(samples),
        mean_ns = mean(samples),
        min_ns = samples[[1L]],
        p95_ns = samples[[p95_idx]],
        inner_iters = iters,
        trials = trials
    )
}

stopifnot(mod$add_i64(17L, 25L) == 42)
stopifnot(.Call(sym_add_i64, 17L, 25L) == 42)

p_nr_1 <- mod$BenchPoint(3.0, 4.0)
p_nr_2 <- mod$BenchPoint(-1.5, 2.5)
p_nr_dot <- p_nr_1$dot
p_raw_1 <- .Call(sym_point_new, 3.0, 4.0)
p_raw_2 <- .Call(sym_point_new, -1.5, 2.5)
stopifnot(abs(p_nr_1$dot(p_nr_2) - 5.5) < 1e-12)
stopifnot(abs(.Call(sym_point_dot, p_raw_1, p_raw_2) - 5.5) < 1e-12)

pure_noop <- function() NULL
pure_add_i64 <- function(a, b) a + b
pure_dot <- function(x1, y1, x2, y2) x1 * x2 + y1 * y2

n <- 100000L
idx <- seq.int(0L, n - 1L)
a <- as.double((idx %% 1000L) * 0.25)
b <- as.double(((idx + 7L) %% 1000L) * 0.5)
expected_dot <- sum(a * b)
stopifnot(abs(mod$dot_view(a, b) - expected_dot) < 1e-6)
stopifnot(abs(mod$dot_vec(a, b) - expected_dot) < 1e-6)
stopifnot(abs(.Call(sym_dot_vec, a, b) - expected_dot) < 1e-6)

expected_scale <- a * 1.5 - 2.0
stopifnot(max(abs(mod$scale_ndarray(a, 1.5, -2.0) - expected_scale)) < 1e-12)
stopifnot(max(abs(mod$scale_vec(a, 1.5, -2.0) - expected_scale)) < 1e-12)
stopifnot(max(abs(.Call(sym_scale_vec, a, 1.5, -2.0) - expected_scale)) < 1e-12)

mat <- matrix(1.25, nrow = 256L, ncol = 256L)
expected_mat_sum <- 256.0 * 256.0 * 1.25
stopifnot(abs(mod$mat_sum_view(mat) - expected_mat_sum) < 1e-9)
stopifnot(abs(.Call(sym_mat_sum, mat) - expected_mat_sum) < 1e-9)

df <- data.frame(
    id = idx,
    x = as.double(idx) * 0.125,
    tag = ifelse((idx %% 2L) == 0L, "even", "odd"),
    stringsAsFactors = FALSE
)
out_df_nr <- mod$df_transform(df)
out_df_raw <- .Call(sym_df_transform, df)
stopifnot(identical(names(out_df_nr), c("id", "x", "y", "tag")))
stopifnot(nrow(out_df_nr) == n)
stopifnot(max(abs(out_df_nr$y - out_df_raw$y)) < 1e-12)

nr_noop_fn <- mod$noop
nr_add_fn <- mod$add_i64
nr_dot_view_fn <- mod$dot_view
nr_dot_vec_fn <- mod$dot_vec
nr_scale_nd_fn <- mod$scale_ndarray
nr_scale_vec_fn <- mod$scale_vec
nr_mat_view_fn <- mod$mat_sum_view
nr_df_fn <- mod$df_transform

benchmarks <- list(
    noop = list(
        nanorbind = measure_r(function() nr_noop_fn()),
        raw_capi = measure_r(function() .Call(sym_noop)),
        pure_lang = measure_r(function() pure_noop())
    ),
    add_i64 = list(
        nanorbind = measure_r(function() nr_add_fn(17L, 25L)),
        raw_capi = measure_r(function() .Call(sym_add_i64, 17L, 25L)),
        pure_lang = measure_r(function() pure_add_i64(17L, 25L))
    ),
    point_dot = list(
        nanorbind = measure_r(function() p_nr_1$dot(p_nr_2)),
        nanorbind_cached = measure_r(function() p_nr_dot(p_nr_2)),
        raw_capi = measure_r(function() .Call(sym_point_dot, p_raw_1, p_raw_2)),
        pure_lang = measure_r(function() pure_dot(3.0, 4.0, -1.5, 2.5))
    ),
    dot_100k = list(
        nanorbind_view = measure_r(function() nr_dot_view_fn(a, b)),
        nanorbind_vec_copy = measure_r(function() nr_dot_vec_fn(a, b), min_iters = 5L),
        raw_capi = measure_r(function() .Call(sym_dot_vec, a, b)),
        pure_lang = measure_r(function() sum(a * b))
    ),
    scale_100k = list(
        nanorbind_ndarray = measure_r(function() nr_scale_nd_fn(a, 1.5, -2.0)),
        nanorbind_vec_copy = measure_r(function() nr_scale_vec_fn(a, 1.5, -2.0), min_iters = 5L),
        raw_capi = measure_r(function() .Call(sym_scale_vec, a, 1.5, -2.0)),
        pure_lang = measure_r(function() a * 1.5 - 2.0)
    ),
    mat_sum_256 = list(
        nanorbind_view = measure_r(function() nr_mat_view_fn(mat)),
        raw_capi = measure_r(function() .Call(sym_mat_sum, mat)),
        pure_lang = measure_r(function() sum(mat))
    ),
    df_transform_100k = list(
        nanorbind = measure_r(function() nr_df_fn(df), min_iters = 2L, max_iters = 100L),
        raw_capi = measure_r(function() .Call(sym_df_transform, df), min_iters = 2L, max_iters = 200L),
        pure_lang = measure_r(
            function() {
                data.frame(
                    id = df$id,
                    x = df$x,
                    y = df$x * 2.0 + df$id,
                    tag = df$tag,
                    stringsAsFactors = FALSE
                )
            },
            min_iters = 2L,
            max_iters = 100L
        )
    )
)

json_escape <- function(s) {
    s <- gsub("\\\\", "\\\\\\\\", s)
    s <- gsub("\"", "\\\\\"", s)
    paste0("\"", s, "\"")
}

emit_metrics <- function(m) {
    sprintf(
        '{"median_ns": %.2f, "mean_ns": %.2f, "min_ns": %.2f, "p95_ns": %.2f, "inner_iters": %d, "trials": %d}',
        m$median_ns, m$mean_ns, m$min_ns, m$p95_ns, m$inner_iters, m$trials
    )
}

cat("{\n")
cat('  "versions": {\n')
cat('    "r": ', json_escape(paste(R.version$major, R.version$minor, sep = ".")), ",\n", sep = "")
cat('    "cpp11": ', json_escape(as.character(packageVersion("cpp11"))), "\n", sep = "")
cat("  },\n")
cat('  "benchmarks": {\n')
b_names <- names(benchmarks)
for (bi in seq_along(b_names)) {
    bname <- b_names[[bi]]
    entries <- benchmarks[[bname]]
    e_names <- names(entries)
    cat("    ", json_escape(bname), ": {\n", sep = "")
    for (ei in seq_along(e_names)) {
        ename <- e_names[[ei]]
        comma <- if (ei < length(e_names)) "," else ""
        cat("      ", json_escape(ename), ": ", emit_metrics(entries[[ename]]), comma, "\n", sep = "")
    }
    b_comma <- if (bi < length(b_names)) "," else ""
    cat("    }", b_comma, "\n", sep = "")
}
cat("  }\n")
cat("}\n")
