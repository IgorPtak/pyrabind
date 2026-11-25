#pragma once

#include "pyrabind/arrow_abi.hpp"
#include "pyrabind/dataframe_types.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <deque>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#if defined(PYRABIND_TARGET_PYTHON)
#include <nanobind/nanobind.h>
#elif defined(PYRABIND_TARGET_R)
#include "pyrabind/r/caster.hpp"
#endif

namespace pyrabind {

namespace arrow_detail {

struct ColumnBuffers {
    column_data col_storage;
    std::vector<std::uint8_t> validity_bitmap;
    std::vector<std::uint8_t> bool_bitmap;
    std::vector<std::int32_t> utf8_offsets;
    std::string utf8_data;
    const void* buffer_ptrs[3]{};
};

struct BatchOwner {
    std::vector<std::string> names;
    std::vector<ColumnBuffers> cols;
    std::vector<ArrowSchema> child_schemas;
    std::vector<ArrowSchema*> child_schema_ptrs;
    std::vector<ArrowArray> child_arrays;
    std::vector<ArrowArray*> child_array_ptrs;
    const void* struct_buffers[1]{nullptr};
};

struct ComposedBatchOwner {
    std::shared_ptr<void> upstream_owner;
    std::string new_col_name;
    ColumnBuffers new_col;
    ArrowSchema new_schema{};
    ArrowArray new_array{};
    std::deque<ArrowSchema> borrowed_schemas;
    std::deque<ArrowArray> borrowed_arrays;
    std::deque<std::vector<ArrowSchema*>> nested_schema_ptrs;
    std::deque<std::vector<ArrowArray*>> nested_array_ptrs;
    std::vector<ArrowSchema*> root_schema_ptrs;
    std::vector<ArrowArray*> root_array_ptrs;
    const void* struct_buffers[1]{nullptr};
};

struct ChildSchemaPriv {
    std::shared_ptr<void> owner;
};

struct ChildArrayPriv {
    std::shared_ptr<void> owner;
};

struct RootSchemaPriv {
    std::shared_ptr<void> owner;
};

struct RootArrayPriv {
    std::shared_ptr<void> owner;
};

inline void release_child_schema(ArrowSchema* sch) {
    if (sch == nullptr || sch->release == nullptr) {
        return;
    }
    for (std::int64_t i = 0; i < sch->n_children; ++i) {
        if (sch->children != nullptr && sch->children[i] != nullptr &&
            sch->children[i]->release != nullptr) {
            sch->children[i]->release(sch->children[i]);
        }
    }
    if (sch->dictionary != nullptr && sch->dictionary->release != nullptr) {
        sch->dictionary->release(sch->dictionary);
    }
    delete static_cast<ChildSchemaPriv*>(sch->private_data);
    sch->private_data = nullptr;
    sch->release = nullptr;
}

inline void release_child_array(ArrowArray* arr) {
    if (arr == nullptr || arr->release == nullptr) {
        return;
    }
    for (std::int64_t i = 0; i < arr->n_children; ++i) {
        if (arr->children != nullptr && arr->children[i] != nullptr &&
            arr->children[i]->release != nullptr) {
            arr->children[i]->release(arr->children[i]);
        }
    }
    if (arr->dictionary != nullptr && arr->dictionary->release != nullptr) {
        arr->dictionary->release(arr->dictionary);
    }
    delete static_cast<ChildArrayPriv*>(arr->private_data);
    arr->private_data = nullptr;
    arr->release = nullptr;
}

inline void release_root_schema(ArrowSchema* sch) {
    if (sch == nullptr || sch->release == nullptr) {
        return;
    }
    for (std::int64_t i = 0; i < sch->n_children; ++i) {
        if (sch->children != nullptr && sch->children[i] != nullptr &&
            sch->children[i]->release != nullptr) {
            sch->children[i]->release(sch->children[i]);
        }
    }
    delete static_cast<RootSchemaPriv*>(sch->private_data);
    sch->private_data = nullptr;
    sch->release = nullptr;
}

inline void release_root_array(ArrowArray* arr) {
    if (arr == nullptr || arr->release == nullptr) {
        return;
    }
    for (std::int64_t i = 0; i < arr->n_children; ++i) {
        if (arr->children != nullptr && arr->children[i] != nullptr &&
            arr->children[i]->release != nullptr) {
            arr->children[i]->release(arr->children[i]);
        }
    }
    delete static_cast<RootArrayPriv*>(arr->private_data);
    arr->private_data = nullptr;
    arr->release = nullptr;
}

inline bool bitmap_get(const void* buf, std::int64_t offset, std::int64_t idx) noexcept {
    if (buf == nullptr) {
        return true;
    }
    const auto* bytes = static_cast<const std::uint8_t*>(buf);
    const std::int64_t bit_idx = offset + idx;
    return ((bytes[bit_idx >> 3] >> (bit_idx & 7)) & 1U) != 0;
}

inline void build_validity_bitmap(const std::vector<std::uint8_t>& mask, std::size_t n,
                                  std::vector<std::uint8_t>& out_bitmap,
                                  std::int64_t& out_null_count) {
    out_bitmap.clear();
    out_null_count = 0;
    if (mask.empty() || n == 0) {
        return;
    }
    bool any_null = false;
    for (std::size_t i = 0; i < n; ++i) {
        if (mask[i] == 0) {
            any_null = true;
            ++out_null_count;
        }
    }
    if (!any_null) {
        return;
    }
    out_bitmap.assign((n + 7) / 8, 0);
    for (std::size_t i = 0; i < n; ++i) {
        if (mask[i] != 0) {
            out_bitmap[i >> 3] |= static_cast<std::uint8_t>(1U << (i & 7));
        }
    }
}

inline void populate_column_arrow(const char* name_cstr, ColumnBuffers& cb, ArrowSchema& cs,
                                  ArrowArray& ca, column_data col,
                                  std::vector<std::uint8_t> val_mask, std::size_t nrows,
                                  const std::shared_ptr<void>& owner) {
    cb.col_storage = std::move(col);

    std::int64_t null_count = 0;
    build_validity_bitmap(val_mask, nrows, cb.validity_bitmap, null_count);

    cs = ArrowSchema{};
    ca = ArrowArray{};
    cs.name = name_cstr;
    cs.flags = null_count > 0 ? ARROW_FLAG_NULLABLE : 0;
    cs.release = &release_child_schema;
    cs.private_data = new ChildSchemaPriv{owner};

    ca.length = static_cast<std::int64_t>(nrows);
    ca.null_count = null_count;
    ca.offset = 0;
    ca.release = &release_child_array;
    ca.private_data = new ChildArrayPriv{owner};

    cb.buffer_ptrs[0] = cb.validity_bitmap.empty() ? nullptr : cb.validity_bitmap.data();

    std::visit(
        [&](const auto& vec) {
            using VecT = std::decay_t<decltype(vec)>;
            if constexpr (std::is_same_v<VecT, std::vector<double>>) {
                cs.format = "g";
                ca.n_buffers = 2;
                cb.buffer_ptrs[1] = vec.data();
            } else if constexpr (std::is_same_v<VecT, std::vector<std::int64_t>>) {
                cs.format = "l";
                ca.n_buffers = 2;
                cb.buffer_ptrs[1] = vec.data();
            } else if constexpr (std::is_same_v<VecT, std::vector<std::int32_t>>) {
                cs.format = "i";
                ca.n_buffers = 2;
                cb.buffer_ptrs[1] = vec.data();
            } else if constexpr (std::is_same_v<VecT, std::vector<bool>>) {
                cs.format = "b";
                ca.n_buffers = 2;
                cb.bool_bitmap.assign((nrows + 7) / 8, 0);
                for (std::size_t r = 0; r < nrows; ++r) {
                    if (vec[r]) {
                        cb.bool_bitmap[r >> 3] |= static_cast<std::uint8_t>(1U << (r & 7));
                    }
                }
                cb.buffer_ptrs[1] = cb.bool_bitmap.data();
            } else {
                cs.format = "u";
                ca.n_buffers = 3;
                std::size_t total_bytes = 0;
                for (std::size_t r = 0; r < nrows; ++r) {
                    total_bytes += vec[r].size();
                }
                if (total_bytes >
                    static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max())) {
                    throw std::overflow_error("export_to_arrow: utf8 column exceeds int32 size");
                }
                cb.utf8_offsets.resize(nrows + 1);
                cb.utf8_data.reserve(total_bytes);
                std::int32_t cur_off = 0;
                for (std::size_t r = 0; r < nrows; ++r) {
                    cb.utf8_offsets[r] = cur_off;
                    cb.utf8_data.append(vec[r]);
                    cur_off += static_cast<std::int32_t>(vec[r].size());
                }
                cb.utf8_offsets[nrows] = cur_off;
                cb.buffer_ptrs[1] = cb.utf8_offsets.data();
                cb.buffer_ptrs[2] = cb.utf8_data.data();
            }
        },
        cb.col_storage);

