args <- commandArgs(trailingOnly = TRUE)
source(args[[1]])
tm <- pyrabind_load(args[[2]])

# 1. Functions, kwargs, overloads, submodule, constants
stopifnot(abs(tm$PI - 3.141592653589793) < 1e-12)
stopifnot(tm$add_defaults(5L) == 115L)
stopifnot(tm$add_defaults(5L, c = 200L) == 215L)
stopifnot(tm$classify(7L) == "int:7")
stopifnot(tm$classify(7.5) == "double:7")
stopifnot(abs(tm$math$square(4.5) - 20.25) < 1e-12)

# 2. Enums and typed exceptions
stopifnot(as.integer(tm$Color$Green) == 2L)
stopifnot(tm$Red == tm$Color$Red)
stopifnot(tm$Red != tm$Blue)

caught_val <- FALSE
tryCatch(tm$check_positive(-1L), pyrabind_value_error = function(e) caught_val <<- TRUE)
stopifnot(caught_val)

caught_key <- FALSE
tryCatch(tm$lookup_key(list(a = 1L), "missing"), pyrabind_key_error = function(e) caught_key <<- TRUE)
stopifnot(caught_key)

# 3. Classes, operators, inheritance, indexing, lifetimes, overloads
a <- tm$Vec2(1.0, 2.0)
b <- tm$Vec2(3.0, 4.0)
c <- 2.0 * a + b
stopifnot(c == tm$Vec2(5.0, 8.0))
stopifnot(-a == tm$Vec2(-1.0, -2.0))
stopifnot(a < b)
stopifnot(abs(tm$Vec2$zero()$norm) < 1e-12)
c$scale(0.5)
stopifnot(abs(c$x - 2.5) < 1e-12)

dog <- tm$Dog("Rex")
stopifnot(dog$speak() == "Rex: woof")
stopifnot(dog$fetch() == "Rex fetched the ball")
stopifnot(tm$greet_animal(dog) == "hello Rex: woof")

box <- tm$Container(3L, 42L)
stopifnot(length(box) == 3L)
stopifnot(box[[0L]] == 42L)
box[[-1L]] <- 77L
stopifnot(box[[2L]] == 77L)
stopifnot(box$`__contains__`(77L))

caught_idx <- FALSE
tryCatch(box[[10L]], pyrabind_index_error = function(e) caught_idx <<- TRUE)
stopifnot(caught_idx)

emb <- box$get_embedded()
stopifnot(emb$id == 99L)
res <- tm$Resource(123L)
box$attach(res)
rm(res)
gc(FALSE)
stopifnot(box$attached_id() == 123L)

acc <- tm$Accumulator(5L)
stopifnot(acc$add_const(3L) == 35L)
stopifnot(acc$add(3L) == 8L)

# 4. STL containers and callbacks
stopifnot(is.null(tm$opt_double(NULL)))
stopifnot(tm$opt_double(21L) == 42L)
swapped <- tm$swap_pair(list("ok", 9L))
stopifnot(identical(swapped, list(9L, "ok")))
trip <- tm$make_triplet(1L, 2.5, "hi")
stopifnot(trip[[1L]] == 1L && abs(trip[[2L]] - 2.5) < 1e-12 && trip[[3L]] == "hi")
stopifnot(identical(tm$reverse_arr3(c(1L, 2L, 3L)), c(3L, 2L, 1L)))
stopifnot(identical(sort(tm$unique_tags(c("b", "a", "a"))), c("a", "b")))
stopifnot(tm$describe_variant(4L) == "i:4")
stopifnot(tm$describe_variant(4.25) == "d:4")
stopifnot(tm$describe_variant("x") == "s:x")
stopifnot(tm$path_filename("/tmp/demo.txt") == "demo.txt")
stopifnot(identical(tm$map_doubles(c(1.0, 2.0, 3.0), function(v) v * 10.0), c(10.0, 20.0, 30.0)))
adder <- tm$make_adder(7L)
stopifnot(adder(5L) == 12L)

