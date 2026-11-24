# Zero-dependency base-R runtime loader and O(1) environment S3 dispatch for nanorbind.

.nanorbind_state <- new.env(parent = emptyenv())
.nanorbind_state$s3_registered <- FALSE

.nanorbind_stop_attr <- function(msg) {
    cond <- structure(
        list(message = msg, call = NULL),
        class = c("nanorbind_attribute_error", "nanorbind_error", "error", "condition")
    )
    stop(cond)
}

.nanorbind_register_s3 <- function() {
    if (isTRUE(.nanorbind_state$s3_registered)) {
        return(invisible(NULL))
    }

    base_ns <- asNamespace("base")
    utils_ns <- asNamespace("utils")

    registerS3method("$", "nanorbind_object", function(x, name) {
        meta <- attr(x, ".nr", exact = TRUE)
        prop_xptr <- get0(name, envir = meta$props, inherits = TRUE, ifnotfound = NULL)
        if (!is.null(prop_xptr)) {
            return(.Call(meta$dispatch$get_prop, x, prop_xptr))
        }
        meth_xptr <- get0(name, envir = meta$methods, inherits = TRUE, ifnotfound = NULL)
        if (!is.null(meth_xptr)) {
            disp <- meta$dispatch
            return(function(...) .External(disp$call_method, meth_xptr, x, ...))
        }
        .nanorbind_stop_attr(paste0("object has no member '", name, "'"))
    }, envir = base_ns)

    registerS3method("$<-", "nanorbind_object", function(x, name, value) {
        meta <- attr(x, ".nr", exact = TRUE)
        prop_xptr <- get0(name, envir = meta$props, inherits = TRUE, ifnotfound = NULL)
        if (is.null(prop_xptr)) {
            .nanorbind_stop_attr(paste0("object has no writable property '", name, "'"))
        }
        .Call(meta$dispatch$set_prop, x, prop_xptr, value)
        x
    }, envir = base_ns)

    registerS3method("[[", "nanorbind_object", function(x, i, ...) {
        meta <- attr(x, ".nr", exact = TRUE)
        meth_xptr <- get0("__getitem__", envir = meta$methods, inherits = TRUE, ifnotfound = NULL)
        if (is.null(meth_xptr)) {
            .nanorbind_stop_attr("object does not support indexing (__getitem__)")
        }
        .External(meta$dispatch$call_method, meth_xptr, x, i)
    }, envir = base_ns)

    registerS3method("[[<-", "nanorbind_object", function(x, i, ..., value) {
        meta <- attr(x, ".nr", exact = TRUE)
        meth_xptr <- get0("__setitem__", envir = meta$methods, inherits = TRUE, ifnotfound = NULL)
        if (is.null(meth_xptr)) {
            .nanorbind_stop_attr("object does not support item assignment (__setitem__)")
        }
        .External(meta$dispatch$call_method, meth_xptr, x, i, value)
        x
    }, envir = base_ns)

    registerS3method("length", "nanorbind_object", function(x) {
        meta <- attr(x, ".nr", exact = TRUE)
        meth_xptr <- get0("__len__", envir = meta$methods, inherits = TRUE, ifnotfound = NULL)
        if (is.null(meth_xptr)) {
            return(1L)
        }
        .External(meta$dispatch$call_method, meth_xptr, x)
    }, envir = base_ns)

    registerS3method("Ops", "nanorbind_object", function(e1, e2) {
        meta <- attr(e1, ".nr", exact = TRUE)
        if (is.null(meta) && !missing(e2)) {
            meta <- attr(e2, ".nr", exact = TRUE)
        }
        is_unary <- missing(e2)
        rhs <- if (is_unary) NULL else e2
        .Call(meta$dispatch$call_op, .Generic, e1, rhs, is_unary)
    }, envir = base_ns)

    registerS3method("print", "nanorbind_object", function(x, ...) {
        meta <- attr(x, ".nr", exact = TRUE)
        cat(.Call(meta$dispatch$call_special, x, "repr"), "\n", sep = "")
        invisible(x)
    }, envir = base_ns)

    registerS3method("format", "nanorbind_object", function(x, ...) {
        meta <- attr(x, ".nr", exact = TRUE)
        .Call(meta$dispatch$call_special, x, "repr")
    }, envir = base_ns)

    registerS3method(".DollarNames", "nanorbind_object", function(x, pattern = "") {
        meta <- attr(x, ".nr", exact = TRUE)
        names_all <- character(0)
        e <- meta$props
        while (!identical(e, emptyenv())) {
            names_all <- c(names_all, ls(e, all.names = TRUE))
            e <- parent.env(e)
        }
        e <- meta$methods
        while (!identical(e, emptyenv())) {
            names_all <- c(names_all, ls(e, all.names = TRUE))
            e <- parent.env(e)
        }
        grep(pattern, unique(names_all), value = TRUE)
    }, envir = utils_ns)

    registerS3method("$", "nanorbind_class", function(x, name) {
        st <- attr(x, ".nr_statics", exact = TRUE)
        if (!is.null(st) && exists(name, envir = st, inherits = FALSE)) {
            return(get(name, envir = st, inherits = FALSE))
        }
        .nanorbind_stop_attr(paste0("class has no static member '", name, "'"))
    }, envir = base_ns)

    registerS3method("print", "nanorbind_class", function(x, ...) {
        cat("<nanorbind class '", attr(x, ".nr_name", exact = TRUE), "'>\n", sep = "")
        invisible(x)
    }, envir = base_ns)

    registerS3method("print", "nanorbind_function", function(x, ...) {
        cat("<nanorbind function '", attr(x, ".nr_name", exact = TRUE), "'>\n", sep = "")
        invisible(x)
    }, envir = base_ns)

    registerS3method("$", "nanorbind_enum", function(x, name) {
        disp <- attr(x, ".nr", exact = TRUE)
        if (name == "name" || name == "value") {
            return(.Call(disp$call_enum_op, name, x, NULL))
        }
        .nanorbind_stop_attr(paste0("enum has no member '", name, "'"))
    }, envir = base_ns)

    registerS3method("as.integer", "nanorbind_enum", function(x, ...) {
        disp <- attr(x, ".nr", exact = TRUE)
        as.integer(.Call(disp$call_enum_op, "value", x, NULL))
    }, envir = base_ns)

    registerS3method("Ops", "nanorbind_enum", function(e1, e2) {
        if (missing(e2) || !(.Generic %in% c("==", "!="))) {
            stop("unsupported operation on nanorbind enum")
        }
        disp <- attr(e1, ".nr", exact = TRUE)
        .Call(disp$call_enum_op, .Generic, e1, e2)
    }, envir = base_ns)

    registerS3method("print", "nanorbind_enum", function(x, ...) {
        disp <- attr(x, ".nr", exact = TRUE)
        cat(.Call(disp$call_enum_op, "repr", x, NULL), "\n", sep = "")
        invisible(x)
    }, envir = base_ns)

    .nanorbind_state$s3_registered <- TRUE
    invisible(NULL)
}

