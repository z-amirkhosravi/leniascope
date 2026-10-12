#include "field_edit.h"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <locale>
#include <sstream>
#include <stdexcept>

namespace lenia {
namespace {
void require(bool ok, const char *message) {
    if (!ok)
        throw std::invalid_argument(message);
}
int wrap(int x, int n) {
    return (x % n + n) % n;
}
std::ostringstream text() {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::setprecision(17);
    return out;
}
std::string region_text(Region2D r) {
    auto out = text();
    out << r.x << ',' << r.y << ' ' << r.width << 'x' << r.height;
    return out.str();
}
void field_valid(const State &s) {
    s.config.validate();
    require(s.config.depth == 1 && s.values.size() == s.config.count(),
            "Editing requires a complete 2D field");
    for (double v : s.values)
        require(std::isfinite(v) && v >= 0 && v <= 1, "Invalid field values");
}
uint64_t random_bits(uint64_t &s) {
    s += 0x9e3779b97f4a7c15ULL;
    uint64_t z = s;
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    return z ^ (z >> 31);
}
} // namespace
void Region2D::validate(const Config &c) const {
    require(c.depth == 1 && width > 0 && height > 0 && width <= c.width && height <= c.height,
            "Selection must fit within one 2D periodic domain");
}
void Patch2D::validate() const {
    require(width > 0 && height > 0 && width <= 4096 && height <= 4096 &&
                values.size() == size_t(width) * height && coverage.size() == values.size(),
            "Invalid patch size");
    require(source.size() <= 1024 && source.find('\0') == std::string::npos, "Invalid patch provenance");
    for (size_t i = 0; i < values.size(); ++i)
        require(std::isfinite(values[i]) && std::isfinite(coverage[i]) && values[i] >= 0 &&
                    coverage[i] >= 0 && coverage[i] <= 1 && values[i] <= coverage[i],
                "Invalid patch values");
}
uint64_t field_identity(const State &s) {
    field_valid(s);
    uint64_t h = 14695981039346656037ULL;
    auto bytes = [&](const auto &value) {
        auto p = reinterpret_cast<const unsigned char *>(&value);
        for (size_t i = 0; i < sizeof value; ++i)
            h = (h ^ p[i]) * 1099511628211ULL;
    };
    const auto &c = s.config;
    for (int v : {c.width, c.height, c.depth, c.radius})
        bytes(v);
    for (double v : {c.mu, c.sigma, c.dt, c.smoothness})
        bytes(v);
    bytes(uint32_t(c.rule));
    for (double v : s.values)
        bytes(v);
    return h;
}
Patch2D copy_patch(const State &s, Region2D r) {
    field_valid(s);
    r.validate(s.config);
    r.x = wrap(r.x, s.config.width);
    r.y = wrap(r.y, s.config.height);
    Patch2D p;
    p.width = r.width;
    p.height = r.height;
    p.values.resize(size_t(p.width) * p.height);
    p.coverage.assign(p.values.size(), 1);
    for (int y = 0; y < r.height; ++y)
        for (int x = 0; x < r.width; ++x)
            p.values[size_t(y) * r.width + x] =
                s.values[size_t((r.y + y) % s.config.height) * s.config.width + (r.x + x) % s.config.width];
    auto out = text();
    out << "copy field=" << std::hex << field_identity(s) << std::dec << " rect=" << region_text(r)
        << " source_R=" << s.config.radius;
    p.source = out.str();
    return p;
}
Patch2D random_patch(int width, int height, PatchShape shape, uint64_t seed, double minimum, double maximum) {
    require(width > 0 && width <= 2048 && height > 0 && height <= 2048, "Patch sides must be 1..2048 cells");
    require(shape == PatchShape::Rectangle || shape == PatchShape::Ellipse, "Invalid patch shape");
    require(std::isfinite(minimum) && std::isfinite(maximum) && 0 <= minimum && minimum <= maximum &&
                maximum <= 1,
            "Patch values must satisfy 0 <= minimum <= maximum <= 1");
    Patch2D p;
    p.width = width;
    p.height = height;
    p.values.resize(size_t(width) * height);
    p.coverage.resize(p.values.size());
    uint64_t rng = seed;
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) {
            double a = (x + .5 - width * .5) / (width * .5), b = (y + .5 - height * .5) / (height * .5);
            bool inside = shape == PatchShape::Rectangle || a * a + b * b <= 1;
            double value = minimum + (maximum - minimum) * ((random_bits(rng) >> 11) * 0x1.0p-53);
            size_t i = size_t(y) * width + x;
            p.values[i] = inside ? value : 0;
            p.coverage[i] = inside ? 1 : 0;
        }
    auto out = text();
    out << "random-v1 " << (shape == PatchShape::Ellipse ? "ellipse" : "rectangle") << ' ' << width << 'x'
        << height << " seed=" << seed << " values=" << minimum << ".." << maximum;
    p.source = out.str();
    return p;
}
Patch2D rotate_patch(const Patch2D &p, double degrees) {
    p.validate();
    require(std::isfinite(degrees), "Rotation must be finite");
    degrees = std::fmod(degrees, 360.);
    if (degrees < 0)
        degrees += 360;
    int quarter = int(std::round(degrees / 90.));
    bool exact = std::abs(degrees - quarter * 90.) < 1e-10;
    Patch2D out;
    if (exact) {
        quarter %= 4;
        out.width = quarter % 2 ? p.height : p.width;
        out.height = quarter % 2 ? p.width : p.height;
        out.values.resize(p.values.size());
        out.coverage.resize(p.coverage.size());
        for (int y = 0; y < p.height; ++y)
            for (int x = 0; x < p.width; ++x) {
                int a = x, b = y;
                if (quarter == 1) {
                    a = p.height - 1 - y;
                    b = x;
                }
                if (quarter == 2) {
                    a = p.width - 1 - x;
                    b = p.height - 1 - y;
                }
                if (quarter == 3) {
                    a = y;
                    b = p.width - 1 - x;
                }
                size_t i = size_t(y) * p.width + x, j = size_t(b) * out.width + a;
                out.values[j] = p.values[i];
                out.coverage[j] = p.coverage[i];
            }
    } else {
        double theta = degrees * 3.14159265358979323846 / 180, c = std::cos(theta), s = std::sin(theta);
        // Include the full support of bilinear interpolation with zero extension.
        out.width = int(std::ceil(std::abs(c) * (p.width + 1) + std::abs(s) * (p.height + 1)));
        out.height = int(std::ceil(std::abs(s) * (p.width + 1) + std::abs(c) * (p.height + 1)));
        require(out.width <= 4096 && out.height <= 4096, "Rotated patch is too large");
        out.values.resize(size_t(out.width) * out.height);
        out.coverage.resize(out.values.size());
        for (int y = 0; y < out.height; ++y)
            for (int x = 0; x < out.width; ++x) {
                double a = x - (out.width - 1) * .5, b = y - (out.height - 1) * .5;
                double sx = c * a + s * b + (p.width - 1) * .5, sy = -s * a + c * b + (p.height - 1) * .5;
                int ix = int(std::floor(sx)), iy = int(std::floor(sy));
                double fx = sx - ix, fy = sy - iy;
                size_t j = size_t(y) * out.width + x;
                for (int dy = 0; dy < 2; ++dy)
                    for (int dx = 0; dx < 2; ++dx) {
                        int xx = ix + dx, yy = iy + dy;
                        if (xx < 0 || yy < 0 || xx >= p.width || yy >= p.height)
                            continue;
                        double weight = (dx ? fx : 1 - fx) * (dy ? fy : 1 - fy);
                        size_t i = size_t(yy) * p.width + xx;
                        out.values[j] += weight * p.values[i];
                        out.coverage[j] += weight * p.coverage[i];
                    }
                out.coverage[j] = std::clamp(out.coverage[j], 0., 1.);
                out.values[j] = std::clamp(out.values[j], 0., out.coverage[j]);
            }
    }
    auto note = text();
    note << p.source << "; clockwise=" << degrees << (exact ? " exact" : " bilinear-zero-extended");
    out.source = note.str();
    out.validate();
    return out;
}
void place_patch(State &s, const Patch2D &p, int x, int y, PatchBlend blend, bool float32) {
    field_valid(s);
    p.validate();
    require(p.width <= s.config.width && p.height <= s.config.height,
            "Rotated patch is larger than the grid");
    require(blend == PatchBlend::Maximum || blend == PatchBlend::Replace, "Invalid overlap mode");
    x = wrap(x, s.config.width);
    y = wrap(y, s.config.height);
    for (int j = 0; j < p.height; ++j)
        for (int i = 0; i < p.width; ++i) {
            size_t from = size_t(j) * p.width + i;
            if (p.coverage[from] == 0)
                continue;
            auto &a = s.values[size_t((y + j) % s.config.height) * s.config.width + (x + i) % s.config.width];
            double v = blend == PatchBlend::Maximum ? std::max(a, p.values[from])
                                                    : a * (1 - p.coverage[from]) + p.values[from];
            v = std::clamp(v, 0., 1.);
            a = float32 ? double(float(v)) : v;
        }
}
FieldDraft::FieldDraft(State source, bool quantize) : current(std::move(source)), float32(quantize) {
    field_valid(current);
    require(current.edits.size() <= 16384 && current.edits.find('\0') == std::string::npos,
            "Invalid edit history");
    auto out = text();
    if (current.edits.size() > 8192) {
        uint64_t hash = 14695981039346656037ULL;
        for (unsigned char ch : current.edits)
            hash = (hash ^ ch) * 1099511628211ULL;
        out << "Earlier construction history compacted; prior-history-fnv64=" << std::hex << hash << std::dec
            << '\n';
        auto line = current.edits.find('\n', current.edits.size() - 8192);
        if (line != std::string::npos)
            out << current.edits.substr(line + 1) << '\n';
    } else if (!current.edits.empty())
        out << current.edits << '\n';
    out << "Field edit v1; parent=" << std::hex << field_identity(current) << std::dec
        << " grid=" << current.config.width << 'x' << current.config.height << " R=" << current.config.radius
        << " mu=" << current.config.mu << " sigma=" << current.config.sigma << " dt=" << current.config.dt
        << " rule=" << uint32_t(current.config.rule) << " sharpness=" << current.config.smoothness
        << " seed=" << current.seed << " step=" << current.steps << " t=" << current.time
        << " backend=" << current.backend << '\n'
        << "Modified cells: " << (float32 ? "float32" : "float64") << "; applied result starts at t=0.\n";
    prefix = out.str();
}
void FieldDraft::commit(std::vector<double> values, std::string operation) {
    if (values == current.values)
        return;
    auto next = journal + operation + '\n';
    require(
        prefix.size() + next.size() <= 16384,
        "Construction history limit reached; undo, or apply and reopen to continue with compacted history");
    past.push_back({current.values, journal}); // Allocate before changing the draft.
    current.values.swap(values);
    journal.swap(next);
    future.clear();
    size_t limit = std::max<size_t>(
        1, std::min<size_t>(32, (128 * 1024 * 1024) / (current.values.size() * sizeof(double))));
    while (past.size() > limit)
        past.pop_front();
}
void FieldDraft::stamp(const Patch2D &p, int x, int y, PatchBlend blend) {
    auto next = current;
    place_patch(next, p, x, y, blend, float32);
    auto note = text();
    note << "stamp " << p.source << "; top-left=" << wrap(x, current.config.width) << ','
         << wrap(y, current.config.height) << " size=" << p.width << 'x' << p.height
         << " overlap=" << (blend == PatchBlend::Maximum ? "maximum" : "replace");
    commit(std::move(next.values), note.str());
}
void FieldDraft::erase(Region2D r, bool keep_only) {
    r.validate(current.config);
    r.x = wrap(r.x, current.config.width);
    r.y = wrap(r.y, current.config.height);
    auto values = current.values;
    for (int y = 0; y < current.config.height; ++y)
        for (int x = 0; x < current.config.width; ++x) {
            bool inside = wrap(x - r.x, current.config.width) < r.width &&
                          wrap(y - r.y, current.config.height) < r.height;
            if (inside != keep_only)
                values[size_t(y) * current.config.width + x] = 0;
        }
    commit(std::move(values), std::string(keep_only ? "keep only " : "erase ") + region_text(r));
}
void FieldDraft::clear() {
    commit(std::vector<double>(current.values.size(), 0), "clear field");
}
void FieldDraft::undo() {
    if (past.empty())
        return;
    future.push_back({current.values, journal});
    current.values.swap(past.back().values);
    journal.swap(past.back().journal);
    past.pop_back();
}
void FieldDraft::redo() {
    if (future.empty())
        return;
    past.push_back({current.values, journal});
    current.values.swap(future.back().values);
    journal.swap(future.back().journal);
    future.pop_back();
}
State FieldDraft::result() const {
    State result = current;
    if (changed()) {
        result.seed = result.steps = 0;
        result.time = 0;
        result.initialization.reset();
        result.initialization_radius = 0;
        result.edits = prefix + journal;
    }
    return result;
}
} // namespace lenia
