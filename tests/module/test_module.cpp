#include "nanorbind/dataframe.hpp"
#include "nanorbind/nanorbind.hpp"
#include "nanorbind/ndarray.hpp"
#include "nanorbind/operators.hpp"
#include "nanorbind/stl.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

namespace nb = nanorbind;

namespace {

enum class Color : std::int32_t { Red = 1, Green = 2, Blue = 4 };

struct Vec2 {
    double x = 0.0;
    double y = 0.0;

    Vec2() = default;
    Vec2(double xv, double yv) : x(xv), y(yv) {}

    double norm() const { return std::hypot(x, y); }
    void scale(double factor) {
        x *= factor;
        y *= factor;
    }

    static Vec2 zero() { return {0.0, 0.0}; }

    Vec2 operator-() const { return {-x, -y}; }
    Vec2 operator+(const Vec2& o) const { return {x + o.x, y + o.y}; }
    Vec2 operator-(const Vec2& o) const { return {x - o.x, y - o.y}; }
    Vec2 operator*(double s) const { return {x * s, y * s}; }
    bool operator==(const Vec2& o) const { return x == o.x && y == o.y; }
    bool operator!=(const Vec2& o) const { return !(*this == o); }
    bool operator<(const Vec2& o) const { return norm() < o.norm(); }

    std::string repr() const {
        return "Vec2(" + std::to_string(x) + ", " + std::to_string(y) + ")";
    }
};

Vec2 operator*(double s, const Vec2& v) {
    return v * s;
}

struct Animal {
    std::string name;
    explicit Animal(std::string n) : name(std::move(n)) {}
    virtual ~Animal() = default;
    virtual std::string speak() const { return name + ": ..."; }
};

struct Dog : Animal {
    Dog(std::string n) : Animal(std::move(n)) {}
    std::string speak() const override { return name + ": woof"; }
    std::string fetch() const { return name + " fetched the ball"; }
};

std::string greet_animal(const Animal& a) {
    return "hello " + a.speak();
}

struct Resource {
    int id = 0;
    explicit Resource(int v) : id(v) {}
};

struct Container {
    std::vector<int> items;
    Resource embedded{99};
    Resource* attached = nullptr;

    explicit Container(std::vector<int> vals) : items(std::move(vals)) {}

    std::size_t size() const { return items.size(); }

    int get_item(std::ptrdiff_t idx) const {
        if (idx < 0) {
            idx += static_cast<std::ptrdiff_t>(items.size());
        }
        if (idx < 0 || static_cast<std::size_t>(idx) >= items.size()) {
            throw nb::index_error("Container index out of range");
        }
        return items[static_cast<std::size_t>(idx)];
    }

    void set_item(std::ptrdiff_t idx, int val) {
        if (idx < 0) {
            idx += static_cast<std::ptrdiff_t>(items.size());
        }
        if (idx < 0 || static_cast<std::size_t>(idx) >= items.size()) {
            throw nb::index_error("Container index out of range");
        }
        items[static_cast<std::size_t>(idx)] = val;
    }

    bool contains(int v) const {
        for (int e : items) {
            if (e == v) {
                return true;
            }
        }
        return false;
    }