    ca.buffers = cb.buffer_ptrs;
}

inline ArrowSchema* clone_borrowed_schema(const ArrowSchema* src, ComposedBatchOwner* owner,
                                          const std::shared_ptr<void>& owner_sp) {
    owner->borrowed_schemas.emplace_back();
    ArrowSchema* dst = &owner->borrowed_schemas.back();
    *dst = *src;
    dst->children = nullptr;
    dst->dictionary = nullptr;
    dst->release = &release_child_schema;
    dst->private_data = new ChildSchemaPriv{owner_sp};

    if (src->n_children > 0 && src->children != nullptr) {
        const auto n = static_cast<std::size_t>(src->n_children);
        owner->nested_schema_ptrs.emplace_back(n, nullptr);
        auto& ptrs = owner->nested_schema_ptrs.back();
        for (std::size_t i = 0; i < n; ++i) {
            ptrs[i] = clone_borrowed_schema(src->children[i], owner, owner_sp);
        }
        dst->children = ptrs.data();
    }
    if (src->dictionary != nullptr) {
        dst->dictionary = clone_borrowed_schema(src->dictionary, owner, owner_sp);
    }
    return dst;
}

inline ArrowArray* clone_borrowed_array(const ArrowArray* src, std::int64_t override_len,
                                        std::int64_t add_offset, ComposedBatchOwner* owner,
                                        const std::shared_ptr<void>& owner_sp) {
    owner->borrowed_arrays.emplace_back();
    ArrowArray* dst = &owner->borrowed_arrays.back();
    *dst = *src;
    dst->children = nullptr;
    dst->dictionary = nullptr;
    if (override_len >= 0) {
        dst->length = override_len;
        if (override_len != src->length || add_offset != 0) {
            dst->null_count = -1;
        }
    }
    dst->offset = src->offset + add_offset;
    dst->release = &release_child_array;
    dst->private_data = new ChildArrayPriv{owner_sp};

    if (src->n_children > 0 && src->children != nullptr) {
        const auto n = static_cast<std::size_t>(src->n_children);
        owner->nested_array_ptrs.emplace_back(n, nullptr);
        auto& ptrs = owner->nested_array_ptrs.back();
        for (std::size_t i = 0; i < n; ++i) {
            ptrs[i] = clone_borrowed_array(src->children[i], -1, 0, owner, owner_sp);
        }
        dst->children = ptrs.data();
    }
    if (src->dictionary != nullptr) {
        dst->dictionary = clone_borrowed_array(src->dictionary, -1, 0, owner, owner_sp);
    }
    return dst;
}

} // namespace arrow_detail

template <typename T> struct arrow_span {
    const T* ptr = nullptr;
    std::size_t len = 0;
    const std::uint8_t* validity = nullptr;
    std::int64_t bit_offset = 0;
    std::int64_t null_cnt = 0;

    const T* data() const noexcept { return ptr; }
    std::size_t size() const noexcept { return len; }
    bool empty() const noexcept { return len == 0; }
    const T& operator[](std::size_t idx) const noexcept { return ptr[idx]; }

    bool has_nulls() const noexcept { return validity != nullptr && null_cnt != 0; }

    bool is_null(std::size_t idx) const noexcept {
        if (validity == nullptr || null_cnt == 0) {
            return false;
        }
        return !arrow_detail::bitmap_get(validity, bit_offset, static_cast<std::int64_t>(idx));
    }
};

