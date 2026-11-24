#include "internal.hpp"

#include <cstring>
#include <deque>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace pyrabind::r_detail {

namespace {

void parse_external_pairlist(SEXP ext_args, call_args& cargs, SEXP& target_xptr_out,
                             bool prepend_self) {
    // ext_args = (dispatch_tag, target_xptr, [self,] user_args...)
    SEXP cur = CDR(ext_args);
    if (cur == R_NilValue) {
        throw type_error("malformed .External call: missing target handle");
    }
    target_xptr_out = CAR(cur);
    cur = CDR(cur);

    if (prepend_self) {
        if (cur == R_NilValue) {
            throw type_error("malformed .External method call: missing self");
        }
        cargs.add_pos(CAR(cur));
        cur = CDR(cur);
    }

    while (cur != R_NilValue) {
        SEXP val = CAR(cur);
        if (val == R_MissingArg) {
            throw type_error("missing positional or keyword argument");
        }
        SEXP tag = TAG(cur);
        if (tag == R_NilValue || TYPEOF(tag) != SYMSXP) {
            cargs.add_pos(val);
        } else {
            const char* kw = CHAR(PRINTNAME(tag));
            if (kw != nullptr && kw[0] != '\0') {
                cargs.add_kw(kw, val);
            } else {
                cargs.add_pos(val);
            }
        }
        cur = CDR(cur);
    }
}

const char* r_op_to_dunder(std::string_view op, bool is_unary, const char*& rev_dunder,
                           const char*& flipped_cmp) noexcept {
    rev_dunder = nullptr;
    flipped_cmp = nullptr;
    if (is_unary && op == "-") {
        return "__neg__";
    }
    if (is_unary && op == "+") {
        return "__pos__";
    }
    if (is_unary && op == "!") {
        return "__bool__";
    }
    if (op == "+") {
        rev_dunder = "__radd__";
        return "__add__";
    }
    if (op == "-") {
        rev_dunder = "__rsub__";
        return "__sub__";
    }
    if (op == "*") {
        rev_dunder = "__rmul__";
        return "__mul__";
    }
    if (op == "/") {
        rev_dunder = "__rtruediv__";
        return "__truediv__";
    }
    if (op == "%%") {
        rev_dunder = "__rmod__";
        return "__mod__";
    }
    if (op == "&") {
        rev_dunder = "__rand__";
        return "__and__";
    }
    if (op == "|") {
        rev_dunder = "__ror__";
        return "__or__";
    }
    if (op == "==") {
        flipped_cmp = "__eq__";
        return "__eq__";
    }
    if (op == "!=") {
        flipped_cmp = "__ne__";
        return "__ne__";
    }
    if (op == "<") {
        flipped_cmp = "__gt__";
        return "__lt__";
    }
    if (op == "<=") {
        flipped_cmp = "__ge__";
        return "__le__";
    }
    if (op == ">") {
        flipped_cmp = "__lt__";
        return "__gt__";
    }
    if (op == ">=") {
        flipped_cmp = "__le__";
        return "__ge__";
    }
    return nullptr;
}

struct RIterHolder {
    std::function<SEXP(bool&, SEXP)> next_fn;
};

void r_iter_finalizer(SEXP xptr) noexcept {
    void* raw = R_ExternalPtrAddr(xptr);
    if (raw != nullptr) {
        delete static_cast<RIterHolder*>(raw);
        R_ClearExternalPtr(xptr);
    }
}

const class_record* find_class_of_sexp(SEXP obj) noexcept {
    auto& rt = RRuntimeState::instance();
    if (obj != nullptr && obj != R_NilValue && TYPEOF(obj) == EXTPTRSXP &&
        R_ExternalPtrTag(obj) == rt.instance_tag) {
        const auto* h = static_cast<const RInstanceHolder*>(R_ExternalPtrAddr(obj));
        if (h != nullptr) {
            return h->cls;
        }
    }
    return nullptr;
}

SEXP make_fn_sexp(const function_record* fn, SEXP disp_env, SEXP make_fn_closure) {
    auto& rt = RRuntimeState::instance();
    SEXP xptr = PROTECT(R_MakeExternalPtr(const_cast<function_record*>(fn), rt.fn_tag, R_NilValue));
    SEXP name_s = PROTECT(Rf_mkString(fn->name.c_str()));
    SEXP doc_s = PROTECT(Rf_mkString(fn->doc.c_str()));
    SEXP call = PROTECT(Rf_lang4(make_fn_closure, xptr, name_s, doc_s));
    SEXP out = cpp11::unwind_protect([&]() { return Rf_eval(call, disp_env); });
    UNPROTECT(4);
    return out;
}

void build_r_class_cache(const class_record* cls, SEXP disp_env, SEXP make_cls_meta_closure) {
    auto& rt = RRuntimeState::instance();
    if (rt.class_cache.find(cls) != rt.class_cache.end()) {
        return;
    }

    SEXP parent_meta = R_NilValue;
    std::vector<std::string> hierarchy;
    hierarchy.push_back(cls->qualified_name);

    if (cls->base != nullptr) {
        build_r_class_cache(cls->base, disp_env, make_cls_meta_closure);
        const auto& base_entry = rt.class_cache.at(cls->base);
        parent_meta = base_entry.cls_env.data();
        const class_record* cur = cls->base;
        while (cur != nullptr) {
            hierarchy.push_back(cur->qualified_name);
            cur = cur->base;
        }
    }

    SEXP call = PROTECT(Rf_lang2(make_cls_meta_closure, parent_meta));
    SEXP cls_env = PROTECT(cpp11::unwind_protect([&]() { return Rf_eval(call, disp_env); }));

    SEXP methods_env = R_getVar(Rf_install("methods"), cls_env, FALSE);
    SEXP props_env = R_getVar(Rf_install("props"), cls_env, FALSE);

    for (const auto& m : cls->methods) {
        SEXP mx =
            PROTECT(R_MakeExternalPtr(const_cast<function_record*>(&m), rt.meth_tag, R_NilValue));
        SEXP sym = PROTECT(Rf_install(m.name.c_str()));
        Rf_defineVar(sym, mx, methods_env);
        UNPROTECT(2);
    }

    for (const auto& p : cls->properties) {
        SEXP px =
            PROTECT(R_MakeExternalPtr(const_cast<property_record*>(&p), rt.prop_tag, R_NilValue));
        SEXP sym = PROTECT(Rf_install(p.name.c_str()));
        Rf_defineVar(sym, px, props_env);
        UNPROTECT(2);
    }

    const R_xlen_t nh = static_cast<R_xlen_t>(hierarchy.size()) + 1;
    SEXP s3_cls = PROTECT(cpp11::safe[Rf_allocVector](STRSXP, nh));
    for (R_xlen_t i = 0; i < nh - 1; ++i) {
        const auto& qname = hierarchy[static_cast<std::size_t>(i)];
        SET_STRING_ELT(s3_cls, i,
                       Rf_mkCharLenCE(qname.data(), static_cast<int>(qname.size()), CE_UTF8));
    }
    SET_STRING_ELT(s3_cls, nh - 1, Rf_mkChar("pyrabind_object"));

    RClassCache cached;
    cached.cls_env = cpp11::sexp(cls_env);
    cached.s3_classes = cpp11::sexp(s3_cls);
    rt.class_cache.emplace(cls, std::move(cached));

    UNPROTECT(3);
}

void build_r_enum_cache(const enum_record* en) {
    auto& rt = RRuntimeState::instance();
    if (rt.enum_cache.find(en) != rt.enum_cache.end()) {
        return;
    }
    SEXP s3_cls = PROTECT(cpp11::safe[Rf_allocVector](STRSXP, 2));
    SET_STRING_ELT(s3_cls, 0,
                   Rf_mkCharLenCE(en->qualified_name.data(),
                                  static_cast<int>(en->qualified_name.size()), CE_UTF8));
    SET_STRING_ELT(s3_cls, 1, Rf_mkChar("pyrabind_enum"));

    REnumCache cached;
    cached.s3_classes = cpp11::sexp(s3_cls);
    rt.enum_cache.emplace(en, std::move(cached));
    UNPROTECT(1);
}

SEXP build_module_env_rec(module_record& mod, SEXP disp_env) {
    auto& rt = RRuntimeState::instance();
    SEXP make_fn = R_getVar(Rf_install("make_fn"), disp_env, FALSE);
    SEXP make_cls = R_getVar(Rf_install("make_class"), disp_env, FALSE);
    SEXP make_cls_meta = R_getVar(Rf_install("make_class_meta"), disp_env, FALSE);

    SEXP env = PROTECT(cpp11::unwind_protect([&]() {
        SEXP new_env_fn = Rf_findFun(Rf_install("new.env"), R_BaseNamespace);
        SEXP args = PROTECT(Rf_lang2(new_env_fn, R_EmptyEnv));
        SEXP res = Rf_eval(args, R_BaseNamespace);
        UNPROTECT(1);
        return res;
    }));

    for (const auto& cls : mod.classes) {
        build_r_class_cache(&cls, disp_env, make_cls_meta);

        SEXP statics_env = PROTECT(cpp11::unwind_protect([&]() {
            SEXP new_env_fn = Rf_findFun(Rf_install("new.env"), R_BaseNamespace);
            SEXP args = PROTECT(Rf_lang2(new_env_fn, R_EmptyEnv));
            SEXP res = Rf_eval(args, R_BaseNamespace);
            UNPROTECT(1);
            return res;
        }));
        for (const auto& sm : cls.static_methods) {
            SEXP sfn = PROTECT(make_fn_sexp(&sm, disp_env, make_fn));
            Rf_defineVar(Rf_install(sm.name.c_str()), sfn, statics_env);
            UNPROTECT(1);
        }

        SEXP static_props_env = PROTECT(cpp11::unwind_protect([&]() {
            SEXP new_env_fn = Rf_findFun(Rf_install("new.env"), R_BaseNamespace);
            SEXP args = PROTECT(Rf_lang2(new_env_fn, R_EmptyEnv));
            SEXP res = Rf_eval(args, R_BaseNamespace);
            UNPROTECT(1);
            return res;
        }));
        for (const auto& sp : cls.static_properties) {
            SEXP px = PROTECT(
                R_MakeExternalPtr(const_cast<property_record*>(&sp), rt.prop_tag, R_NilValue));
            Rf_defineVar(Rf_install(sp.name.c_str()), px, static_props_env);
            UNPROTECT(1);
        }

        SEXP cls_xptr =
            PROTECT(R_MakeExternalPtr(const_cast<class_record*>(&cls), rt.cls_tag, R_NilValue));
        SEXP qname_s = PROTECT(Rf_mkString(cls.qualified_name.c_str()));
        SEXP doc_s = PROTECT(Rf_mkString(cls.doc.c_str()));
        SEXP call =
            PROTECT(Rf_lang6(make_cls, cls_xptr, qname_s, doc_s, statics_env, static_props_env));
        SEXP ctor = PROTECT(cpp11::unwind_protect([&]() { return Rf_eval(call, disp_env); }));
        Rf_defineVar(Rf_install(cls.name.c_str()), ctor, env);
        UNPROTECT(7);
    }

    for (const auto& en : mod.enums) {
        build_r_enum_cache(&en);

        SEXP enum_env = PROTECT(cpp11::unwind_protect([&]() {
            SEXP new_env_fn = Rf_findFun(Rf_install("new.env"), R_BaseNamespace);
            SEXP args = PROTECT(Rf_lang2(new_env_fn, R_EmptyEnv));
            SEXP res = Rf_eval(args, R_BaseNamespace);
            UNPROTECT(1);
            return res;
        }));
        for (const auto& entry : en.entries) {
            SEXP val = PROTECT(r_wrap_enum(&en, entry.value));
            Rf_defineVar(Rf_install(entry.name.c_str()), val, enum_env);
            if (en.exported) {
                Rf_defineVar(Rf_install(entry.name.c_str()), val, env);
            }
            UNPROTECT(1);
        }
        Rf_defineVar(Rf_install(en.name.c_str()), enum_env, env);
        UNPROTECT(1);
    }

    for (const auto& fn : mod.functions) {
        SEXP f = PROTECT(make_fn_sexp(&fn, disp_env, make_fn));
        Rf_defineVar(Rf_install(fn.name.c_str()), f, env);
        UNPROTECT(1);
    }

    for (const auto& [cname, cfn] : mod.constants) {
        SEXP val = PROTECT(cfn());
        Rf_defineVar(Rf_install(cname.c_str()), val, env);
        UNPROTECT(1);
    }

    for (auto& sub_ptr : mod.submodules) {
        SEXP sub_env = PROTECT(build_module_env_rec(*sub_ptr, disp_env));
        Rf_defineVar(Rf_install(sub_ptr->name.c_str()), sub_env, env);
        UNPROTECT(1);
    }

    UNPROTECT(1);
    return env;
}

} // namespace