    Resource& get_embedded() { return embedded; }
    void attach(Resource* res) { attached = res; }
    int attached_id() const { return attached != nullptr ? attached->id : -1; }
};

Container create_container(int count, int fill) {
    return Container(std::vector<int>(static_cast<std::size_t>(count), fill));
}

struct Accumulator {
    int base = 0;
    explicit Accumulator(int b) : base(b) {}
    int add(int x) {
        base += x;
        return base;
    }
    int add(int x) const { return base + x * 10; }
};

int add_with_defaults(int a, int b = 10, int c = 100) {
    return a + b + c;
}

std::string classify(int x) {
    return "int:" + std::to_string(x);
}
std::string classify(double x) {
    return "double:" + std::to_string(static_cast<int>(x));
}

void check_positive(int x) {
    if (x < 0) {
        throw nb::value_error("expected non-negative value");
    }
}

void lookup_key(const std::map<std::string, int>& m, const std::string& k) {
    if (m.find(k) == m.end()) {
        throw nb::key_error("key not found");
    }
}

std::optional<int> opt_double(std::optional<int> v) {
    if (!v.has_value()) {
        return std::nullopt;
    }
    return (*v) * 2;
}

std::pair<int, std::string> swap_pair(std::pair<std::string, int> p) {
    return {p.second, std::move(p.first)};
}

std::tuple<int, double, std::string> make_triplet(int a, double b, const std::string& c) {
    return {a, b, c};
}

std::array<int, 3> reverse_arr3(std::array<int, 3> a) {
    return {a[2], a[1], a[0]};
}

std::set<std::string> unique_tags(const std::set<std::string>& s) {
    return s;
}

std::string describe_variant(const std::variant<int, double, std::string>& v) {
    if (std::holds_alternative<int>(v)) {
        return "i:" + std::to_string(std::get<int>(v));
    }
    if (std::holds_alternative<double>(v)) {
        return "d:" + std::to_string(static_cast<int>(std::get<double>(v)));
    }
    return "s:" + std::get<std::string>(v);
}

std::string path_filename(const std::filesystem::path& p) {
    return p.filename().string();
}

std::vector<double> map_doubles(const std::vector<double>& xs,
                                const std::function<double(double)>& fn) {
    std::vector<double> out;
    out.reserve(xs.size());
    for (double x : xs) {
        out.push_back(fn(x));
    }
    return out;
}

std::function<int(int)> make_adder(int k) {
    return [k](int x) { return x + k; };
}

double nd_dot_1d(nb::ndarray<const double, nb::ndim<1>> a,
                 nb::ndarray<const double, nb::ndim<1>> b) {
    if (a.shape(0) != b.shape(0)) {
        throw nb::value_error("1D ndarrays must have identical length");
    }
    auto va = a.view();
    auto vb = b.view();
    double acc = 0.0;
    for (std::size_t i = 0; i < va.shape(0); ++i) {
        acc += va(i) * vb(i);
    }
    return acc;
}

double nd_mat_trace_and_sum(nb::ndarray<const double, nb::ndim<2>> m, double& trace_out) {
    auto vm = m.view();
    const std::size_t rows = vm.shape(0);
    const std::size_t cols = vm.shape(1);
    double sum = 0.0;
    for (std::size_t r = 0; r < rows; ++r) {
        for (std::size_t c = 0; c < cols; ++c) {
            sum += vm(r, c);
        }
    }
    trace_out = 0.0;
    const std::size_t diag = rows < cols ? rows : cols;
    for (std::size_t i = 0; i < diag; ++i) {
        trace_out += vm(i, i);
    }
    return sum;
}

std::pair<double, double> nd_sum_and_trace(nb::ndarray<const double, nb::ndim<2>> m) {
    double tr = 0.0;
    double s = nd_mat_trace_and_sum(m, tr);
    return {s, tr};
}

nb::ndarray<nb::numpy, double, nb::ndim<1>> nd_scale_1d(nb::ndarray<const double, nb::ndim<1>> a,
                                                        double factor, double offset) {
    const std::size_t n = a.shape(0);
    auto* buf = new double[n];
    auto va = a.view();
    for (std::size_t i = 0; i < n; ++i) {
        buf[i] = va(i) * factor + offset;
    }
    nb::capsule owner(buf, [](void* p) noexcept { delete[] static_cast<double*>(p); });
    return nb::ndarray<nb::numpy, double, nb::ndim<1>>(buf, {n}, owner);
}

nb::ndarray<nb::numpy, double, nb::ndim<2>> nd_make_2d(std::size_t rows, std::size_t cols) {
    auto* buf = new double[rows * cols];
    for (std::size_t r = 0; r < rows; ++r) {
        for (std::size_t c = 0; c < cols; ++c) {
            buf[r * cols + c] = static_cast<double>(r * 10 + c + 1);
        }
    }
    nb::capsule owner(buf, [](void* p) noexcept { delete[] static_cast<double*>(p); });
    return nb::ndarray<nb::numpy, double, nb::ndim<2>>(buf, {rows, cols}, owner);
}

nb::data_frame transform_df(const nb::data_frame& df) {
    const auto* x_col = std::get_if<std::vector<double>>(&df["x"]);
    if (x_col == nullptr) {
        throw nb::type_error("column 'x' must be float64");
    }
    const auto& id_col = df["id"];
    const auto& tag_col = df["tag"];

    std::vector<double> scaled;
    scaled.reserve(x_col->size());
    std::visit(
        [&](const auto& id_vec) {
            using VecT = std::decay_t<decltype(id_vec)>;
            if constexpr (std::is_same_v<VecT, std::vector<std::string>>) {
                throw nb::type_error("column 'id' must be numeric");
            } else {
                for (std::size_t i = 0; i < x_col->size(); ++i) {
                    scaled.push_back((*x_col)[i] * 2.0 + static_cast<double>(id_vec[i]));
                }
            }
        },
        id_col);

    nb::data_frame out;
    out.add_column("id", id_col);
    out.add_column("x", *x_col);
    out.add_column("scaled", std::move(scaled));
    out.add_column("tag", tag_col);
    return out;
}

} // namespace

