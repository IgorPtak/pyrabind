args <- commandArgs(trailingOnly = TRUE)
source(args[[1]])
tm <- pyrabind_load(args[[2]])

t0 <- as.POSIXct("2025-01-15 12:00:00", tz = "UTC")
delta15 <- as.difftime(15, units = "secs")
v1 <- as.numeric(1:6)
v2 <- as.numeric(6:1)
df <- data.frame(
    id = c(1L, 2L, 3L),
    x = c(1.5, 2.5, 3.5),
    tag = c("a", "b", "c"),
    stringsAsFactors = FALSE
)

gctorture(TRUE)
on.exit(gctorture(FALSE), add = TRUE)

# Functions, kwargs, overloads, submodules, constants
stopifnot(abs(tm$PI - 3.141592653589793) < 1e-12)
stopifnot(tm$add_defaults(5L, c = 200L) == 215L)
stopifnot(tm$classify(7L) == "int:7")
stopifnot(tm$classify(7.5) == "double:7")
stopifnot(abs(tm$math$square(4.5) - 20.25) < 1e-12)

# Enums and error boundary + custom exception
stopifnot(as.integer(tm$Color$Green) == 2L)
caught_val <- FALSE
tryCatch(tm$check_positive(-1L), pyrabind_value_error = function(e) caught_val <<- TRUE)
stopifnot(caught_val)

caught_dom <- tryCatch({
    tm$fail_domain("gc stress error")
    FALSE
}, CustomDomainError = function(e) {
    grepl("gc stress error", conditionMessage(e), fixed = TRUE)
})
stopifnot(isTRUE(caught_dom))

# Classes, operators, inheritance, indexing, keep_alive, properties
a <- tm$Vec2(1.0, 2.0)
b <- tm$Vec2(3.0, 4.0)
c <- 2.0 * a + b
stopifnot(c == tm$Vec2(5.0, 8.0))
stopifnot(-a == tm$Vec2(-1.0, -2.0))

dog <- tm$Dog("Rex")
stopifnot(dog$speak() == "Rex: woof")
stopifnot(tm$greet_animal(dog) == "hello Rex: woof")

box <- tm$Container(3L, 42L)
box[[-1L]] <- 77L
stopifnot(box[[2L]] == 77L)
emb <- box$get_embedded()
stopifnot(emb$id == 99L)
res <- tm$Resource(123L)
box$attach(res)
rm(res)
stopifnot(box$attached_id() == 123L)

# STL containers, callbacks, complex, chrono, list
swapped <- tm$swap_pair(list("ok", 9L))
stopifnot(identical(swapped, list(9L, "ok")))
stopifnot(identical(tm$map_doubles(c(1.0, 2.0), function(v) v * 10.0), c(10.0, 20.0)))
adder <- tm$make_adder(7L)
stopifnot(adder(5L) == 12L)
z <- tm$complex_add_mul(1 + 2i, 3 + 4i)
stopifnot(abs(z - (-3 + 5i)) < 1e-12)
t1 <- tm$shift_timestamp(t0, delta15)
stopifnot(abs(as.numeric(difftime(t1, t0, units = "secs")) - 15.0) < 1e-6)
stopifnot(identical(tm$tag_str_list(c("a", "b"), "x:"), c("x:a", "x:b")))

# Smart pointers + relinquish
sp <- tm$make_shared_node(10L, "root")
stopifnot(tm$retain_shared_node(sp) >= 1L)
stopifnot(tm$bump_retained_node(5L) == 15L)
stopifnot(sp$id == 15L)
tm$clear_retained_node()

tok <- tm$make_unique_token(7L)
stopifnot(tm$consume_unique_token(tok) == 70L)

# kw_only, call_guard, static properties
stopifnot(tm$kw_combine(1L, b = 2L) == 125L)
g0 <- tm$guard_counts()
gw <- tm$guarded_work(10L)
g1 <- tm$guard_counts()
stopifnot(g1[[1L]] == g0[[1L]] + 1L, gw == 10L + g1[[1L]])

tm$StaticBox$mutable_val <- 25L
stopifnot(tm$StaticBox$mutable_val == 25L)
tm$StaticBox$mutable_val <- 10L

# Iterators, opaque containers, operators
bag <- tm$NumberBag(c(10L, 20L), list(a = 1L, b = 2L))
stopifnot(identical(unlist(as.list(bag$keys())), c("a", "b")))
items <- as.list(bag$items())
stopifnot(length(items) == 2L)
it <- bag$keys()
stopifnot(it$`__next__`() == "a", it$`__next__`() == "b")

ov <- tm$OpaqueVec()
tm$push_opaque(ov, tm$OpaqueItem(2L, "two"))
stopifnot(length(ov) == 1L, ov[[0L]]$label == "two")

om <- tm$OpaqueMap()
tm$set_opaque_entry(om, "k", tm$OpaqueItem(4L, "four"))
stopifnot(om[["k"]]$tag == 4L)

m0 <- tm$ModInt(0L)
m5 <- tm$ModInt(5L)
stopifnot(identical(!m0, TRUE), identical(!m5, FALSE))
stopifnot(abs(abs(tm$SignedScalar(-3.5)) - 3.5) < 1e-12)

# ndarray & data_frame
stopifnot(abs(tm$nd_dot_1d(v1, v2) - sum(v1 * v2)) < 1e-9)
out1 <- tm$nd_scale_1d(v1, 2.0, 1.0)
stopifnot(max(abs(out1 - (v1 * 2.0 + 1.0))) < 1e-12)
out2 <- tm$nd_make_2d(3L, 4L)
stopifnot(identical(dim(out2), c(3L, 4L)), out2[2L, 1L] == 11.0)

res_df <- tm$transform_df(df)
stopifnot(max(abs(res_df$scaled - c(4.0, 7.0, 10.0))) < 1e-12)

null_df <- data.frame(
    id = c(1L, NA_integer_, 3L),
    x = c(1.5, NA_real_, 3.5),
    flag = c(TRUE, NA, FALSE),
    tag = c("a", NA_character_, "c"),
    stringsAsFactors = FALSE
)
stopifnot(tm$df_count_nulls(null_df) == 4L)
abatch <- tm$arrow_roundtrip(tm$df_to_arrow(null_df))
stopifnot(tm$df_count_nulls(tm$arrow_to_df(abatch)) == 4L)

gctorture(FALSE)
gc(FALSE)
cat("gctorture(TRUE) R stress test passed\n")