struct arrow_string_view {
    const std::int32_t* offsets32 = nullptr;
    const std::int64_t* offsets64 = nullptr;
    const char* bytes = nullptr;
    std::size_t len = 0;
    const std::uint8_t* validity = nullptr;
    std::int64_t bit_offset = 0;
    std::int64_t null_cnt = 0;
    bool large_offsets = false;

    std::size_t size() const noexcept { return len; }
    bool empty() const noexcept { return len == 0; }
    bool has_nulls() const noexcept { return validity != nullptr && null_cnt != 0; }

    bool is_null(std::size_t idx) const noexcept {
        if (validity == nullptr || null_cnt == 0) {
            return false;
        }
        return !arrow_detail::bitmap_get(validity, bit_offset, static_cast<std::int64_t>(idx));
    }

    std::string_view operator[](std::size_t idx) const noexcept {
        if (large_offsets) {
            const std::int64_t start = offsets64[idx];
            const std::int64_t end = offsets64[idx + 1];
            return std::string_view(bytes + start, static_cast<std::size_t>(end - start));
        }
        const std::int32_t start = offsets32[idx];
        const std::int32_t end = offsets32[idx + 1];
        return std::string_view(bytes + start, static_cast<std::size_t>(end - start));
    }
};

struct arrow_batch_view;

struct arrow_batch {
    ArrowSchema schema{};
    ArrowArray array{};

    arrow_batch() = default;
    ~arrow_batch() { release(); }

    arrow_batch(const arrow_batch&) = delete;
    arrow_batch& operator=(const arrow_batch&) = delete;

    arrow_batch(arrow_batch&& other) noexcept : schema(other.schema), array(other.array) {
        other.schema = ArrowSchema{};
        other.array = ArrowArray{};
    }

    arrow_batch& operator=(arrow_batch&& other) noexcept {
        if (this != &other) {
            release();
            schema = other.schema;
            array = other.array;
            other.schema = ArrowSchema{};
            other.array = ArrowArray{};
        }
        return *this;
    }

    bool is_valid() const noexcept { return schema.release != nullptr && array.release != nullptr; }

    void release() noexcept {
        if (array.release != nullptr) {
            array.release(&array);
        }
        array = ArrowArray{};
        if (schema.release != nullptr) {
            schema.release(&schema);
        }
        schema = ArrowSchema{};
    }

    arrow_batch_view view() const noexcept;

    arrow_batch insert_column(std::size_t pos, std::string name, column_data col,
                              std::vector<std::uint8_t> val_mask = {}) &&;

    arrow_batch with_column(std::string name, column_data col,
                            std::vector<std::uint8_t> val_mask = {}) &&;
};

struct arrow_batch_view {
    const ArrowSchema* schema = nullptr;
    const ArrowArray* array = nullptr;
    std::shared_ptr<void> owner;

    bool is_valid() const noexcept {
        return schema != nullptr && array != nullptr && schema->release != nullptr &&
               array->release != nullptr && schema->format != nullptr &&
               std::strcmp(schema->format, "+s") == 0 && schema->n_children == array->n_children;
    }

    std::size_t ncols() const noexcept {
        return is_valid() ? static_cast<std::size_t>(schema->n_children) : 0;
    }

    std::size_t nrows() const noexcept {
        return is_valid() ? static_cast<std::size_t>(array->length) : 0;
    }

    std::string_view column_name(std::size_t idx) const {
        if (!is_valid() || idx >= ncols()) {
            throw std::out_of_range("arrow_batch_view: column index out of range");
        }
        const ArrowSchema* cs = schema->children[idx];
        return cs != nullptr && cs->name != nullptr ? std::string_view(cs->name)
                                                    : std::string_view{};
    }

    std::string_view column_format(std::size_t idx) const {
        if (!is_valid() || idx >= ncols()) {
            throw std::out_of_range("arrow_batch_view: column index out of range");
        }
        const ArrowSchema* cs = schema->children[idx];
        return cs != nullptr && cs->format != nullptr ? std::string_view(cs->format)
                                                      : std::string_view{};
    }

    std::string_view column_format(std::string_view name) const {
        return column_format(index_of(name));
    }

    bool contains(std::string_view name) const noexcept {
        if (!is_valid()) {
            return false;
        }
        for (std::size_t i = 0; i < ncols(); ++i) {
            const ArrowSchema* cs = schema->children[i];
            if (cs != nullptr && cs->name != nullptr && name == cs->name) {
                return true;
            }
        }
        return false;
    }

    std::size_t index_of(std::string_view name) const {
        if (!is_valid()) {
            throw std::invalid_argument("arrow_batch_view: invalid struct batch");
        }
        const std::size_t n = ncols();
        for (std::size_t i = 0; i < n; ++i) {
            const ArrowSchema* cs = schema->children[i];
            if (cs != nullptr && cs->name != nullptr && name == cs->name) {
                return i;
            }
        }
        throw std::out_of_range("arrow_batch_view has no column '" + std::string(name) + "'");
    }