SEXP r_create_module_env(const char* name, SEXP dispatch_env, void (*init_fn)(module_record&)) {
    return r_error_boundary([&]() -> SEXP {
        static std::deque<std::unique_ptr<module_record>> storage_pool;
        auto& rt = RRuntimeState::instance();
        rt.ensure_symbols();
        rt.dispatch_env = cpp11::sexp(dispatch_env);

        auto mod_rec = std::make_unique<module_record>();
        mod_rec->name = name;
        mod_rec->qualified_name = name;

        init_fn(*mod_rec);
        type_registry::instance().resolve_bases();

        SEXP out = build_module_env_rec(*mod_rec, dispatch_env);
        storage_pool.push_back(std::move(mod_rec));
        return out;
    });
}

SEXP r_wrap_iterator(std::function<SEXP(bool&, SEXP)> next_fn) {
    auto& rt = RRuntimeState::instance();
    rt.ensure_symbols();
    auto* holder = new RIterHolder{std::move(next_fn)};
    SEXP xptr = PROTECT(R_MakeExternalPtr(holder, rt.iter_tag, R_NilValue));
    R_RegisterCFinalizerEx(xptr, &r_iter_finalizer, TRUE);
    SEXP s3 = PROTECT(Rf_mkString("pyrabind_iterator"));
    Rf_setAttrib(xptr, R_ClassSymbol, s3);
    if (rt.dispatch_env.data() != R_NilValue) {
        Rf_setAttrib(xptr, rt.pb_attr_sym, rt.dispatch_env.data());
    }
    UNPROTECT(2);
    return xptr;
}

