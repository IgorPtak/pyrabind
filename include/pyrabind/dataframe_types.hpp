#pragma once

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace pyrabind {

using column_data =
    std::variant<std::vector<double>, std::vector<std::int64_t>, std::vector<std::int32_t>,
                 std::vector<bool>, std::vector<std::string>>;

inline std::size_t column_size(const column_data& col) noexcept {
    return std::visit([](const auto& vec) { return vec.size(); }, col);
}

struct data_frame {
    std::vector<std::string> names;
    std::vector<column_data> columns;
    // Optional per-column validity masks aligned with `columns`:
    // an empty inner vector (or missing entry) means all rows are valid (non-null);
    // otherwise length equals nrows() with 1 = valid and 0 = null/NA.
    std::vector<std::vector<std::uint8_t>> validity;

    std::size_t ncols() const noexcept { return columns.size(); }

    std::size_t nrows() const noexcept {
        return columns.empty() ? 0 : column_size(columns.front());
    }

    bool empty() const noexcept { return columns.empty() || nrows() == 0; }

    bool contains(std::string_view name) const noexcept {
        for (const auto& n : names) {
            if (n == name) {
                return true;
            }
        }
        return false;
    }

    std::size_t index_of(std::string_view name) const {
        for (std::size_t i = 0; i < names.size(); ++i) {
            if (names[i] == name) {
                return i;
            }
        }
        throw std::out_of_range("data_frame has no column '" + std::string(name) + "'");
    }

    column_data& operator[](std::string_view name) { return columns[index_of(name)]; }

    const column_data& operator[](std::string_view name) const { return columns[index_of(name)]; }

    bool has_nulls(std::size_t col_idx) const noexcept {
        if (col_idx >= validity.size() || validity[col_idx].empty()) {
            return false;
        }
        for (std::uint8_t v : validity[col_idx]) {
            if (v == 0) {
                return true;
            }
        }
        return false;
    }

    bool any_nulls() const noexcept {
        for (std::size_t i = 0; i < validity.size(); ++i) {
            if (has_nulls(i)) {
                return true;
            }
        }
        return false;
    }

    bool is_null(std::size_t col_idx, std::size_t row_idx) const noexcept {
        if (col_idx >= validity.size() || row_idx >= validity[col_idx].size()) {
            return false;
        }
        return validity[col_idx][row_idx] == 0;
    }

    void set_null(std::size_t col_idx, std::size_t row_idx, bool null_val = true) {
        if (col_idx >= columns.size() || row_idx >= column_size(columns[col_idx])) {
            throw std::out_of_range("data_frame index out of range in set_null");
        }
        if (validity.size() < columns.size()) {
            validity.resize(columns.size());
        }
        auto& mask = validity[col_idx];
        if (mask.empty()) {
            if (!null_val) {
                return;
            }
            mask.assign(column_size(columns[col_idx]), 1);
        }
        mask[row_idx] = null_val ? 0 : 1;
    }

    void add_column(std::string name, column_data col, std::vector<std::uint8_t> val_mask = {}) {
        if (contains(name)) {
            throw std::invalid_argument("duplicate data_frame column '" + name + "'");
        }
        const std::size_t len = column_size(col);
        if (!columns.empty() && len != nrows()) {
            throw std::invalid_argument("column '" + name + "' length (" + std::to_string(len) +
                                        ") does not match data_frame nrows (" +
                                        std::to_string(nrows()) + ")");
        }
        if (!val_mask.empty() && val_mask.size() != len) {
            throw std::invalid_argument("validity mask length for column '" + name +
                                        "' does not match column length");
        }
        names.push_back(std::move(name));
        columns.push_back(std::move(col));
        if (!val_mask.empty() || !validity.empty()) {
            validity.resize(columns.size());
            if (!val_mask.empty()) {
                validity.back() = std::move(val_mask);
            }
        }
    }
};

} // namespace pyrabind
