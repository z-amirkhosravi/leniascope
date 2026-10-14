#include "morphology.h"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <stdexcept>

namespace lenia {
namespace {
void require(bool ok, const char *message) {
    if (!ok)
        throw std::invalid_argument(message);
}
struct Lift {
    int32_t x = std::numeric_limits<int32_t>::min(), y = 0, z = 0;
};
} // namespace
void Morphology::validate(const Config &c) const {
    c.validate();
    require(active_cells <= c.count() && components <= active_cells && largest_cells <= active_cells &&
                winding_components <= components && (winding_axes & ~(c.depth > 1 ? 7u : 3u)) == 0 &&
                ((winding_components == 0) == (winding_axes == 0)),
            "Invalid shape measurements");
    if (active_cells == 0)
        require(largest_cells == 0, "Empty mask has a largest component");
    else
        require(components > 0 && largest_cells > 0 && largest_cells * components >= active_cells &&
                    largest_cells <= active_cells - components + 1,
                "Inconsistent component sizes");
}
namespace {
Morphology analyze(const State &s, double threshold, ComponentMap *map) {
    const auto &c = s.config;
    c.validate();
    require(std::isfinite(threshold) && threshold >= 0 && threshold <= 1, "Shape threshold must be in [0,1]");
    require(s.values.size() == c.count(), "Invalid shape field size");
    Morphology result;
    for (double value : s.values) {
        require(std::isfinite(value) && value >= 0 && value <= 1, "Invalid shape field value");
        result.active_cells += value > threshold;
    }
    if (map)
        map->labels.resize(c.count());
    if (!result.active_cells)
        return result;
    // Track which periodic image each cell occupies along the traversal tree. A conflicting
    // image at an already visited cell closes a path with nonzero winding. Three int32s per
    // grid cell and one uint32 per queued cell bound additional memory to 16 bytes/cell.
    // Image offsets are at most the number of cells (<= 2^24), so int32 cannot overflow.
    std::vector<Lift> lifts(c.count());
    std::vector<uint32_t> queue;
    // Reserve once so vector growth cannot temporarily exceed the traversal-storage bound.
    queue.reserve(size_t(result.active_cells));
    const uint32_t plane = c.width * c.height;
    for (uint32_t start = 0; start < s.values.size(); ++start) {
        if (s.values[start] <= threshold || lifts[start].x != std::numeric_limits<int32_t>::min())
            continue;
        ++result.components;
        uint32_t axes = 0;
        queue.clear();
        queue.push_back(start);
        lifts[start] = {0, 0, 0};
        for (size_t head = 0; head < queue.size(); ++head) {
            uint32_t i = queue[head];
            int x = i % c.width, y = (i / c.width) % c.height, z = i / plane;
            const auto current = lifts[i];
            auto visit = [&](uint32_t neighbor, int wx, int wy, int wz) {
                if (s.values[neighbor] <= threshold)
                    return;
                Lift expected{current.x + wx, current.y + wy, current.z + wz};
                auto &actual = lifts[neighbor];
                if (actual.x == std::numeric_limits<int32_t>::min()) {
                    actual = expected;
                    queue.push_back(neighbor);
                } else {
                    axes |= actual.x != expected.x ? 1u : 0u;
                    axes |= actual.y != expected.y ? 2u : 0u;
                    axes |= actual.z != expected.z ? 4u : 0u;
                }
            };
            visit(x + 1 < c.width ? i + 1 : i - (c.width - 1), x + 1 == c.width, 0, 0);
            visit(x > 0 ? i - 1 : i + c.width - 1, x == 0 ? -1 : 0, 0, 0);
            visit(y + 1 < c.height ? i + c.width : i - (c.height - 1) * c.width, 0, y + 1 == c.height, 0);
            visit(y > 0 ? i - c.width : i + (c.height - 1) * c.width, 0, y == 0 ? -1 : 0, 0);
            if (c.depth > 1) {
                visit(z + 1 < c.depth ? i + plane : i - (c.depth - 1) * plane, 0, 0, z + 1 == c.depth);
                visit(z > 0 ? i - plane : i + (c.depth - 1) * plane, 0, 0, z == 0 ? -1 : 0);
            }
        }
        result.largest_cells = std::max(result.largest_cells, uint64_t(queue.size()));
        result.winding_components += axes != 0;
        result.winding_axes |= axes;
        if (map) {
            map->components.push_back({uint32_t(queue.size()), axes});
            for (auto i : queue)
                map->labels[i] = uint32_t(result.components);
        }
    }
    result.validate(c);
    return result;
}
} // namespace
Morphology measure_morphology(const State &s, double threshold) {
    return analyze(s, threshold, nullptr);
}
ComponentMap label_components(const State &s, double threshold) {
    ComponentMap map;
    map.summary = analyze(s, threshold, &map);
    return map;
}
const char *morphology_csv_header() {
    return "active_cells,components,largest_component_cells,largest_active_fraction,winding_components,"
           "winding_x,winding_y,winding_z";
}
std::string morphology_csv(const std::optional<Morphology> &shape) {
    if (!shape)
        return ",,,,,,,";
    const auto &m = *shape;
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::setprecision(17) << m.active_cells << ',' << m.components << ',' << m.largest_cells << ',';
    if (m.active_cells)
        out << double(m.largest_cells) / m.active_cells;
    out << ',' << m.winding_components << ',' << bool(m.winding_axes & 1) << ',' << bool(m.winding_axes & 2)
        << ',' << bool(m.winding_axes & 4);
    return out.str();
}
std::string winding_axes_text(uint32_t axes) {
    std::string text;
    for (int i = 0; i < 3; ++i)
        if (axes & (1u << i)) {
            if (!text.empty())
                text += ", ";
            text += "XYZ"[i];
        }
    return text.empty() ? "none" : text;
}
} // namespace lenia