    template <typename T> arrow_span<T> column_span(std::size_t idx) const {
        if (!is_valid() || idx >= ncols()) {
            throw std::out_of_range("arrow_batch_view: column index out of range");
        }
        const ArrowSchema* cs = schema->children[idx];
        const ArrowArray* ca = array->children[idx];
        if (cs == nullptr || ca == nullptr || cs->format == nullptr) {
            throw std::invalid_argument("arrow_batch_view: null child schema or array");
        }

        const char* expected_fmt = nullptr;
        if constexpr (std::is_same_v<T, double>) {
            expected_fmt = "g";
        } else if constexpr (std::is_same_v<T, float>) {
            expected_fmt = "f";
        } else if constexpr (std::is_same_v<T, std::int64_t>) {
            expected_fmt = "l";
        } else if constexpr (std::is_same_v<T, std::int32_t>) {
            expected_fmt = "i";
        } else if constexpr (std::is_same_v<T, std::uint8_t>) {
            expected_fmt = "C";
        } else {
            static_assert(!sizeof(T), "unsupported arrow_span scalar type");
        }

        if (std::strcmp(cs->format, expected_fmt) != 0) {
            throw std::invalid_argument(
                "arrow_batch_view: column '" + std::string(cs->name != nullptr ? cs->name : "") +
                "' format '" + cs->format + "' does not match expected '" + expected_fmt + "'");
        }

        const std::size_t n = nrows();
        if (n > 0 && (ca->n_buffers < 2 || ca->buffers == nullptr || ca->buffers[1] == nullptr)) {
            throw std::invalid_argument("arrow_batch_view: missing data buffer");
        }
        const std::int64_t total_off = array->offset + ca->offset;
        const std::uint8_t* val_ptr = (ca->null_count != 0 && ca->n_buffers > 0 &&
                                       ca->buffers != nullptr && ca->buffers[0] != nullptr)
                                          ? static_cast<const std::uint8_t*>(ca->buffers[0])
                                          : nullptr;
        const T* data_ptr = n > 0 ? static_cast<const T*>(ca->buffers[1]) + total_off : nullptr;
        return arrow_span<T>{data_ptr, n, val_ptr, total_off, ca->null_count};
    }

    template <typename T> arrow_span<T> column_span(std::string_view name) const {
        return column_span<T>(index_of(name));
    }

    arrow_string_view string_column_view(std::size_t idx) const {
        if (!is_valid() || idx >= ncols()) {
            throw std::out_of_range("arrow_batch_view: column index out of range");
        }
        const ArrowSchema* cs = schema->children[idx];
        const ArrowArray* ca = array->children[idx];
        if (cs == nullptr || ca == nullptr || cs->format == nullptr) {
            throw std::invalid_argument("arrow_batch_view: null child schema or array");
        }
        const bool is_u32 = std::strcmp(cs->format, "u") == 0;
        const bool is_u64 = std::strcmp(cs->format, "U") == 0;
        if (!is_u32 && !is_u64) {
            throw std::invalid_argument("arrow_batch_view: column '" +
                                        std::string(cs->name != nullptr ? cs->name : "") +
                                        "' is not UTF-8 string ('u' or 'U')");
        }
        const std::size_t n = nrows();
        if (n > 0 && (ca->n_buffers < 3 || ca->buffers == nullptr || ca->buffers[1] == nullptr)) {
            throw std::invalid_argument("arrow_batch_view: missing UTF-8 buffers");
        }
        const std::int64_t total_off = array->offset + ca->offset;
        const std::uint8_t* val_ptr = (ca->null_count != 0 && ca->n_buffers > 0 &&
                                       ca->buffers != nullptr && ca->buffers[0] != nullptr)
                                          ? static_cast<const std::uint8_t*>(ca->buffers[0])
                                          : nullptr;
        arrow_string_view out;
        out.len = n;
        out.validity = val_ptr;
        out.bit_offset = total_off;
        out.null_cnt = ca->null_count;
        out.large_offsets = is_u64;
        if (n > 0) {
            if (is_u64) {
                out.offsets64 = static_cast<const std::int64_t*>(ca->buffers[1]) + total_off;
            } else {
                out.offsets32 = static_cast<const std::int32_t*>(ca->buffers[1]) + total_off;
            }
            out.bytes = static_cast<const char*>(ca->buffers[2]);
        }
        return out;
    }

    arrow_string_view string_column_view(std::string_view name) const {
        return string_column_view(index_of(name));
    }

    arrow_batch insert_column(std::size_t pos, std::string name, column_data col,
                              std::vector<std::uint8_t> val_mask = {}) const {
        if (!is_valid()) {
            throw std::invalid_argument("insert_column: invalid Arrow struct batch");
        }
        const std::size_t cur_ncols = ncols();
        if (pos > cur_ncols) {
            throw std::out_of_range("insert_column: position out of range");
        }
        if (contains(name)) {
            throw std::invalid_argument("insert_column: duplicate column '" + name + "'");
        }
        const std::size_t n = nrows();
        if (column_size(col) != n) {
            throw std::invalid_argument("insert_column: column length does not match batch nrows");
        }
        if (!val_mask.empty() && val_mask.size() != n) {
            throw std::invalid_argument(
                "insert_column: validity mask length does not match batch nrows");
        }

        auto composed = std::make_shared<arrow_detail::ComposedBatchOwner>();
        composed->upstream_owner = owner;
        composed->new_col_name = std::move(name);
        arrow_detail::populate_column_arrow(composed->new_col_name.c_str(), composed->new_col,
                                            composed->new_schema, composed->new_array,
                                            std::move(col), std::move(val_mask), n, composed);

        const std::size_t out_ncols = cur_ncols + 1;
        composed->root_schema_ptrs.reserve(out_ncols);
        composed->root_array_ptrs.reserve(out_ncols);

        const std::int64_t root_off = array->offset;
        const std::int64_t len_i64 = static_cast<std::int64_t>(n);

        for (std::size_t i = 0; i <= cur_ncols; ++i) {
            if (i == pos) {
                composed->root_schema_ptrs.push_back(&composed->new_schema);
                composed->root_array_ptrs.push_back(&composed->new_array);
            }
            if (i < cur_ncols) {
                composed->root_schema_ptrs.push_back(arrow_detail::clone_borrowed_schema(
                    schema->children[i], composed.get(), composed));
                composed->root_array_ptrs.push_back(arrow_detail::clone_borrowed_array(
                    array->children[i], len_i64, root_off, composed.get(), composed));
            }
        }

        arrow_batch batch;
        batch.schema.format = "+s";
        batch.schema.name = "";
        batch.schema.flags = 0;
        batch.schema.n_children = static_cast<std::int64_t>(out_ncols);
        batch.schema.children = out_ncols > 0 ? composed->root_schema_ptrs.data() : nullptr;
        batch.schema.release = &arrow_detail::release_root_schema;
        batch.schema.private_data = new arrow_detail::RootSchemaPriv{composed};

        batch.array.length = len_i64;
        batch.array.null_count = 0;
        batch.array.offset = 0;
        batch.array.n_buffers = 1;
        batch.array.buffers = composed->struct_buffers;
        batch.array.n_children = static_cast<std::int64_t>(out_ncols);
        batch.array.children = out_ncols > 0 ? composed->root_array_ptrs.data() : nullptr;
        batch.array.release = &arrow_detail::release_root_array;
        batch.array.private_data = new arrow_detail::RootArrayPriv{composed};

        return batch;
    }

