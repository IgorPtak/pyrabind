#pragma once

#include "pyrabind/r/caster.hpp"

#include <memory>
#include <type_traits>
#include <typeindex>
#include <utility>

namespace pyrabind::r_detail {

template <typename T> struct type_caster<std::shared_ptr<T>> {
    using Td = std::remove_const_t<T>;
    std::shared_ptr<T> val;

    bool from_r(SEXP x, bool) {
        if (x == R_NilValue) {
            val.reset();
            return true;
        }
        void* raw = nullptr;
        std::shared_ptr<void> owner;
        if (!r_extract_shared_holder(x, std::type_index(typeid(Td)), raw, owner)) {
            return false;
        }
        std::shared_ptr<Td> aliased(std::move(owner), static_cast<Td*>(raw));
        val = std::move(aliased);
        return true;
    }

    std::shared_ptr<T>& get() noexcept { return val; }

    static SEXP to_r(const std::shared_ptr<T>& sp, rv_policy, SEXP) {
        if (!sp) {
            return R_NilValue;
        }
        Td* raw_ptr = const_cast<Td*>(sp.get());
        const class_record* cls = nullptr;
        void* exposed_ptr = raw_ptr;
        if constexpr (std::is_polymorphic_v<Td>) {
            cls = type_registry::instance().find_class(std::type_index(typeid(*raw_ptr)));
            if (cls != nullptr) {
                exposed_ptr = dynamic_cast<void*>(raw_ptr);
            }
        }
        if (cls == nullptr) {
            cls = type_registry::instance().find_class(std::type_index(typeid(Td)));
            exposed_ptr = raw_ptr;
        }
        if (cls == nullptr) {
            throw type_error("cannot return std::shared_ptr to unregistered C++ type");
        }
        instance_box box;
        box.ptr = exposed_ptr;
        box.type = cls->type;
        box.holder = std::const_pointer_cast<void>(std::static_pointer_cast<const void>(sp));
        box.owned = true;
        return r_wrap_instance(std::move(box), cls, rv_policy::take_ownership, R_NilValue);
    }
};

} // namespace pyrabind::r_detail
