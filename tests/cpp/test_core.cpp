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

        // Zero-copy arrow_batch_view, column_span, string_column_view, insert_column, with_column
        auto v = batch.view();
        assert(v.is_valid());
        assert(v.ncols() == 5 && v.nrows() == 3);
        assert(v.column_name(4) == "txt");
        assert(v.column_format("f64") == "g");

        auto sf64 = v.column_span<double>("f64");
        assert(sf64.size() == 3 && sf64.has_nulls());
        assert(sf64[0] == 1.5 && sf64.is_null(1) && !sf64.is_null(2) && sf64[2] == 3.5);

        auto si64 = v.column_span<std::int64_t>("i64");
        assert(si64.size() == 3 && si64[0] == 10 && si64[1] == 20 && si64.is_null(2));

        auto stxt = v.string_column_view("txt");
        assert(stxt.size() == 3 && stxt.has_nulls());
        assert(stxt[0] == "alpha" && stxt.is_null(1) && stxt[2] == "gamma");

        const void* orig_f64_ptr = batch.array.children[0]->buffers[1];
        const void* orig_txt_data = batch.array.children[4]->buffers[2];

        // Zero-copy insert_column in the middle, then release original stack batch
        // to verify composed batch keeps buffers alive via shared ownership.
        auto shared_orig = std::make_shared<pyrabind::arrow_batch>(std::move(batch));
        pyrabind::arrow_batch_view sv{&shared_orig->schema, &shared_orig->array, shared_orig};
        pyrabind::arrow_batch inserted =
            sv.insert_column(2, "doubled", std::vector<double>{3.0, 0.0, 7.0}, {1, 0, 1});
        shared_orig.reset();

        assert(inserted.schema.n_children == 6);
        assert(std::strcmp(inserted.schema.children[2]->name, "doubled") == 0);
        // Untouched columns share identical buffer addresses zero-copy:
        assert(inserted.array.children[0]->buffers[1] == orig_f64_ptr);
        assert(inserted.array.children[5]->buffers[2] == orig_txt_data);

        // Zero-copy with_column (replace existing + append new) and independent child release:
        pyrabind::arrow_batch replaced = std::move(inserted).with_column(
            "doubled", std::vector<double>{30.0, 0.0, 70.0}, {1, 0, 1});
        pyrabind::arrow_batch appended =
            std::move(replaced).with_column("tail", std::vector<std::int32_t>{7, 8, 9});
        assert(appended.schema.n_children == 7);
        assert(appended.array.children[0]->buffers[1] == orig_f64_ptr);

        auto av = appended.view();
        assert(av.column_span<double>("doubled")[0] == 30.0);
        assert(av.column_span<std::int32_t>("tail")[2] == 9);

        // Simulated root slice offset > 0 test:
        appended.array.offset = 1;
        appended.array.length = 2;
        auto sliced_view = appended.view();
        auto sliced_f64 = sliced_view.column_span<double>("f64");
        auto sliced_txt = sliced_view.string_column_view("txt");
        assert(sliced_f64.size() == 2 && sliced_f64.is_null(0) && sliced_f64[1] == 3.5);
        assert(sliced_txt.size() == 2 && sliced_txt.is_null(0) && sliced_txt[1] == "gamma");

        pyrabind::arrow_batch sliced_out =
            sliced_view.insert_column(0, "s", std::vector<double>{9.0, 10.0});
        pyrabind::data_frame sliced_df = pyrabind::import_from_arrow(sliced_out);
        assert(sliced_df.nrows() == 2);
        assert(sliced_df.is_null(1, 0) &&
               std::get<std::vector<double>>(sliced_df["f64"])[1] == 3.5);
        assert(sliced_df.is_null(6, 0) &&
               std::get<std::vector<std::string>>(sliced_df["txt"])[1] == "gamma");

        // Release child schema and array independently before batch destruction to verify ASan
        // safety.
        ArrowSchema* c0_sch = sliced_out.schema.children[0];
        ArrowArray* c0_arr = sliced_out.array.children[0];
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
