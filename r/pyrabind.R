# Zero-dependency base-R runtime loader and O(1) environment S3 dispatch for pyrabind.

.pyrabind_state <- new.env(parent = emptyenv())
.pyrabind_state$s3_registered <- FALSE

.pyrabind_stop_attr <- function(msg) {
    cond <- structure(
        list(message = msg, call = NULL),
        class = c("pyrabind_attribute_error", "pyrabind_error", "error", "condition")
    )
    stop(cond)
}

.pyrabind_register_s3 <- function() {
    if (isTRUE(.pyrabind_state$s3_registered)) {
        return(invisible(NULL))
    }

    base_ns <- asNamespace("base")
    utils_ns <- asNamespace("utils")

    registerS3method("$", "pyrabind_object", function(x, name) {
        meta <- attr(x, ".pb", exact = TRUE)
        prop_xptr <- get0(name, envir = meta$props, inherits = TRUE, ifnotfound = NULL)
        if (!is.null(prop_xptr)) {
            return(.Call(meta$dispatch$get_prop, x, prop_xptr))
        }
        meth_xptr <- get0(name, envir = meta$methods, inherits = TRUE, ifnotfound = NULL)
        if (!is.null(meth_xptr)) {
            disp <- meta$dispatch
            return(function(...) .External(disp$call_method, meth_xptr, x, ...))
        }
        .pyrabind_stop_attr(paste0("object has no member '", name, "'"))
    }, envir = base_ns)

    registerS3method("$<-", "pyrabind_object", function(x, name, value) {
        meta <- attr(x, ".pb", exact = TRUE)
        prop_xptr <- get0(name, envir = meta$props, inherits = TRUE, ifnotfound = NULL)
        if (is.null(prop_xptr)) {
            .pyrabind_stop_attr(paste0("object has no writable property '", name, "'"))
        }
        .Call(meta$dispatch$set_prop, x, prop_xptr, value)
        x
    }, envir = base_ns)

    registerS3method("[[", "pyrabind_object", function(x, i, ...) {
        meta <- attr(x, ".pb", exact = TRUE)
        meth_xptr <- get0("__getitem__", envir = meta$methods, inherits = TRUE, ifnotfound = NULL)
        if (is.null(meth_xptr)) {
            .pyrabind_stop_attr("object does not support indexing (__getitem__)")
        }
        .External(meta$dispatch$call_method, meth_xptr, x, i)
    }, envir = base_ns)

    registerS3method("[[<-", "pyrabind_object", function(x, i, ..., value) {
        meta <- attr(x, ".pb", exact = TRUE)
        meth_xptr <- get0("__setitem__", envir = meta$methods, inherits = TRUE, ifnotfound = NULL)
        if (is.null(meth_xptr)) {
            .pyrabind_stop_attr("object does not support item assignment (__setitem__)")
        }
        .External(meta$dispatch$call_method, meth_xptr, x, i, value)
        x
    }, envir = base_ns)

    registerS3method("length", "pyrabind_object", function(x) {
        meta <- attr(x, ".pb", exact = TRUE)
        meth_xptr <- get0("__len__", envir = meta$methods, inherits = TRUE, ifnotfound = NULL)
        if (is.null(meth_xptr)) {
            return(1L)
        }
        .External(meta$dispatch$call_method, meth_xptr, x)
    }, envir = base_ns)

    registerS3method("Ops", "pyrabind_object", function(e1, e2) {
        meta <- attr(e1, ".pb", exact = TRUE)
        if (is.null(meta) && !missing(e2)) {
            meta <- attr(e2, ".pb", exact = TRUE)
        }
        is_unary <- missing(e2)
        rhs <- if (is_unary) NULL else e2
        .Call(meta$dispatch$call_op, .Generic, e1, rhs, is_unary)
    }, envir = base_ns)

    registerS3method("print", "pyrabind_object", function(x, ...) {
        meta <- attr(x, ".pb", exact = TRUE)
        cat(.Call(meta$dispatch$call_special, x, "repr"), "\n", sep = "")
        invisible(x)
    }, envir = base_ns)

    registerS3method("format", "pyrabind_object", function(x, ...) {
        meta <- attr(x, ".pb", exact = TRUE)
        .Call(meta$dispatch$call_special, x, "repr")
    }, envir = base_ns)

    registerS3method("abs", "pyrabind_object", function(x) {
        meta <- attr(x, ".pb", exact = TRUE)
        .Call(meta$dispatch$call_special, x, "abs")
    }, envir = base_ns)

    registerS3method(".DollarNames", "pyrabind_object", function(x, pattern = "") {
        meta <- attr(x, ".pb", exact = TRUE)
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

    registerS3method("$", "pyrabind_class", function(x, name) {
        sp <- attr(x, ".pb_static_props", exact = TRUE)
        if (!is.null(sp)) {
            prop_xptr <- get0(name, envir = sp, inherits = FALSE, ifnotfound = NULL)
            if (!is.null(prop_xptr)) {
                disp <- attr(x, ".pb_dispatch", exact = TRUE)
                return(.Call(disp$get_prop, NULL, prop_xptr))
            }
        }
        st <- attr(x, ".pb_statics", exact = TRUE)
        if (!is.null(st) && exists(name, envir = st, inherits = FALSE)) {
            return(get(name, envir = st, inherits = FALSE))
        }
        .pyrabind_stop_attr(paste0("class has no static member '", name, "'"))
    }, envir = base_ns)

    registerS3method("$<-", "pyrabind_class", function(x, name, value) {
        sp <- attr(x, ".pb_static_props", exact = TRUE)
        prop_xptr <- if (!is.null(sp)) get0(name, envir = sp, inherits = FALSE, ifnotfound = NULL) else NULL
        if (is.null(prop_xptr)) {
            .pyrabind_stop_attr(paste0("class has no writable static property '", name, "'"))
        }
        disp <- attr(x, ".pb_dispatch", exact = TRUE)
        .Call(disp$set_prop, NULL, prop_xptr, value)
        x
    }, envir = base_ns)

    registerS3method("print", "pyrabind_class", function(x, ...) {
        cat("<pyrabind class '", attr(x, ".pb_name", exact = TRUE), "'>\n", sep = "")
        invisible(x)
    }, envir = base_ns)

    registerS3method("print", "pyrabind_function", function(x, ...) {
        cat("<pyrabind function '", attr(x, ".pb_name", exact = TRUE), "'>\n", sep = "")
        invisible(x)
    }, envir = base_ns)

    registerS3method("$", "pyrabind_iterator", function(x, name) {
        disp <- attr(x, ".pb", exact = TRUE)
        if (name == "__next__") {
            return(function() .Call(disp$iter_next, x))
        }
        if (name == "__iter__") {
            return(function() x)
        }
        .pyrabind_stop_attr(paste0("iterator has no member '", name, "'"))
    }, envir = base_ns)

    registerS3method("as.list", "pyrabind_iterator", function(x, ...) {
        disp <- attr(x, ".pb", exact = TRUE)
        .Call(disp$iter_collect, x)
    }, envir = base_ns)

    registerS3method("print", "pyrabind_iterator", function(x, ...) {
        cat("<pyrabind iterator>\n")
        invisible(x)
    }, envir = base_ns)

    registerS3method("$", "pyrabind_enum", function(x, name) {
        disp <- attr(x, ".pb", exact = TRUE)
        if (name == "name" || name == "value") {
            return(.Call(disp$call_enum_op, name, x, NULL))
        }
        .pyrabind_stop_attr(paste0("enum has no member '", name, "'"))
    }, envir = base_ns)

    registerS3method("as.integer", "pyrabind_enum", function(x, ...) {
        disp <- attr(x, ".pb", exact = TRUE)
        as.integer(.Call(disp$call_enum_op, "value", x, NULL))
    }, envir = base_ns)

    registerS3method("Ops", "pyrabind_enum", function(e1, e2) {
        if (missing(e2) || !(.Generic %in% c("==", "!="))) {
            stop("unsupported operation on pyrabind enum")
        }
        disp <- attr(e1, ".pb", exact = TRUE)
        .Call(disp$call_enum_op, .Generic, e1, e2)
    }, envir = base_ns)

    registerS3method("print", "pyrabind_enum", function(x, ...) {
        disp <- attr(x, ".pb", exact = TRUE)
        cat(.Call(disp$call_enum_op, "repr", x, NULL), "\n", sep = "")
        invisible(x)
    }, envir = base_ns)

    .pyrabind_state$s3_registered <- TRUE
    invisible(NULL)
}

pyrabind_load <- function(path) {
    .pyrabind_register_s3()
    dll <- if (inherits(path, "DLLInfo")) path else dyn.load(path)

    sym <- function(name) {
        getNativeSymbolInfo(name, dll)
    }

    dispatch <- new.env(parent = emptyenv())
    dispatch$module_env <- sym("pyrabind_module_env")
    dispatch$call_fn <- sym("pyrabind_ext_call_fn")
    dispatch$construct <- sym("pyrabind_ext_construct")
    dispatch$call_method <- sym("pyrabind_ext_call_method")
    dispatch$get_prop <- sym("pyrabind_get_prop")
    dispatch$set_prop <- sym("pyrabind_set_prop")
    dispatch$call_op <- sym("pyrabind_call_op")
    dispatch$call_special <- sym("pyrabind_call_special")
    dispatch$call_enum_op <- sym("pyrabind_call_enum_op")
    dispatch$iter_next <- sym("pyrabind_iter_next")
    dispatch$iter_collect <- sym("pyrabind_iter_collect")

    dispatch$make_fn <- function(fn_xptr, name, doc) {
        force(fn_xptr)
        fn <- function(...) .External(dispatch$call_fn, fn_xptr, ...)
        attr(fn, ".pb_name") <- name
        attr(fn, ".pb_doc") <- doc
        class(fn) <- c("pyrabind_function", "function")
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

    dispatch$make_class <- function(cls_xptr, qname, doc, statics_env, static_props_env) {
        force(cls_xptr)
        ctor <- function(...) .External(dispatch$construct, cls_xptr, ...)
        attr(ctor, ".pb_name") <- qname
        attr(ctor, ".pb_doc") <- doc
        attr(ctor, ".pb_statics") <- statics_env
        attr(ctor, ".pb_static_props") <- static_props_env
        attr(ctor, ".pb_dispatch") <- dispatch
        class(ctor) <- c("pyrabind_class", "function")
        ctor
    }

    .Call(dispatch$module_env, dispatch)
}