# 5. Zero-copy ndarray (1D vector + column-major matrix) and data_frame
u <- c(1.0, 2.0, 3.0, 4.0)
v <- c(10.0, 20.0, 30.0, 40.0)
stopifnot(abs(tm$nd_dot_1d(u, v) - 300.0) < 1e-12)

mat <- matrix(c(1.0, 4.0, 2.0, 5.0, 3.0, 6.0), nrow = 2L, ncol = 3L)
st <- tm$nd_sum_and_trace(mat)
stopifnot(abs(st[[1L]] - 21.0) < 1e-12)
stopifnot(abs(st[[2L]] - 6.0) < 1e-12)

out1 <- tm$nd_scale_1d(u, 2.5, -1.0)
stopifnot(max(abs(out1 - (u * 2.5 - 1.0))) < 1e-12)

out2 <- tm$nd_make_2d(2L, 3L)
stopifnot(identical(dim(out2), c(2L, 3L)))
stopifnot(abs(out2[1L, 3L] - 3.0) < 1e-12)
stopifnot(abs(out2[2L, 1L] - 11.0) < 1e-12)

df <- data.frame(
    id = c(1L, 2L, 3L),
    x = c(1.5, 2.5, 3.5),
    tag = c("a", "b", "c"),
    stringsAsFactors = FALSE
)
res_df <- tm$transform_df(df)
stopifnot(is.data.frame(res_df))
stopifnot(identical(names(res_df), c("id", "x", "scaled", "tag")))
stopifnot(max(abs(res_df$scaled - c(4.0, 7.0, 10.0))) < 1e-12)
stopifnot(identical(res_df$tag, c("a", "b", "c")))

if (requireNamespace("tibble", quietly = TRUE)) {
    tb <- tibble::as_tibble(df)
    res_tb <- tm$transform_df(tb)
    stopifnot(max(abs(res_tb$scaled - c(4.0, 7.0, 10.0))) < 1e-12)
}

# 12. Smart pointers: std::shared_ptr and std::unique_ptr
sp <- tm$make_shared_node(10L, "root")
stopifnot(sp$id == 10L, sp$name == "root")
stopifnot(tm$shared_node_use_count(sp) >= 1L)
stopifnot(tm$retain_shared_node(sp) >= 1L)
stopifnot(tm$bump_retained_node(5L) == 15L)
stopifnot(sp$id == 15L)
tm$clear_retained_node()

tok <- tm$make_unique_token(7L)
stopifnot(isTRUE(tok$active()))
stopifnot(tm$consume_unique_token(tok) == 70L)
err_moved_call <- tryCatch({
    tm$consume_unique_token(tok)
    FALSE
}, error = function(e) TRUE)
stopifnot(err_moved_call)
err_moved_method <- tryCatch({
    tok$active()
    FALSE
}, error = function(e) TRUE)
stopifnot(err_moved_method)

# 13. std::complex, std::chrono, std::list
z1 <- tm$complex_add_mul(1 + 2i, 3 + 4i)
stopifnot(abs(z1 - (-3 + 5i)) < 1e-12)
z2 <- tm$complex_add_mul(2.0, 5.0)
stopifnot(abs(z2 - (2 + 5i)) < 1e-12)

d_sum <- tm$add_duration_ms(
    as.difftime(0.25, units = "secs"),
    as.difftime(750.0, units = "secs") / 1000.0
)
stopifnot(inherits(d_sum, "difftime"))
stopifnot(abs(as.numeric(d_sum, units = "secs") - 1.0) < 1e-9)

t0 <- as.POSIXct("2025-01-15 12:00:00", tz = "UTC")
t1 <- tm$shift_timestamp(t0, as.difftime(30, units = "secs"))
stopifnot(inherits(t1, "POSIXct"))
stopifnot(abs(as.numeric(difftime(t1, t0, units = "secs")) - 30.0) < 1e-6)

