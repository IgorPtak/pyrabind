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

    void add_column(std::string name, column_data col) {
        if (contains(name)) {
            throw std::invalid_argument("duplicate data_frame column '" + name + "'");
        }
        const std::size_t len = column_size(col);
        if (!columns.empty() && len != nrows()) {
            throw std::invalid_argument("column '" + name + "' length (" + std::to_string(len) +
                                        ") does not match data_frame nrows (" +
                                        std::to_string(nrows()) + ")");
        }
        names.push_back(std::move(name));
        columns.push_back(std::move(col));
    }
};

} // namespace pyrabind