    arrow_batch with_column(std::string name, column_data col,
                            std::vector<std::uint8_t> val_mask = {}) const {
        if (!is_valid()) {
            throw std::invalid_argument("with_column: invalid Arrow struct batch");
        }
        const std::size_t n = nrows();
        if (column_size(col) != n) {
            throw std::invalid_argument("with_column: column length does not match batch nrows");
        }
        if (!val_mask.empty() && val_mask.size() != n) {
            throw std::invalid_argument(
                "with_column: validity mask length does not match batch nrows");
        }

        const std::size_t cur_ncols = ncols();
        std::size_t replace_idx = cur_ncols;
        for (std::size_t i = 0; i < cur_ncols; ++i) {
            const ArrowSchema* cs = schema->children[i];
            if (cs != nullptr && cs->name != nullptr && name == cs->name) {
                replace_idx = i;
                break;
            }
        }
        if (replace_idx == cur_ncols) {
            return insert_column(cur_ncols, std::move(name), std::move(col), std::move(val_mask));
        }

        auto composed = std::make_shared<arrow_detail::ComposedBatchOwner>();
        composed->upstream_owner = owner;
        composed->new_col_name = std::move(name);
        arrow_detail::populate_column_arrow(composed->new_col_name.c_str(), composed->new_col,
                                            composed->new_schema, composed->new_array,
                                            std::move(col), std::move(val_mask), n, composed);

        composed->root_schema_ptrs.reserve(cur_ncols);
        composed->root_array_ptrs.reserve(cur_ncols);

        const std::int64_t root_off = array->offset;
        const std::int64_t len_i64 = static_cast<std::int64_t>(n);

        for (std::size_t i = 0; i < cur_ncols; ++i) {
            if (i == replace_idx) {
                composed->root_schema_ptrs.push_back(&composed->new_schema);
                composed->root_array_ptrs.push_back(&composed->new_array);
            } else {
                composed->root_schema_ptrs.push_back(arrow_detail::clone_borrowed_schema(
                    schema->children[i], composed.get(), composed));
                composed->root_array_ptrs.push_back(arrow_detail::clone_borrowed_array(
                    array->children[i], len_i64, root_off, composed.get(), composed));
            }
        }

        arrow_batch batch;
        batch.schema.format = "+s";
        batch.schema.name = "";
        batch.schema.flags = 0;
        batch.schema.n_children = static_cast<std::int64_t>(cur_ncols);
        batch.schema.children = cur_ncols > 0 ? composed->root_schema_ptrs.data() : nullptr;
        batch.schema.release = &arrow_detail::release_root_schema;
        batch.schema.private_data = new arrow_detail::RootSchemaPriv{composed};

        batch.array.length = len_i64;
        batch.array.null_count = 0;
        batch.array.offset = 0;
        batch.array.n_buffers = 1;
        batch.array.buffers = composed->struct_buffers;
        batch.array.n_children = static_cast<std::int64_t>(cur_ncols);
        batch.array.children = cur_ncols > 0 ? composed->root_array_ptrs.data() : nullptr;
        batch.array.release = &arrow_detail::release_root_array;
        batch.array.private_data = new arrow_detail::RootArrayPriv{composed};

        return batch;
    }
};

inline arrow_batch_view arrow_batch::view() const noexcept {
    return arrow_batch_view{&schema, &array, nullptr};
}

inline arrow_batch arrow_batch::insert_column(std::size_t pos, std::string name, column_data col,
                                              std::vector<std::uint8_t> val_mask) && {
    auto shared = std::make_shared<arrow_batch>(std::move(*this));
    arrow_batch_view v{&shared->schema, &shared->array, shared};
    return v.insert_column(pos, std::move(name), std::move(col), std::move(val_mask));
}

inline arrow_batch arrow_batch::with_column(std::string name, column_data col,
                                            std::vector<std::uint8_t> val_mask) && {
    auto shared = std::make_shared<arrow_batch>(std::move(*this));
    arrow_batch_view v{&shared->schema, &shared->array, shared};
    return v.with_column(std::move(name), std::move(col), std::move(val_mask));
}

inline arrow_batch export_to_arrow(data_frame df) {
    if (df.names.size() != df.columns.size()) {
        throw std::invalid_argument("export_to_arrow: data_frame names and columns size mismatch");
    }

    const std::size_t ncols = df.ncols();
    const std::size_t nrows = df.nrows();

    auto owner = std::make_shared<arrow_detail::BatchOwner>();
    owner->names = std::move(df.names);
    owner->cols.resize(ncols);
    owner->child_schemas.resize(ncols);
    owner->child_schema_ptrs.resize(ncols);
    owner->child_arrays.resize(ncols);
    owner->child_array_ptrs.resize(ncols);

    for (std::size_t c = 0; c < ncols; ++c) {
        std::vector<std::uint8_t> val_mask;
        if (c < df.validity.size()) {
            val_mask = std::move(df.validity[c]);
        }
        arrow_detail::populate_column_arrow(
            owner->names[c].c_str(), owner->cols[c], owner->child_schemas[c],
            owner->child_arrays[c], std::move(df.columns[c]), std::move(val_mask), nrows, owner);
        owner->child_schema_ptrs[c] = &owner->child_schemas[c];
        owner->child_array_ptrs[c] = &owner->child_arrays[c];
    }

    arrow_batch batch;
    batch.schema.format = "+s";
    batch.schema.name = "";
    batch.schema.flags = 0;
    batch.schema.n_children = static_cast<std::int64_t>(ncols);
    batch.schema.children = ncols > 0 ? owner->child_schema_ptrs.data() : nullptr;
    batch.schema.release = &arrow_detail::release_root_schema;
    batch.schema.private_data = new arrow_detail::RootSchemaPriv{owner};

    batch.array.length = static_cast<std::int64_t>(nrows);
    batch.array.null_count = 0;
    batch.array.offset = 0;
    batch.array.n_buffers = 1;
    batch.array.buffers = owner->struct_buffers;
    batch.array.n_children = static_cast<std::int64_t>(ncols);
    batch.array.children = ncols > 0 ? owner->child_array_ptrs.data() : nullptr;
    batch.array.release = &arrow_detail::release_root_array;
    batch.array.private_data = new arrow_detail::RootArrayPriv{owner};

    return batch;
}