stopifnot(identical(tm$reverse_int_list(c(1L, 2L, 3L)), c(3L, 2L, 1L)))
stopifnot(identical(tm$tag_str_list(c("a", "b"), "p:"), c("p:a", "p:b")))

# 14. kw_only and call_guard
stopifnot(tm$kw_combine(1L, b = 2L) == 125L)
stopifnot(tm$kw_combine(a = 1L, b = 2L, c = 3L) == 123L)
err_kw_pos <- tryCatch({
    tm$kw_combine(1L, 2L)
    FALSE
}, error = function(e) TRUE)
stopifnot(err_kw_pos)

g0 <- tm$guard_counts()
gw <- tm$guarded_work(10L)
g1 <- tm$guard_counts()
stopifnot(g1[[1L]] == g0[[1L]] + 1L)
stopifnot(g1[[2L]] == g0[[2L]] + 1L)
stopifnot(gw == 10L + g1[[1L]])

# 15. Static properties and custom C++ exceptions
stopifnot(tm$StaticBox$CONST_VAL == 42L)
err_ro_static <- tryCatch({
    tm$StaticBox$CONST_VAL <- 99L
    FALSE
}, error = function(e) TRUE)
stopifnot(err_ro_static)

stopifnot(tm$StaticBox$mutable_val == 10L)
tm$StaticBox$mutable_val <- 25L
stopifnot(tm$StaticBox$mutable_val == 25L)
tm$StaticBox$mutable_val <- 10L

stopifnot(tm$StaticBox$double_const == 84L)
stopifnot(tm$StaticBox$computed == 100L)
tm$StaticBox$computed <- 250L
stopifnot(tm$StaticBox$computed == 250L)
tm$StaticBox$computed <- 100L

caught_domain <- tryCatch({
    tm$fail_domain("bad domain input")
    FALSE
}, CustomDomainError = function(e) {
    grepl("bad domain input", conditionMessage(e), fixed = TRUE)
})
stopifnot(isTRUE(caught_domain))

# 16. Iterators (make_iterator, make_key_iterator, make_value_iterator)
bag <- tm$NumberBag(c(10L, 20L, 30L), list(a = 1L, b = 2L))
stopifnot(identical(unlist(as.list(bag$`__iter__`())), c(10L, 20L, 30L)))
stopifnot(identical(unlist(as.list(bag$keys())), c("a", "b")))
stopifnot(identical(unlist(as.list(bag$values())), c(1L, 2L)))
items <- as.list(bag$items())
stopifnot(length(items) == 2L)
stopifnot(items[[1L]][[1L]] == "a", items[[1L]][[2L]] == 1L)
stopifnot(items[[2L]][[1L]] == "b", items[[2L]][[2L]] == 2L)

it <- bag$keys()
stopifnot(it$`__next__`() == "a", it$`__next__`() == "b")
stopped <- tryCatch({
    it$`__next__`()
    FALSE
}, pyrabind_stop_iteration = function(e) TRUE)
stopifnot(isTRUE(stopped))

# 17. PYRABIND_MAKE_OPAQUE, bind_vector, and bind_map
ov <- tm$OpaqueVec()
ov$append(tm$OpaqueItem(1L, "one"))
tm$push_opaque(ov, tm$OpaqueItem(2L, "two"))
stopifnot(length(ov) == 2L)
stopifnot(ov[[0L]]$tag == 1L, ov[[1L]]$label == "two")
ov[[0L]] <- tm$OpaqueItem(9L, "nine")
stopifnot(ov[[0L]]$tag == 9L, ov[[0L]]$label == "nine")
popped <- ov$pop()
stopifnot(popped$tag == 2L, length(ov) == 1L)
ov$clear()
stopifnot(length(ov) == 0L)

om <- tm$OpaqueMap()
om[["k1"]] <- tm$OpaqueItem(3L, "three")
tm$set_opaque_entry(om, "k2", tm$OpaqueItem(4L, "four"))
stopifnot(length(om) == 2L)
stopifnot(isTRUE(om$`__contains__`("k2")))
stopifnot(om[["k1"]]$tag == 3L, om[["k2"]]$label == "four")
stopifnot(identical(unlist(as.list(om$keys())), c("k1", "k2")))
om$`__delitem__`("k1")
stopifnot(length(om) == 1L, !isTRUE(om$`__contains__`("k1")))

