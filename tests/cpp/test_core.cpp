#include "nanorbind/dataframe_types.hpp"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

int main() {
    nanorbind::data_frame df;
    assert(df.empty());
    df.add_column("id", std::vector<std::int64_t>{1, 2, 3});
    df.add_column("x", std::vector<double>{1.5, 2.5, 3.5});
    df.add_column("tag", std::vector<std::string>{"a", "b", "c"});

    assert(df.ncols() == 3);
    assert(df.nrows() == 3);
    assert(df.contains("id"));
    assert(!df.contains("missing"));
    assert(df.index_of("x") == 1);

    const auto& x_col = std::get<std::vector<double>>(df["x"]);
    assert(x_col[1] == 2.5);

    bool threw_dup = false;
    try {
        df.add_column("id", std::vector<std::int64_t>{4, 5, 6});
    } catch (const std::invalid_argument&) {
        threw_dup = true;
    }
    assert(threw_dup);

    bool threw_len = false;
    try {
        df.add_column("bad", std::vector<double>{1.0});
    } catch (const std::invalid_argument&) {
        threw_len = true;
    }
    assert(threw_len);

    bool threw_key = false;
    try {
        (void)df["unknown"];
    } catch (const std::out_of_range&) {
        threw_key = true;
    }
    assert(threw_key);

    return 0;
}
