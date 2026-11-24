#' Construct a RidgeRegressor model
#'
#' @param ... Arguments passed to the C++ constructor (e.g., `alpha = 1e-6`).
#' @return A `pyrabind_object` wrapping `RidgeRegressor`.
#' @export
RidgeRegressor <- function(...) {
    .pyrastats_env$ns$RidgeRegressor(...)
}

#' SummaryStats C++ class reference
#'
#' @param ... Arguments forwarded to `SummaryStats`.
#' @export
SummaryStats <- function(...) {
    .pyrastats_env$ns$SummaryStats(...)
}

#' Compute robust descriptive statistics on a numeric vector
#'
#' @param x Numeric vector.
#' @return A `SummaryStats` object (`n`, `mean`, `std`, `median`, `mad`, `q25`, `q75`).
#' @export
robust_summary <- function(x) {
    .pyrastats_env$ns$robust_summary(x)
}

#' Compute pairwise Euclidean distances between rows of two matrices
#'
#' @param a Numeric matrix (`n_a x d`).
#' @param b Numeric matrix (`n_b x d`).
#' @return Numeric matrix (`n_a x n_b`).
#' @export
pairwise_euclidean <- function(a, b) {
    .pyrastats_env$ns$pairwise_euclidean(a, b)
}

#' Standardize selected numeric columns while preserving NA values
#'
#' @param df Data frame or tibble.
#' @param cols Character vector of column names.
#' @return Data frame with standardized columns and preserved NAs.
#' @export
standardize_table <- function(df, cols) {
    .pyrastats_env$ns$standardize_table(df, cols)
}

#' Export a data.frame to a zero-copy Arrow C Data Interface batch
#'
#' @param df Data frame.
#' @return A `nanoarrow_array` external pointer holding a struct batch.
#' @export
stats_to_arrow <- function(df) {
    .pyrastats_env$ns$stats_to_arrow(df)
}
