#pragma once

#include "pyrabind/r/types.hpp"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <typeindex>
#include <unordered_map>
#include <utility>
#include <vector>

namespace pyrabind::r_detail {

struct call_args {
    static constexpr std::size_t kInlineCap = 16;

    SEXP inline_pos[kInlineCap]{};
    const char* inline_kw_names[kInlineCap]{};
    SEXP inline_kw_vals[kInlineCap]{};
    std::size_t n_pos = 0;
    std::size_t n_kw = 0;
    std::vector<SEXP> overflow_pos;
    std::vector<std::pair<const char*, SEXP>> overflow_kw;

    void add_pos(SEXP v) {
        if (n_pos < kInlineCap && overflow_pos.empty()) {
            inline_pos[n_pos++] = v;
        } else {
            if (overflow_pos.empty()) {
                overflow_pos.assign(inline_pos, inline_pos + n_pos);
            }
            overflow_pos.push_back(v);
            n_pos = overflow_pos.size();
        }
    }

    void add_kw(const char* name, SEXP v) {
        if (n_kw < kInlineCap && overflow_kw.empty()) {
            inline_kw_names[n_kw] = name;
            inline_kw_vals[n_kw] = v;
            ++n_kw;
        } else {
            if (overflow_kw.empty()) {
                for (std::size_t i = 0; i < n_kw; ++i) {
                    overflow_kw.emplace_back(inline_kw_names[i], inline_kw_vals[i]);
                }
            }
            overflow_kw.emplace_back(name, v);
            n_kw = overflow_kw.size();
        }
    }

    SEXP pos_at(std::size_t idx) const noexcept {
        return overflow_pos.empty() ? inline_pos[idx] : overflow_pos[idx];
    }

    const char* kw_name_at(std::size_t idx) const noexcept {
        return overflow_kw.empty() ? inline_kw_names[idx] : overflow_kw[idx].first;
    }

    SEXP kw_val_at(std::size_t idx) const noexcept {
        return overflow_kw.empty() ? inline_kw_vals[idx] : overflow_kw[idx].second;
    }
};

struct instance_box {
    void* ptr = nullptr;
    std::type_index type = std::type_index(typeid(void));
    std::shared_ptr<void> holder;
    bool owned = false;

    void* cast_to(std::type_index target) const noexcept;
};

struct arg_spec {
    std::string name;
    bool convert = true;
    bool accepts_none = false;
    bool has_default = false;
    std::function<SEXP()> default_fn;
};

struct keep_alive_spec {
    std::size_t nurse = 0;
    std::size_t patient = 0;
};

struct overload_record {
    using invoke_fn_t =
        std::function<SEXP(const call_args&, bool convert, bool& matched, SEXP parent)>;
    std::size_t arity = 0;
    std::vector<arg_spec> args;
    std::vector<keep_alive_spec> keep_alives;
    bool is_method = false;
    bool is_operator = false;
    std::string doc;
    invoke_fn_t invoke;
};

struct function_record {
    std::string name;
    std::vector<overload_record> overloads;
    std::string doc;
};

struct property_record {
    std::string name;
    std::function<SEXP(SEXP)> getter;
    std::function<void(SEXP, SEXP)> setter;
    std::string doc;
};

struct class_record {
    std::string name;
    std::string qualified_name;
    std::string doc;
    std::type_index type = std::type_index(typeid(void));
    std::type_index base_type = std::type_index(typeid(void));
    const class_record* base = nullptr;
    void* (*upcast)(void*) noexcept = nullptr;

    std::vector<overload_record> constructors;
    std::deque<function_record> methods;
    std::deque<function_record> static_methods;
    std::deque<property_record> properties;

    function_record& get_or_create_method(const std::string& mname, bool is_static = false) {
        auto& pool = is_static ? static_methods : methods;
        for (auto& fn : pool) {
            if (fn.name == mname) {
                return fn;
            }
        }
        pool.emplace_back();
        pool.back().name = mname;
        return pool.back();
    }

    const function_record* find_method(std::string_view mname,
                                       bool walk_bases = true) const noexcept {
        for (const auto& fn : methods) {
            if (fn.name == mname) {
                return &fn;
            }
        }
        if (walk_bases && base != nullptr) {
            return base->find_method(mname, true);
        }
        return nullptr;
    }
};

struct enum_entry {
    std::string name;
    std::int64_t value = 0;
    std::string doc;
};

struct enum_record {
    std::string name;
    std::string qualified_name;
    std::string doc;
    std::type_index type = std::type_index(typeid(void));
    std::vector<enum_entry> entries;
    bool exported = false;
};

struct module_record {
    std::string name;
    std::string qualified_name;
    std::string doc;
    std::deque<class_record> classes;
    std::deque<enum_record> enums;
    std::deque<function_record> functions;
    std::vector<std::pair<std::string, std::function<SEXP()>>> constants;
    std::vector<std::unique_ptr<module_record>> submodules;

    function_record& get_or_create_function(const std::string& fname) {
        for (auto& fn : functions) {
            if (fn.name == fname) {
                return fn;
            }
        }
        functions.emplace_back();
        functions.back().name = fname;
        return functions.back();
    }
};

class type_registry {
public:
    static type_registry& instance();

    void register_class(class_record* cls) { classes_by_type_.emplace(cls->type, cls); }

    void register_enum(enum_record* en) { enums_by_type_.emplace(en->type, en); }

    const class_record* find_class(std::type_index t) const noexcept {
        auto it = classes_by_type_.find(t);
        return it == classes_by_type_.end() ? nullptr : it->second;
    }

    const enum_record* find_enum(std::type_index t) const noexcept {
        auto it = enums_by_type_.find(t);
        return it == enums_by_type_.end() ? nullptr : it->second;
    }

    void resolve_bases() noexcept {
        for (auto& [_, cls] : classes_by_type_) {
            if (cls->base == nullptr && cls->base_type != std::type_index(typeid(void))) {
                auto it = classes_by_type_.find(cls->base_type);
                if (it != classes_by_type_.end()) {
                    cls->base = it->second;
                }
            }
        }
    }

    void* upcast_ptr(void* ptr, std::type_index from, std::type_index to) const noexcept {
        if (ptr == nullptr || from == to) {
            return ptr;
        }
        const class_record* cur = find_class(from);
        void* p = ptr;
        while (cur != nullptr && cur->base != nullptr && cur->upcast != nullptr) {
            p = cur->upcast(p);
            cur = cur->base;
            if (cur->type == to) {
                return p;
            }
        }
        return nullptr;
    }

private:
    std::unordered_map<std::type_index, class_record*> classes_by_type_;
    std::unordered_map<std::type_index, enum_record*> enums_by_type_;
};

inline void* instance_box::cast_to(std::type_index target) const noexcept {
    if (ptr == nullptr) {
        return nullptr;
    }
    if (type == target) {
        return ptr;
    }
    return type_registry::instance().upcast_ptr(ptr, type, target);
}

} // namespace pyrabind::r_detail