# 18. Expanded operators (unary +, !, abs, %%, &, |, bitwise/in-place/hash)
m0 <- tm$ModInt(0L)
m3 <- tm$ModInt(3L)
m5 <- tm$ModInt(5L)
m6 <- tm$ModInt(6L)
stopifnot((+m5)$value == 5L)
stopifnot(identical(!m0, TRUE), identical(!m5, FALSE))
stopifnot((m5 %% m3)$value == 2L)
stopifnot((m6 & m3)$value == 2L)
stopifnot((tm$ModInt(4L) | tm$ModInt(1L))$value == 5L)
stopifnot(m6$`__xor__`(m3)$value == 5L)
stopifnot(m3$`__lshift__`(tm$ModInt(2L))$value == 12L)
stopifnot(tm$ModInt(12L)$`__rshift__`(tm$ModInt(2L))$value == 3L)
stopifnot(m0$`__invert__`()$value == -1L)
m5$`__iadd__`(m3)
stopifnot(m5$value == 8L)
m5$`__imod__`(m3)
stopifnot(m5$value == 2L)
stopifnot(is.numeric(tm$ModInt(7L)$`__hash__`()))
stopifnot(abs(abs(tm$SignedScalar(-3.5)) - 3.5) < 1e-12)

# 19. Nullable data_frame and zero-copy Arrow C Data Interface
null_df <- data.frame(
    id = c(1L, NA_integer_, 3L),
    x = c(1.5, NA_real_, 3.5),
    flag = c(TRUE, NA, FALSE),
    tag = c("a", NA_character_, "c"),
    stringsAsFactors = FALSE
)
stopifnot(tm$df_count_nulls(null_df) == 4L)
echo_df <- tm$df_echo(null_df)
stopifnot(identical(dim(echo_df), c(3L, 4L)))
stopifnot(is.na(echo_df$id[[2L]]), echo_df$id[[1L]] == 1L, echo_df$id[[3L]] == 3L)
stopifnot(is.na(echo_df$x[[2L]]), abs(echo_df$x[[3L]] - 3.5) < 1e-12)
stopifnot(is.na(echo_df$flag[[2L]]), isTRUE(echo_df$flag[[1L]]), identical(echo_df$flag[[3L]], FALSE))
stopifnot(is.na(echo_df$tag[[2L]]), echo_df$tag[[1L]] == "a", echo_df$tag[[3L]] == "c")

abatch <- tm$df_to_arrow(null_df)
stopifnot(inherits(abatch, "nanoarrow_array"))
round_abatch <- tm$arrow_roundtrip(abatch)
moved_arrow_err <- tryCatch({
    tm$arrow_to_df(abatch)
    FALSE
}, error = function(e) TRUE)
stopifnot(moved_arrow_err)

arrow_df <- tm$arrow_to_df(round_abatch)
stopifnot(tm$df_count_nulls(arrow_df) == 4L)
stopifnot(is.na(arrow_df$tag[[2L]]), arrow_df$tag[[3L]] == "c")

if (requireNamespace("nanoarrow", quietly = TRUE)) {
    stopifnot(inherits(nanoarrow::infer_nanoarrow_schema(tm$df_to_arrow(null_df)), "nanoarrow_schema"))
    na_conv <- nanoarrow::convert_array(tm$df_to_arrow(null_df))
    stopifnot(is.na(na_conv$tag[[2L]]), na_conv$id[[1L]] == 1L)
    na_back <- tm$arrow_to_df(nanoarrow::as_nanoarrow_array(null_df))
    stopifnot(tm$df_count_nulls(na_back) == 4L, na_back$tag[[3L]] == "c")
}

cat("All pyrabind R tests passed\n")