inline data_frame import_from_arrow(const arrow_batch& batch) {
    if (!batch.is_valid() || batch.schema.format == nullptr ||
        std::strcmp(batch.schema.format, "+s") != 0) {
        throw std::invalid_argument("import_from_arrow: expected valid Arrow struct batch ('+s')");
    }
    if (batch.schema.n_children != batch.array.n_children) {
        throw std::invalid_argument("import_from_arrow: schema and array child count mismatch");
    }

    const std::int64_t ncols = batch.schema.n_children;
    const std::int64_t nrows = batch.array.length;
    const std::int64_t root_off = batch.array.offset;

    data_frame out;
    for (std::int64_t c = 0; c < ncols; ++c) {
        const ArrowSchema* cs = batch.schema.children[c];
        const ArrowArray* ca = batch.array.children[c];
        if (cs == nullptr || ca == nullptr || cs->format == nullptr) {
            throw std::invalid_argument("import_from_arrow: null child schema or array");
        }
        std::string col_name = cs->name != nullptr ? cs->name : ("col" + std::to_string(c));
        const std::int64_t off = root_off + ca->offset;

        std::vector<std::uint8_t> mask;
        if (ca->null_count != 0 && ca->n_buffers > 0 && ca->buffers[0] != nullptr) {
            mask.resize(static_cast<std::size_t>(nrows));
            for (std::int64_t r = 0; r < nrows; ++r) {
                mask[static_cast<std::size_t>(r)] =
                    arrow_detail::bitmap_get(ca->buffers[0], off, r) ? 1 : 0;
            }
        }

        const char fmt0 = cs->format[0];
        const char fmt1 = fmt0 != '\0' ? cs->format[1] : '\0';
        column_data col;

        if (fmt0 == 'g' && fmt1 == '\0') {
            std::vector<double> vec(static_cast<std::size_t>(nrows));
            if (nrows > 0) {
                const auto* src = static_cast<const double*>(ca->buffers[1]) + off;
                std::memcpy(vec.data(), src, static_cast<std::size_t>(nrows) * sizeof(double));
            }
            col = std::move(vec);
        } else if (fmt0 == 'f' && fmt1 == '\0') {
            std::vector<double> vec(static_cast<std::size_t>(nrows));
            if (nrows > 0) {
                const auto* src = static_cast<const float*>(ca->buffers[1]) + off;
                for (std::int64_t r = 0; r < nrows; ++r) {
                    vec[static_cast<std::size_t>(r)] = static_cast<double>(src[r]);
                }
            }
            col = std::move(vec);
        } else if (fmt0 == 'l' && fmt1 == '\0') {
            std::vector<std::int64_t> vec(static_cast<std::size_t>(nrows));
            if (nrows > 0) {
                const auto* src = static_cast<const std::int64_t*>(ca->buffers[1]) + off;
                std::memcpy(vec.data(), src,
                            static_cast<std::size_t>(nrows) * sizeof(std::int64_t));
            }
            col = std::move(vec);
        } else if (fmt0 == 'i' && fmt1 == '\0') {
            std::vector<std::int32_t> vec(static_cast<std::size_t>(nrows));
            if (nrows > 0) {
                const auto* src = static_cast<const std::int32_t*>(ca->buffers[1]) + off;
                std::memcpy(vec.data(), src,
                            static_cast<std::size_t>(nrows) * sizeof(std::int32_t));
            }
            col = std::move(vec);
        } else if (fmt0 == 'b' && fmt1 == '\0') {
            std::vector<bool> vec(static_cast<std::size_t>(nrows), false);
            for (std::int64_t r = 0; r < nrows; ++r) {
                vec[static_cast<std::size_t>(r)] = arrow_detail::bitmap_get(ca->buffers[1], off, r);
            }
            col = std::move(vec);
        } else if (fmt0 == 'u' && fmt1 == '\0') {
            std::vector<std::string> vec(static_cast<std::size_t>(nrows));
            if (nrows > 0) {
                const auto* offsets = static_cast<const std::int32_t*>(ca->buffers[1]) + off;
                const auto* bytes = static_cast<const char*>(ca->buffers[2]);
                for (std::int64_t r = 0; r < nrows; ++r) {
                    const std::int32_t start = offsets[r];
                    const std::int32_t end = offsets[r + 1];
                    vec[static_cast<std::size_t>(r)].assign(bytes + start,
                                                            static_cast<std::size_t>(end - start));
                }
            }
            col = std::move(vec);
        } else if (fmt0 == 'U' && fmt1 == '\0') {
            std::vector<std::string> vec(static_cast<std::size_t>(nrows));
            if (nrows > 0) {
                const auto* offsets = static_cast<const std::int64_t*>(ca->buffers[1]) + off;
                const auto* bytes = static_cast<const char*>(ca->buffers[2]);
                for (std::int64_t r = 0; r < nrows; ++r) {
                    const std::int64_t start = offsets[r];
                    const std::int64_t end = offsets[r + 1];
                    vec[static_cast<std::size_t>(r)].assign(bytes + start,
                                                            static_cast<std::size_t>(end - start));
                }
            }
            col = std::move(vec);
        } else {
            throw std::invalid_argument("import_from_arrow: unsupported Arrow format '" +
                                        std::string(cs->format) + "'");
        }

        out.add_column(std::move(col_name), std::move(col), std::move(mask));
    }
    return out;
}

} // namespace pyrabind

