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

cat("All pyrabind R tests passed\n")
