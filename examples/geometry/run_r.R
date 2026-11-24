args <- commandArgs(trailingOnly = TRUE)
source(args[[1]])
geo <- pyrabind_load(args[[2]])

a <- geo$Vec2(1.0, 2.0)
b <- geo$Vec2(4.0, 6.0)
c <- 2.0 * a + b
stopifnot(abs(c$x - 6.0) < 1e-12)
stopifnot(abs(c$y - 10.0) < 1e-12)
stopifnot(abs(c$norm - sqrt(6.0^2 + 10.0^2)) < 1e-12)

u <- c(1.0, 2.0, 3.0)
v <- c(4.0, 5.0, 6.0)
stopifnot(abs(geo$array_dot(u, v) - 32.0) < 1e-12)
cat("geometry R example passed\n")