#if defined(PYRABIND_TARGET_PYTHON)

namespace pyrabind::py_arrow_detail {

struct ArrowPyObj {
    PyObject_HEAD ArrowSchema schema;
    ArrowArray array;
};

inline void capsule_destroy_schema(PyObject* cap) noexcept {
    auto* sch = static_cast<ArrowSchema*>(PyCapsule_GetPointer(cap, "arrow_schema"));
    if (sch == nullptr) {
        PyErr_Clear();
        return;
    }
    if (sch->release != nullptr) {
        sch->release(sch);
    }
    delete sch;
}

inline void capsule_destroy_array(PyObject* cap) noexcept {
    auto* arr = static_cast<ArrowArray*>(PyCapsule_GetPointer(cap, "arrow_array"));
    if (arr == nullptr) {
        PyErr_Clear();
        return;
    }
    if (arr->release != nullptr) {
        arr->release(arr);
    }
    delete arr;
}

inline void arrow_obj_dealloc(PyObject* self) noexcept {
    auto* obj = reinterpret_cast<ArrowPyObj*>(self);
    if (obj->array.release != nullptr) {
        obj->array.release(&obj->array);
    }
    if (obj->schema.release != nullptr) {
        obj->schema.release(&obj->schema);
    }
    Py_TYPE(self)->tp_free(self);
}

inline PyObject* arrow_obj_c_array(PyObject* self, PyObject* args, PyObject* kwargs) {
    static const char* kwlist[] = {"requested_schema", nullptr};
    PyObject* req = Py_None;
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "|O:__arrow_c_array__",
                                     const_cast<char**>(kwlist), &req)) {
        return nullptr;
    }
    (void)req;
    auto* obj = reinterpret_cast<ArrowPyObj*>(self);
    if (obj->schema.release == nullptr || obj->array.release == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "Arrow batch has already been consumed");
        return nullptr;
    }
    auto* out_sch = new ArrowSchema(obj->schema);
    auto* out_arr = new ArrowArray(obj->array);
    obj->schema = ArrowSchema{};
    obj->array = ArrowArray{};

    PyObject* sch_cap = PyCapsule_New(out_sch, "arrow_schema", &capsule_destroy_schema);
    if (sch_cap == nullptr) {
        if (out_sch->release != nullptr) {
            out_sch->release(out_sch);
        }
        if (out_arr->release != nullptr) {
            out_arr->release(out_arr);
        }
        delete out_sch;
        delete out_arr;
        return nullptr;
    }
    PyObject* arr_cap = PyCapsule_New(out_arr, "arrow_array", &capsule_destroy_array);
    if (arr_cap == nullptr) {
        Py_DECREF(sch_cap);
        if (out_arr->release != nullptr) {
            out_arr->release(out_arr);
        }
        delete out_arr;
        return nullptr;
    }
    PyObject* tup = PyTuple_Pack(2, sch_cap, arr_cap);
    Py_DECREF(sch_cap);
    Py_DECREF(arr_cap);
    return tup;
}

inline PyTypeObject* get_arrow_wrapper_type() noexcept {
    static PyMethodDef methods[] = {
        {"__arrow_c_array__",
         reinterpret_cast<PyCFunction>(reinterpret_cast<void (*)(void)>(&arrow_obj_c_array)),
         METH_VARARGS | METH_KEYWORDS, "Export Arrow C Data Interface capsules"},
        {nullptr, nullptr, 0, nullptr},
    };

    static PyTypeObject type_obj = []() {
        PyTypeObject t{};
        Py_SET_REFCNT(reinterpret_cast<PyObject*>(&t), 1);
        t.tp_name = "pyrabind.ArrowBatch";
        t.tp_basicsize = sizeof(ArrowPyObj);
        t.tp_flags = Py_TPFLAGS_DEFAULT;
        t.tp_dealloc = &arrow_obj_dealloc;
        t.tp_methods = methods;
        return t;
    }();

    static bool ready = false;
    if (!ready) {
        if (PyType_Ready(&type_obj) == 0) {
            ready = true;
        }
    }
    return &type_obj;
}

inline PyObject* arrow_c_array_attr_name() noexcept {
    static PyObject* name = PyUnicode_InternFromString("__arrow_c_array__");
    return name;
}

inline bool import_arrow_py_into(PyObject* obj, pyrabind::arrow_batch& dst) noexcept {
    dst.release();
    PyObject* attr_name = arrow_c_array_attr_name();
    if (attr_name == nullptr) {
        return false;
    }
    PyObject* tup = PyObject_CallMethodNoArgs(obj, attr_name);
    if (tup == nullptr) {
        PyErr_Clear();
        return false;
    }
    if (!PyTuple_Check(tup) || PyTuple_GET_SIZE(tup) != 2) {
        Py_DECREF(tup);
        return false;
    }
    PyObject* sch_cap = PyTuple_GET_ITEM(tup, 0);
    PyObject* arr_cap = PyTuple_GET_ITEM(tup, 1);
    auto* sch = static_cast<ArrowSchema*>(PyCapsule_GetPointer(sch_cap, "arrow_schema"));
    auto* arr = static_cast<ArrowArray*>(PyCapsule_GetPointer(arr_cap, "arrow_array"));
    if (sch == nullptr || arr == nullptr || sch->release == nullptr || arr->release == nullptr) {
        PyErr_Clear();
        Py_DECREF(tup);
        return false;
    }
    dst.schema = *sch;
    dst.array = *arr;
    *sch = ArrowSchema{};
    *arr = ArrowArray{};
    Py_DECREF(tup);
    return true;
}

} // namespace pyrabind::py_arrow_detail