nanorbind_load <- function(path, module_name = NULL) {
    .nanorbind_register_s3()
    dll <- dyn.load(path)

    sym <- function(name) {
        getNativeSymbolInfo(name, dll)
    }

    dispatch <- new.env(parent = emptyenv())
    dispatch$module_env <- sym("nanorbind_module_env")
    dispatch$call_fn <- sym("nanorbind_ext_call_fn")
    dispatch$construct <- sym("nanorbind_ext_construct")
    dispatch$call_method <- sym("nanorbind_ext_call_method")
    dispatch$get_prop <- sym("nanorbind_get_prop")
    dispatch$set_prop <- sym("nanorbind_set_prop")
    dispatch$call_op <- sym("nanorbind_call_op")
    dispatch$call_special <- sym("nanorbind_call_special")
    dispatch$call_enum_op <- sym("nanorbind_call_enum_op")

    dispatch$make_fn <- function(fn_xptr, name, doc) {
        force(fn_xptr)
        fn <- function(...) .External(dispatch$call_fn, fn_xptr, ...)
        attr(fn, ".nr_name") <- name
        attr(fn, ".nr_doc") <- doc
        class(fn) <- c("nanorbind_function", "function")
        fn
    }

    dispatch$make_class_meta <- function(parent_meta) {
        meta <- new.env(parent = emptyenv())
        meta$dispatch <- dispatch
        meta$methods <- new.env(
            hash = TRUE,
            parent = if (is.null(parent_meta)) emptyenv() else parent_meta$methods
        )
        meta$props <- new.env(
            hash = TRUE,
            parent = if (is.null(parent_meta)) emptyenv() else parent_meta$props
        )
        meta
    }

    dispatch$make_class <- function(cls_xptr, qname, doc, statics_env) {
        force(cls_xptr)
        ctor <- function(...) .External(dispatch$construct, cls_xptr, ...)
        attr(ctor, ".nr_name") <- qname
        attr(ctor, ".nr_doc") <- doc
        attr(ctor, ".nr_statics") <- statics_env
        class(ctor) <- c("nanorbind_class", "function")
        ctor
    }

    .Call(dispatch$module_env, dispatch)
}
