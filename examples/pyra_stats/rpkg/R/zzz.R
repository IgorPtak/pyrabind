.pyrastats_env <- new.env(parent = emptyenv())

.onLoad <- function(libname, pkgname) {
    dll <- getLoadedDLLs()[[pkgname]]
    if (is.null(dll)) {
        stop("Native library for '", pkgname, "' was not loaded")
    }
    .pyrastats_env$ns <- pyrabind_load(dll)
    invisible(NULL)
}