namespace nanobind::detail {

template <> struct type_caster<pyrabind::arrow_batch> {
    NB_TYPE_CASTER(pyrabind::arrow_batch, const_name("ArrowBatch"))

    bool from_python(handle src, uint32_t, cleanup_list*) noexcept {
        return pyrabind::py_arrow_detail::import_arrow_py_into(src.ptr(), value);
    }

    static handle from_cpp(pyrabind::arrow_batch&& batch, rv_policy, cleanup_list*) noexcept {
        PyTypeObject* tp = pyrabind::py_arrow_detail::get_arrow_wrapper_type();
        auto* obj = PyObject_New(pyrabind::py_arrow_detail::ArrowPyObj, tp);
        if (obj == nullptr) {
            return {};
        }
        obj->schema = batch.schema;
        obj->array = batch.array;
        batch.schema = ArrowSchema{};
        batch.array = ArrowArray{};
        return handle(reinterpret_cast<PyObject*>(obj));
    }

    static handle from_cpp(const pyrabind::arrow_batch&, rv_policy, cleanup_list*) noexcept {
        PyErr_SetString(PyExc_TypeError, "arrow_batch is move-only and cannot be copied");
        return {};
    }
};

template <> struct type_caster<pyrabind::arrow_batch_view> {
    NB_TYPE_CASTER(pyrabind::arrow_batch_view, const_name("ArrowBatch"))

    bool from_python(handle src, uint32_t, cleanup_list*) noexcept {
        auto holder = std::make_shared<pyrabind::arrow_batch>();
        if (!pyrabind::py_arrow_detail::import_arrow_py_into(src.ptr(), *holder)) {
            value = pyrabind::arrow_batch_view{};
            return false;
        }
        value.schema = &holder->schema;
        value.array = &holder->array;
        value.owner = std::move(holder);
        return true;
    }

    static handle from_cpp(const pyrabind::arrow_batch_view&, rv_policy, cleanup_list*) noexcept {
        PyErr_SetString(PyExc_TypeError, "arrow_batch_view is input-only");
        return {};
    }
};

} // namespace nanobind::detail

#elif defined(PYRABIND_TARGET_R)

namespace pyrabind::r_detail {

inline bool extract_r_arrow_pointers(SEXP x, SEXP& out_sch_xptr, ArrowArray*& out_arr,
                                     ArrowSchema*& out_sch) noexcept {
    if (x == nullptr || x == R_NilValue || TYPEOF(x) != EXTPTRSXP) {
        return false;
    }
    SEXP sch_xptr = R_ExternalPtrTag(x);
    if (sch_xptr == nullptr || sch_xptr == R_NilValue || TYPEOF(sch_xptr) != EXTPTRSXP) {
        sch_xptr = Rf_getAttrib(x, Rf_install("schema"));
        if (sch_xptr == nullptr || sch_xptr == R_NilValue || TYPEOF(sch_xptr) != EXTPTRSXP) {
            return false;
        }
    }
    auto* arr = static_cast<ArrowArray*>(R_ExternalPtrAddr(x));
    auto* sch = static_cast<ArrowSchema*>(R_ExternalPtrAddr(sch_xptr));
    if (arr == nullptr || sch == nullptr || arr->release == nullptr || sch->release == nullptr) {
        return false;
    }
    out_sch_xptr = sch_xptr;
    out_arr = arr;
    out_sch = sch;
    return true;
}

inline void r_finalize_arrow_schema(SEXP xptr) noexcept {
    auto* sch = static_cast<ArrowSchema*>(R_ExternalPtrAddr(xptr));
    if (sch != nullptr) {
        if (sch->release != nullptr) {
            sch->release(sch);
        }
        delete sch;
        R_ClearExternalPtr(xptr);
    }
}

inline void r_finalize_arrow_array(SEXP xptr) noexcept {
    auto* arr = static_cast<ArrowArray*>(R_ExternalPtrAddr(xptr));
    if (arr != nullptr) {
        if (arr->release != nullptr) {
            arr->release(arr);
        }
        delete arr;
        R_ClearExternalPtr(xptr);
    }
}

template <> struct type_caster<arrow_batch> {
    arrow_batch val;

    bool from_r(SEXP x, bool) {
        val.release();
        SEXP sch_xptr = R_NilValue;
        ArrowArray* arr = nullptr;
        ArrowSchema* sch = nullptr;
        if (!extract_r_arrow_pointers(x, sch_xptr, arr, sch)) {
            return false;
        }
        val.schema = *sch;
        val.array = *arr;
        *sch = ArrowSchema{};
        *arr = ArrowArray{};
        return true;
    }

    arrow_batch& get() noexcept { return val; }

    static SEXP to_r(arrow_batch batch, rv_policy, SEXP) {
        auto* heap_sch = new ArrowSchema(batch.schema);
        auto* heap_arr = new ArrowArray(batch.array);
        batch.schema = ArrowSchema{};
        batch.array = ArrowArray{};

        SEXP sch_xptr = PROTECT(R_MakeExternalPtr(heap_sch, R_NilValue, R_NilValue));
        R_RegisterCFinalizerEx(sch_xptr, &r_finalize_arrow_schema, TRUE);
        SEXP sch_cls = PROTECT(Rf_mkString("nanoarrow_schema"));
        Rf_setAttrib(sch_xptr, R_ClassSymbol, sch_cls);

        SEXP arr_xptr = PROTECT(R_MakeExternalPtr(heap_arr, sch_xptr, R_NilValue));
        R_RegisterCFinalizerEx(arr_xptr, &r_finalize_arrow_array, TRUE);
        SEXP arr_cls = PROTECT(Rf_mkString("nanoarrow_array"));
        Rf_setAttrib(arr_xptr, R_ClassSymbol, arr_cls);

        UNPROTECT(4);
        return arr_xptr;
    }
};

template <> struct type_caster<arrow_batch_view> {
    arrow_batch_view val;

    bool from_r(SEXP x, bool) {
        val = arrow_batch_view{};
        SEXP sch_xptr = R_NilValue;
        ArrowArray* arr = nullptr;
        ArrowSchema* sch = nullptr;
        if (!extract_r_arrow_pointers(x, sch_xptr, arr, sch)) {
            return false;
        }
        val.schema = sch;
        val.array = arr;
        val.owner = r_retain_arrow_lease(x, sch_xptr);
        return true;
    }

    const arrow_batch_view& get() const noexcept { return val; }
    arrow_batch_view& get() noexcept { return val; }
};

} // namespace pyrabind::r_detail

#endif
