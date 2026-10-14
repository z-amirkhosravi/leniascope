#include "src/morphology.h"
#include <algorithm>
#include <functional>
#include <iostream>
#include <limits>
#include <numeric>
#include <stdexcept>
using namespace lenia;
namespace {
void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
void rejects(const std::function<void()> &f) {
    bool failed = false;
    try {
        f();
    } catch (const std::exception &) {
        failed = true;
    }
    check(failed, "Invalid morphology input accepted");
}
State blank(int w = 8, int h = 8, int d = 1) {
    Config c{w, h, d, 1};
    return {c, std::vector<double>(c.count(), 0)};
}
void cell(State &s, int x, int y, int z = 0, double value = 1) {
    auto &c = s.config;
    s.values[(size_t(z) * c.height + y) * c.width + x] = value;
}
State shifted(const State &s, int dx, int dy, int dz) {
    auto out = s;
    const auto &c = s.config;
    for (int z = 0; z < c.depth; ++z)
        for (int y = 0; y < c.height; ++y)
            for (int x = 0; x < c.width; ++x)
                cell(out, (x + dx) % c.width, (y + dy) % c.height, (z + dz) % c.depth,
                     s.values[(size_t(z) * c.height + y) * c.width + x]);
    return out;
}
void expected(const State &s, uint64_t active, uint64_t components, uint64_t largest, uint64_t winding,
              uint32_t axes, double threshold = .01) {
    auto result = measure_morphology(s, threshold);
    check(result.active_cells == active && result.components == components &&
              result.largest_cells == largest && result.winding_components == winding &&
              result.winding_axes == axes,
          "Analytic shape / winding measurement differs");
    auto map = label_components(s, threshold);
    check(morphology_csv(map.summary) == morphology_csv(result) && map.components.size() == components &&
              map.labels.size() == s.values.size(),
          "Component map summary differs");
    std::vector<uint32_t> sizes(map.components.size());
    uint32_t all_axes = 0, winding_count = 0;
    for (size_t i = 0; i < map.labels.size(); ++i) {
        auto id = map.labels[i];
        check((id != 0) == (s.values[i] > threshold) && id <= components,
              "Labels disagree with exact threshold mask");
        if (id)
            ++sizes[id - 1];
    }
    for (size_t i = 0; i < sizes.size(); ++i) {
        check(sizes[i] == map.components[i].cells, "Component size disagrees with cell membership");
        all_axes |= map.components[i].winding_axes;
        winding_count += map.components[i].winding_axes != 0;
    }
    check(all_axes == axes && winding_count == winding, "Per-component winding disagrees with summary");
    for (const auto &offset : {std::vector<int>{1, 2, 1},
                               std::vector<int>{s.config.width / 2, s.config.height / 2, s.config.depth / 2},
                               std::vector<int>{s.config.width - 1, s.config.height - 1, s.config.depth - 1}})
        check(morphology_csv(measure_morphology(shifted(s, offset[0], offset[1], offset[2]), threshold)) ==
                  morphology_csv(result),
              "Shape measurements changed when translated across periodic seams");
}
// Independent disjoint-set oracle for component counts/sizes: union only forward graph edges.
void compare_components(const State &s) {
    const auto &c = s.config;
    std::vector<size_t> parent(c.count()), sizes(c.count());
    std::iota(parent.begin(), parent.end(), 0);
    auto root = [&](size_t i) {
        while (parent[i] != i)
            i = parent[i];
        return i;
    };
    auto index = [&](int x, int y, int z) { return (size_t(z) * c.height + y) * c.width + x; };
    for (int z = 0; z < c.depth; ++z)
        for (int y = 0; y < c.height; ++y)
            for (int x = 0; x < c.width; ++x) {
                size_t i = index(x, y, z);
                if (s.values[i] <= .5)
                    continue;
                for (size_t j : {index((x + 1) % c.width, y, z), index(x, (y + 1) % c.height, z),
                                 index(x, y, (z + 1) % c.depth)})
                    if (s.values[j] > .5)
                        parent[root(i)] = root(j);
            }
    for (size_t i = 0; i < s.values.size(); ++i)
        if (s.values[i] > .5)
            ++sizes[root(i)];
    auto actual = measure_morphology(s, .5);
    auto map = label_components(s, .5);
    std::vector<uint32_t> root_label(c.count());
    for (size_t i = 0; i < s.values.size(); ++i) {
        if (s.values[i] <= .5) {
            check(map.labels[i] == 0, "Inactive cell has a component label");
            continue;
        }
        auto r = root(i), id = size_t(map.labels[i]);
        check(id > 0 && id <= map.components.size(), "Active cell missing from map");
        check(root_label[r] == 0 || root_label[r] == id, "Oracle component split between labels");
        root_label[r] = uint32_t(id);
        check(map.components[id - 1].cells == sizes[r], "Label joins distinct oracle components");
    }
    check(map.components.size() == actual.components, "Map has extra/missing components");
    check(actual.components ==
                  uint64_t(std::count_if(sizes.begin(), sizes.end(), [](size_t n) { return n > 0; })) &&
              actual.largest_cells == *std::max_element(sizes.begin(), sizes.end()),
          "Flood fill disagrees with independent connectivity oracle");
    check(morphology_csv(actual) == morphology_csv(measure_morphology(shifted(s, 3, 1, 1), .5)),
          "Random shape winding changed under translation");
}
} // namespace
void morphology_tests() {
    auto s = blank();
    expected(s, 0, 0, 0, 0, 0);
    cell(s, 0, 2);
    cell(s, 7, 2); // Seam crossing alone does not wind.
    expected(s, 2, 1, 2, 0, 0);
    cell(s, 3, 2);
    expected(s, 3, 2, 2, 0, 0);
    s = blank();
    cell(s, 0, 0);
    cell(s, 7, 7); // Diagonal contact is not a face connection.
    expected(s, 2, 2, 1, 0, 0);
    s = blank();
    for (int x = 0; x < 8; ++x)
        cell(s, x, 1);
    expected(s, 8, 1, 8, 1, 1);
    for (int x = 0; x < 8; ++x)
        cell(s, x, 4);
    expected(s, 16, 2, 8, 2, 1);
    for (int y = 0; y < 8; ++y)
        cell(s, 2, y);
    expected(s, 22, 1, 22, 1, 3);
    s = blank();
    for (int x = 2; x <= 5; ++x) {
        cell(s, x, 2);
        cell(s, x, 5);
    }
    for (int y = 3; y <= 4; ++y) {
        cell(s, 2, y);
        cell(s, 5, y);
    }
    expected(s, 12, 1, 12, 0, 0); // A local ring has a hole but no winding around the domain.
    s = blank(16, 16);
    for (int y = 0; y < 16; ++y)
        for (int k = 0; k < 3; ++k)
            cell(s, (2 * y + k) % 16, y);
    expected(s, 48, 1, 48, 1, 3); // One staircase loop with winding (2,1).
    s = blank(8, 4, 16);
    cell(s, 0, 0, 0);
    cell(s, 0, 0, 15);
    cell(s, 7, 0, 0);
    cell(s, 0, 3, 0);
    expected(s, 4, 1, 4, 0, 0); // Cross all three seams without a noncontractible path.
    s = blank(8, 4, 16);
    for (int z = 0; z < 16; ++z)
        cell(s, 2, 1, z);
    expected(s, 16, 1, 16, 1, 4);
    s = blank(8, 8, 8);
    for (int z = 2; z <= 5; ++z)
        for (int y = 2; y <= 5; ++y)
            for (int x = 2; x <= 5; ++x)
                if (x == 2 || x == 5 || y == 2 || y == 5 || z == 2 || z == 5)
                    cell(s, x, y, z);
    expected(s, 56, 1, 56, 0, 0); // Hollow shell, also after translating through a corner.
    s = blank(8, 8, 8);
    for (int t = 0; t < 8; ++t) {
        cell(s, t, 1, 1);
        cell(s, 4, 4, t);
    }
    cell(s, 0, 6, 6);
    cell(s, 7, 6, 6);
    expected(s, 18, 3, 8, 2, 5);
    auto map = label_components(s);
    auto at = [&](int x, int y, int z) { return map.components.at(map.labels[(z * 8 + y) * 8 + x] - 1); };
    check(at(0, 1, 1).winding_axes == 1 && at(4, 4, 0).winding_axes == 4 && at(0, 6, 6).winding_axes == 0 &&
              at(0, 6, 6).cells == 2,
          "Winding was assigned to the wrong component");
    for (auto c : {Config{2, 2, 1, 1}, Config{8, 4, 1, 1}, Config{8, 4, 16, 1}}) {
        State full{c, std::vector<double>(c.count(), 1)};
        expected(full, c.count(), 1, c.count(), 1, c.depth > 1 ? 7 : 3);
        expected(full, 0, 0, 0, 0, 0, 1);
    }
    s = blank();
    cell(s, 2, 2, 0, .9);
    cell(s, 3, 2, 0, .2);
    cell(s, 4, 2, 0, .9);
    expected(s, 3, 1, 3, 0, 0, .1);
    expected(s, 2, 2, 1, 0, 0, .2); // Strict > comparison; the bridge is exactly at threshold.
    auto original = s.values;
    measure_morphology(s);
    check(s.values == original, "Shape analysis changed the field");
    uint32_t rng = 27;
    for (int depth : {1, 4})
        for (int trial = 0; trial < 200; ++trial) {
            auto field = blank(8, 4, depth);
            for (auto &v : field.values) {
                rng = rng * 1664525u + 1013904223u;
                v = double(rng >> 16) / 65535.;
            }
            compare_components(field);
        }
    for (double threshold : {-1., 1.01, std::numeric_limits<double>::quiet_NaN()})
        rejects([&] { measure_morphology(s, threshold); });
    s.values[0] = std::numeric_limits<double>::infinity();
    rejects([&] { measure_morphology(s); });
    s.values.pop_back();
    rejects([&] { measure_morphology(s); });
    check(morphology_csv(std::nullopt) == ",,,,,,,", "Missing measurements were represented as zero");
    check(morphology_csv(Morphology{}) == "0,0,0,,0,0,0,0", "Empty-mask share should be undefined");
    rejects([] { Morphology{1, 2, 1, 0, 0}.validate({8, 8, 1, 1}); });
    rejects([] { Morphology{8, 1, 8, 1, 4}.validate({8, 8, 1, 1}); });
    std::cout << "PASS periodic components: independent oracle, seams vs winding, hollow shells, helical "
                 "loops, thresholds and translation invariance\n";
}
