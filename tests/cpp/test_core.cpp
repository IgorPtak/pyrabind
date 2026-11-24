#include "pyrabind/arrow.hpp"
#include "pyrabind/dataframe_types.hpp"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

int main() {
    pyrabind::data_frame df;
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

    // Nullable data_frame + Arrow C Data Interface round-trip across all 5 dtypes
    pyrabind::data_frame ndf;
    ndf.add_column("f64", std::vector<double>{1.5, 0.0, 3.5}, {1, 0, 1});
    ndf.add_column("i64", std::vector<std::int64_t>{10, 20, 0}, {1, 1, 0});
    ndf.add_column("i32", std::vector<std::int32_t>{-5, 0, 7}, {1, 0, 1});
    ndf.add_column("flag", std::vector<bool>{true, false, false}, {1, 0, 1});
    ndf.add_column("txt", std::vector<std::string>{"alpha", "", "gamma"}, {1, 0, 1});

    assert(ndf.any_nulls());
    assert(ndf.has_nulls(0) && ndf.is_null(0, 1) && !ndf.is_null(0, 0));
    assert(ndf.is_null(1, 2) && ndf.is_null(2, 1) && ndf.is_null(3, 1) && ndf.is_null(4, 1));

    // Toggle set_null
    ndf.set_null(0, 0, true);
    assert(ndf.is_null(0, 0));
    ndf.set_null(0, 0, false);
    assert(!ndf.is_null(0, 0));

    {
        pyrabind::arrow_batch batch = pyrabind::export_to_arrow(ndf);
        assert(batch.is_valid());
        assert(std::strcmp(batch.schema.format, "+s") == 0);
        assert(batch.array.length == 3);
        assert(batch.array.n_children == 5);

        pyrabind::data_frame restored = pyrabind::import_from_arrow(batch);
        assert(restored.ncols() == 5);
        assert(restored.nrows() == 3);
        assert(restored.is_null(0, 1) && !restored.is_null(0, 2));
        assert(std::get<std::vector<double>>(restored["f64"])[2] == 3.5);
        assert(restored.is_null(1, 2));
        assert(std::get<std::vector<std::int64_t>>(restored["i64"])[1] == 20);
        assert(restored.is_null(2, 1));
        assert(std::get<std::vector<std::int32_t>>(restored["i32"])[0] == -5);
        assert(restored.is_null(3, 1));
        assert(std::get<std::vector<bool>>(restored["flag"])[0] == true);
        assert(restored.is_null(4, 1));
        assert(std::get<std::vector<std::string>>(restored["txt"])[2] == "gamma");

        // Release child schema and array independently before batch destruction to verify ASan
        // safety.
        ArrowSchema* c0_sch = batch.schema.children[0];
        ArrowArray* c0_arr = batch.array.children[0];
        c0_sch->release(c0_sch);
        c0_arr->release(c0_arr);
    }

    // Empty data_frame Arrow round-trip
    pyrabind::data_frame empty_df;
    pyrabind::arrow_batch empty_batch = pyrabind::export_to_arrow(empty_df);
    pyrabind::data_frame empty_restored = pyrabind::import_from_arrow(empty_batch);
    assert(empty_restored.ncols() == 0 && empty_restored.nrows() == 0);

    return 0;
}