NANORBIND_MODULE(test_module, m) {
    using namespace nb::literals;
    m.doc() = "Comprehensive dual-target nanorbind test module";
    m.attr("PI") = 3.141592653589793;

    nb::enum_<Color>(m, "Color", nb::is_arithmetic())
        .value("Red", Color::Red)
        .value("Green", Color::Green)
        .value("Blue", Color::Blue)
        .export_values();

    m.def("add_defaults", &add_with_defaults, "a"_a, "b"_a = 10, "c"_a = 100);
    m.def("classify", nb::overload_cast<int>(&classify), "x"_a);
    m.def("classify", nb::overload_cast<double>(&classify), "x"_a);
    m.def("check_positive", &check_positive, "x"_a);
    m.def("lookup_key", &lookup_key, "m"_a, "k"_a);
    m.def("opt_double", &opt_double, "v"_a = nb::none());
    m.def("swap_pair", &swap_pair, "p"_a);
    m.def("make_triplet", &make_triplet, "a"_a, "b"_a, "c"_a);
    m.def("reverse_arr3", &reverse_arr3, "a"_a);
    m.def("unique_tags", &unique_tags, "s"_a);
    m.def("describe_variant", &describe_variant, "v"_a);
    m.def("path_filename", &path_filename, "p"_a);
    m.def("map_doubles", &map_doubles, "xs"_a, "fn"_a);
    m.def("make_adder", &make_adder, "k"_a);
    m.def("nd_dot_1d", &nd_dot_1d, "a"_a, "b"_a);
    m.def("nd_sum_and_trace", &nd_sum_and_trace, "m"_a);
    m.def("nd_scale_1d", &nd_scale_1d, "a"_a, "factor"_a = 1.0, "offset"_a = 0.0);
    m.def("nd_make_2d", &nd_make_2d, "rows"_a, "cols"_a);
    m.def("transform_df", &transform_df, "df"_a);

    auto sub = m.def_submodule("math", "Math helpers submodule");
    sub.def("square", [](double x) { return x * x; }, "x"_a);

    nb::class_<Vec2>(m, "Vec2")
        .def(nb::init<>())
        .def(nb::init<double, double>(), "x"_a, "y"_a)
        .def_rw("x", &Vec2::x)
        .def_rw("y", &Vec2::y)
        .def_prop_ro("norm", &Vec2::norm)
        .def("scale", &Vec2::scale, "factor"_a)
        .def_static("zero", &Vec2::zero)
        .def("__repr__", &Vec2::repr)
        .def(-nb::self)
        .def(nb::self + nb::self)
        .def(nb::self - nb::self)
        .def(nb::self * double())
        .def(double() * nb::self)
        .def(nb::self == nb::self)
        .def(nb::self != nb::self)
        .def(nb::self < nb::self);

    nb::class_<Animal>(m, "Animal")
        .def(nb::init<std::string>(), "name"_a)
        .def_ro("name", &Animal::name)
        .def("speak", &Animal::speak);

    nb::class_<Dog, Animal>(m, "Dog")
        .def(nb::init<std::string>(), "name"_a)
        .def("fetch", &Dog::fetch);

    m.def("greet_animal", &greet_animal, "a"_a);

    nb::class_<Resource>(m, "Resource").def(nb::init<int>(), "id"_a).def_rw("id", &Resource::id);

    nb::class_<Container>(m, "Container")
        .def(nb::init<std::vector<int>>(), "items"_a)
        .def(nb::new_(&create_container), "count"_a, "fill"_a)
        .def("__len__", &Container::size)
        .def("__getitem__", &Container::get_item)
        .def("__setitem__", &Container::set_item)
        .def("__contains__", &Container::contains)
        .def("get_embedded", &Container::get_embedded, nb::rv_policy::reference_internal)
        .def("attach", &Container::attach, nb::keep_alive<1, 2>())
        .def("attached_id", &Container::attached_id);

    nb::class_<Accumulator>(m, "Accumulator")
        .def(nb::init<int>(), "base"_a)
        .def_ro("base", &Accumulator::base)
        .def("add", nb::overload_cast<int>(&Accumulator::add), "x"_a)
        .def("add_const", nb::overload_cast<int>(&Accumulator::add, nb::const_), "x"_a);
}
