#pragma once

#include "pyrabind/r/caster.hpp"

#include <memory>
#include <type_traits>
#include <typeindex>
#include <utility>

namespace pyrabind::r_detail {

template <typename T> struct type_caster<std::unique_ptr<T>> {
    using Td = std::remove_const_t<T>;
    std::unique_ptr<T> val;

    bool from_r(SEXP x, bool) {
        if (x == R_NilValue) {
            val.reset();
            return true;
        }
        void* raw = nullptr;
        constexpr bool kPolyOrVd = std::is_polymorphic_v<Td> || std::has_virtual_destructor_v<Td>;
        if (!r_relinquish_unique_ptr(x, std::type_index(typeid(Td)), kPolyOrVd, raw)) {
            return false;
        }
        val.reset(static_cast<T*>(raw));
        return true;
    }

    std::unique_ptr<T>& get() noexcept { return val; }

    static SEXP to_r(std::unique_ptr<T> up, rv_policy, SEXP) {
        if (!up) {
            return R_NilValue;
        }
        Td* alloc_ptr = const_cast<Td*>(up.release());
        const class_record* cls = nullptr;
        void* exposed_ptr = alloc_ptr;
        if constexpr (std::is_polymorphic_v<Td>) {
            cls = type_registry::instance().find_class(std::type_index(typeid(*alloc_ptr)));
            if (cls != nullptr) {
                exposed_ptr = dynamic_cast<void*>(alloc_ptr);
            }
        }
        if (cls == nullptr) {
            cls = type_registry::instance().find_class(std::type_index(typeid(Td)));
            exposed_ptr = alloc_ptr;
        }
        if (cls == nullptr) {
            delete alloc_ptr;
            throw type_error("cannot return std::unique_ptr to unregistered C++ type");
        }
        instance_box box = make_owned_instance_box<Td>(alloc_ptr, exposed_ptr, cls->type);
        return r_wrap_instance(std::move(box), cls, rv_policy::take_ownership, R_NilValue);
    }
};

} // namespace pyrabind::r_detail