extern "C" {

PYRABIND_R_EXPORT SEXP pyrabind_ext_call_fn(SEXP args) {
    return r_error_boundary([&]() -> SEXP {
        auto& rt = RRuntimeState::instance();
        rt.ensure_symbols();

        call_args cargs;
        SEXP fn_xptr = R_NilValue;
        parse_external_pairlist(args, cargs, fn_xptr, false);

        if (TYPEOF(fn_xptr) != EXTPTRSXP || R_ExternalPtrTag(fn_xptr) != rt.fn_tag) {
            throw type_error("invalid pyrabind function handle");
        }
        const function_record* rec = nullptr;
        void* addr = R_ExternalPtrAddr(fn_xptr);
        if (addr == nullptr) {
            throw type_error("null pyrabind function handle");
        }
        if (R_ExternalPtrProtected(fn_xptr) == rt.fn_tag) {
            rec = static_cast<const RFuncHolder*>(addr)->rec;
        } else {
            rec = static_cast<const function_record*>(addr);
        }
        return dispatch_overload_list(rec->overloads, cargs, rec->name, R_NilValue);
    });
}

PYRABIND_R_EXPORT SEXP pyrabind_ext_construct(SEXP args) {
    return r_error_boundary([&]() -> SEXP {
        auto& rt = RRuntimeState::instance();
        rt.ensure_symbols();

        call_args cargs;
        SEXP cls_xptr = R_NilValue;
        parse_external_pairlist(args, cargs, cls_xptr, false);

        if (TYPEOF(cls_xptr) != EXTPTRSXP || R_ExternalPtrTag(cls_xptr) != rt.cls_tag) {
            throw type_error("invalid pyrabind class handle");
        }
        const auto* cls = static_cast<const class_record*>(R_ExternalPtrAddr(cls_xptr));
        if (cls == nullptr) {
            throw type_error("null pyrabind class handle");
        }
        return dispatch_overload_list(cls->constructors, cargs, cls->qualified_name, R_NilValue);
    });
}

PYRABIND_R_EXPORT SEXP pyrabind_ext_call_method(SEXP args) {
    return r_error_boundary([&]() -> SEXP {
        auto& rt = RRuntimeState::instance();
        rt.ensure_symbols();

        call_args cargs;
        SEXP meth_xptr = R_NilValue;
        parse_external_pairlist(args, cargs, meth_xptr, true);

        if (TYPEOF(meth_xptr) != EXTPTRSXP || R_ExternalPtrTag(meth_xptr) != rt.meth_tag) {
            throw type_error("invalid pyrabind method handle");
        }
        const auto* meth = static_cast<const function_record*>(R_ExternalPtrAddr(meth_xptr));
        if (meth == nullptr) {
            throw type_error("null pyrabind method handle");
        }
        return dispatch_overload_list(meth->overloads, cargs, meth->name, cargs.pos_at(0));
    });
}

PYRABIND_R_EXPORT SEXP pyrabind_call_get_prop(SEXP obj, SEXP prop_xptr) {
    return r_error_boundary([&]() -> SEXP {
        auto& rt = RRuntimeState::instance();
        rt.ensure_symbols();
        if (TYPEOF(prop_xptr) != EXTPTRSXP || R_ExternalPtrTag(prop_xptr) != rt.prop_tag) {
            throw type_error("invalid pyrabind property handle");
        }
        const auto* prop = static_cast<const property_record*>(R_ExternalPtrAddr(prop_xptr));
        if (prop == nullptr || !prop->getter) {
            throw attribute_error("property is unreadable");
        }
        return prop->getter(obj);
    });
}

PYRABIND_R_EXPORT SEXP pyrabind_call_set_prop(SEXP obj, SEXP prop_xptr, SEXP val) {
    return r_error_boundary([&]() -> SEXP {
        auto& rt = RRuntimeState::instance();
        rt.ensure_symbols();
        if (TYPEOF(prop_xptr) != EXTPTRSXP || R_ExternalPtrTag(prop_xptr) != rt.prop_tag) {
            throw type_error("invalid pyrabind property handle");
        }
        const auto* prop = static_cast<const property_record*>(R_ExternalPtrAddr(prop_xptr));
        if (prop == nullptr) {
            throw attribute_error("unknown property");
        }
        if (!prop->setter) {
            throw attribute_error("property '" + prop->name + "' is read-only");
        }
        prop->setter(obj, val);
        return R_NilValue;
    });
}

PYRABIND_R_EXPORT SEXP pyrabind_call_op(SEXP op_sexp, SEXP lhs, SEXP rhs, SEXP unary_sexp) {
    return r_error_boundary([&]() -> SEXP {
        std::string op_name;
        if (!r_to_string(op_sexp, false, op_name)) {
            throw type_error("operator name must be character(1)");
        }
        bool is_unary = false;
        r_to_bool(unary_sexp, true, is_unary);

        const char* rev_dunder = nullptr;
        const char* flipped_cmp = nullptr;
        const char* fwd_dunder = r_op_to_dunder(op_name, is_unary, rev_dunder, flipped_cmp);
        if (fwd_dunder == nullptr) {
            throw type_error("unsupported operator '" + op_name + "'");
        }

        const class_record* lhs_cls = find_class_of_sexp(lhs);
        if (lhs_cls != nullptr) {
            if (const function_record* fn = lhs_cls->find_method(fwd_dunder)) {
                call_args cargs;
                cargs.add_pos(lhs);
                if (!is_unary) {
                    cargs.add_pos(rhs);
                }
                auto finalize_op_result = [&](SEXP res) -> SEXP {
                    if (is_unary && op_name == "!") {
                        PROTECT(res);
                        bool truthy = false;
                        if (!r_to_bool(res, true, truthy)) {
                            UNPROTECT(1);
                            throw type_error("__bool__ must return a boolean");
                        }
                        UNPROTECT(1);
                        return r_from_bool(!truthy);
                    }
                    return res;
                };
                for (const auto& ov : fn->overloads) {
                    bool matched = false;
                    SEXP res = ov.invoke(cargs, false, matched, R_NilValue);
                    if (matched) {
                        return finalize_op_result(res);
                    }
                }
                for (const auto& ov : fn->overloads) {
                    bool matched = false;
                    SEXP res = ov.invoke(cargs, true, matched, R_NilValue);
                    if (matched) {
                        return finalize_op_result(res);
                    }
                }
            }
        }

        if (!is_unary) {
            const class_record* rhs_cls = find_class_of_sexp(rhs);
            const char* right_name = rev_dunder != nullptr ? rev_dunder : flipped_cmp;
            if (rhs_cls != nullptr && right_name != nullptr) {
                if (const function_record* fn = rhs_cls->find_method(right_name)) {
                    call_args cargs;
                    cargs.add_pos(rhs);
                    cargs.add_pos(lhs);
                    return dispatch_overload_list(fn->overloads, cargs, right_name, R_NilValue);
                }
            }
        }

        throw type_error("no matching C++ overload for operator '" + op_name + "'");
    });
}

PYRABIND_R_EXPORT SEXP pyrabind_call_special(SEXP obj, SEXP kind_sexp) {
    return r_error_boundary([&]() -> SEXP {
        std::string kind;
        if (!r_to_string(kind_sexp, false, kind)) {
            throw type_error("special op kind must be character(1)");
        }
        if (kind == "repr") {
            const RInstanceHolder* h = r_unwrap_instance(obj);
            if (const function_record* fn = h->cls->find_method("__repr__")) {
                call_args cargs;
                cargs.add_pos(obj);
                return dispatch_overload_list(fn->overloads, cargs, "__repr__", obj);
            }
            return r_from_string("<pyrabind " + h->cls->qualified_name + ">");
        }
        if (kind == "abs") {
            const RInstanceHolder* h = r_unwrap_instance(obj);
            if (const function_record* fn = h->cls->find_method("__abs__")) {
                call_args cargs;
                cargs.add_pos(obj);
                return dispatch_overload_list(fn->overloads, cargs, "__abs__", obj);
            }
            throw type_error("object does not implement __abs__");
        }
        throw value_error("unknown special op '" + kind + "'");
    });
}

PYRABIND_R_EXPORT SEXP pyrabind_call_enum_op(SEXP op_sexp, SEXP a_sexp, SEXP b_sexp) {
    return r_error_boundary([&]() -> SEXP {
        std::string op;
        if (!r_to_string(op_sexp, false, op)) {
            throw type_error("enum operation must be character(1)");
        }
        const REnumHolder* a = r_unwrap_enum(a_sexp);
        if (op == "==" || op == "!=") {
            const REnumHolder* b = r_unwrap_enum(b_sexp);
            bool eq = (a->rec->type == b->rec->type) && (a->value == b->value);
            return r_from_bool(op == "==" ? eq : !eq);
        }
        if (op == "value") {
            return r_from_i64(a->value);
        }
        if (op == "name") {
            for (const auto& entry : a->rec->entries) {
                if (entry.value == a->value) {
                    return r_from_string(entry.name);
                }
            }
            return r_from_string("");
        }
        if (op == "repr") {
            std::string ename;
            for (const auto& entry : a->rec->entries) {
                if (entry.value == a->value) {
                    ename = entry.name;
                    break;
                }
            }
            std::string text = ename.empty() ? a->rec->name + "(" + std::to_string(a->value) + ")"
                                             : a->rec->name + "." + ename;
            return r_from_string(text);
        }
        throw value_error("unknown enum op '" + op + "'");
    });
}

PYRABIND_R_EXPORT SEXP pyrabind_call_iter_next(SEXP iter_xptr) {
    return r_error_boundary([&]() -> SEXP {
        auto& rt = RRuntimeState::instance();
        rt.ensure_symbols();
        if (iter_xptr == nullptr || iter_xptr == R_NilValue || TYPEOF(iter_xptr) != EXTPTRSXP ||
            R_ExternalPtrTag(iter_xptr) != rt.iter_tag) {
            throw type_error("expected a pyrabind iterator");
        }
        auto* h = static_cast<RIterHolder*>(R_ExternalPtrAddr(iter_xptr));
        if (h == nullptr || !h->next_fn) {
            throw type_error("invalid pyrabind iterator");
        }
        bool done = false;
        SEXP val = h->next_fn(done, iter_xptr);
        if (done) {
            throw stop_iteration("iterator exhausted");
        }
        return val;
    });
}

PYRABIND_R_EXPORT SEXP pyrabind_call_iter_collect(SEXP iter_xptr) {
    return r_error_boundary([&]() -> SEXP {
        auto& rt = RRuntimeState::instance();
        rt.ensure_symbols();
        if (iter_xptr == nullptr || iter_xptr == R_NilValue || TYPEOF(iter_xptr) != EXTPTRSXP ||
            R_ExternalPtrTag(iter_xptr) != rt.iter_tag) {
            throw type_error("expected a pyrabind iterator");
        }
        auto* h = static_cast<RIterHolder*>(R_ExternalPtrAddr(iter_xptr));
        if (h == nullptr || !h->next_fn) {
            throw type_error("invalid pyrabind iterator");
        }
        std::vector<cpp11::sexp> items;
        while (true) {
            bool done = false;
            SEXP val = h->next_fn(done, iter_xptr);
            if (done) {
                break;
            }
            items.emplace_back(val);
        }
        const R_xlen_t n = static_cast<R_xlen_t>(items.size());
        SEXP out = PROTECT(cpp11::safe[Rf_allocVector](VECSXP, n));
        for (R_xlen_t i = 0; i < n; ++i) {
            SET_VECTOR_ELT(out, i, items[static_cast<std::size_t>(i)].data());
        }
        UNPROTECT(1);
        return out;
    });
}

} // extern "C"

} // namespace pyrabind::r_detail
