#include "simulation.h"
#include "fftw_lib/fftw3.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <mutex>
#include <stdexcept>

namespace lenia {
namespace {
using Clock = std::chrono::steady_clock;
double ms(Clock::time_point a, Clock::time_point b) {
    return std::chrono::duration<double, std::milli>(b - a).count();
}
std::mutex fftw_mutex; // FFTW plan creation/destruction share global state.
int wrap(int x, int n) {
    return (x % n + n) % n;
}
double gaussian(double x, double m, double s) {
    double t = (x - m) / s;
    return std::exp(-.5 * t * t);
}
double softplus(double x) {
    return std::max(x, 0.) + std::log1p(std::exp(-std::abs(x)));
}
uint64_t next_random(uint64_t &s) {
    s += 0x9e3779b97f4a7c15ULL;
    uint64_t z = s;
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    return z ^ (z >> 31);
}
double uniform(uint64_t &s) {
    return (next_random(s) >> 11) * 0x1.0p-53;
}
void validate_values(const State &s) {
    s.config.validate();
    if (s.initialization) {
        s.initialization->validate();
        Config initial = s.config;
        initial.radius = s.initialization_radius;
        initial.validate();
    }
    if (s.values.size() != s.config.count() || !std::isfinite(s.time) || s.time < 0)
        throw std::runtime_error("Invalid state dimensions or time");
    for (double a : s.values)
        if (!std::isfinite(a) || a < 0 || a > 1)
            throw std::runtime_error("State values must be finite and in [0,1]");
}
template <class T> void write(std::ostream &f, const T &a) {
    f.write(reinterpret_cast<const char *>(&a), sizeof a);
}
template <class T> void read(std::istream &f, T &a) {
    if (!f.read(reinterpret_cast<char *>(&a), sizeof a))
        throw std::runtime_error("Truncated checkpoint");
}
} // namespace
size_t Config::count() const {
    return size_t(width) * size_t(height) * size_t(depth);
}
void Config::validate() const {
    auto power = [](int x) { return x > 0 && x <= 2048 && (x & (x - 1)) == 0; };
    if (!power(width) || !power(height) || !power(depth) || width < 2 || height < 2 ||
        count() > size_t(256) * 256 * 256)
        throw std::invalid_argument("Use power-of-two dimensions, at most 16,777,216 cells");
    int shortest = std::min(width, height);
    if (depth > 1)
        shortest = std::min(shortest, depth);
    if (radius < 1 || radius > shortest / 2)
        throw std::invalid_argument("Kernel radius must be between 1 and half the shortest dimension");
    if (!std::isfinite(mu) || mu < 0 || mu > 1 || !std::isfinite(sigma) || sigma <= 0 || sigma > 1 ||
        !std::isfinite(dt) || dt <= 0 || dt > 1 || !std::isfinite(smoothness) || smoothness < 1 ||
        smoothness > 100 || uint32_t(rule) > 1)
        throw std::invalid_argument("Invalid model parameters");
}
bool Config::operator==(const Config &c) const {
    return width == c.width && height == c.height && depth == c.depth && radius == c.radius && mu == c.mu &&
           sigma == c.sigma && dt == c.dt && smoothness == c.smoothness && rule == c.rule;
}
std::vector<double> make_kernel(const Config &c) {
    c.validate();
    std::vector<double> k(c.count());
    double sum = 0;
    int r = c.radius, zmin = c.depth > 1 ? -r + 1 : 0, zmax = c.depth > 1 ? r : 0;
    for (int z = zmin; z <= zmax; ++z)
        for (int y = -r + 1; y <= r; ++y)
            for (int x = -r + 1; x <= r; ++x) {
                double rho = std::sqrt(double(x * x + y * y + z * z)) / r;
                if (rho >= 1)
                    continue;
                double v = gaussian(rho, .5, .15);
                k[(size_t(wrap(z, c.depth)) * c.height + wrap(y, c.height)) * c.width + wrap(x, c.width)] +=
                    v;
                sum += v;
            }
    if (!(sum > 0))
        throw std::runtime_error("Empty kernel");
    for (double &v : k)
        v /= sum;
    return k;
}
void SeedSettings::validate() const {
    if (patches < 0 || patches > 4096 || !std::isfinite(size_min) || !std::isfinite(size_max) ||
        size_min < .05 || size_max < size_min || size_max > 4 || !std::isfinite(value_min) ||
        !std::isfinite(value_max) || value_min < 0 || value_max > 1 || value_max < value_min)
        throw std::invalid_argument("Seed settings: 0..4096 patches, sides 0.05..4 radii, values in [0,1]");
    if (uint32_t(layout) > 2 || !std::isfinite(spread) || spread < 0 || spread > 16 ||
        !std::isfinite(angle) || angle < -180 || angle > 180 || !std::isfinite(elevation) ||
        elevation < -90 || elevation > 90 || (layout == SeedLayout::Pair && patches != 2))
        throw std::invalid_argument("Invalid seed layout: spread 0..16 radii, angle -180..180, elevation "
                                    "-90..90; pairs need 2 patches");
}
const char *seed_layout_name(SeedLayout layout) {
    switch (layout) {
    case SeedLayout::Uniform:
        return "Across grid";
    case SeedLayout::Cluster:
        return "Single cluster";
    case SeedLayout::Pair:
        return "Isolated pair";
    default:
        return "Unknown";
    }
}
namespace {
double mean_side(int dimension, int radius, const SeedSettings &s) {
    double low = radius * s.size_min, high = radius * s.size_max;
    if (low == high)
        return std::clamp(int(low), 1, dimension);
    // E[L] = sum P(L >= k), including rounding to cells and clipping to each dimension.
    double mean = 1;
    for (int k = 2; k <= dimension; ++k)
        mean += std::clamp((high - k) / (high - low), 0., 1.);
    return mean;
}
double patch_fraction(const Config &c, const SeedSettings &s) {
    c.validate();
    s.validate();
    double cells = mean_side(c.width, c.radius, s) * mean_side(c.height, c.radius, s);
    if (c.depth > 1)
        cells *= mean_side(c.depth, c.radius, s);
    return cells / c.count();
}
} // namespace
double seed_coverage(const Config &c, const SeedSettings &s) {
    if (s.layout != SeedLayout::Uniform)
        throw std::invalid_argument("Coverage estimate applies only to patches placed across the grid");
    double fraction = patch_fraction(c, s);
    if (!s.patches)
        return 0;
    return fraction == 1 ? 1 : -std::expm1(s.patches * std::log1p(-fraction));
}
SeedSettings resize_seed_settings(const Config &from, const Config &to, SeedSettings s) {
    if (s.layout != SeedLayout::Uniform) {
        from.validate();
        to.validate();
        s.validate();
        return s; // Enlarging an isolated experiment adds empty space, not extra patches.
    }
    double old_fraction = patch_fraction(from, s), new_fraction = patch_fraction(to, s);
    if (!s.patches || old_fraction == new_fraction)
        return s;
    if (new_fraction == 1)
        s.patches = 1;
    else {
        if (old_fraction == 1)
            throw std::invalid_argument(
                "Exact full coverage cannot be preserved with smaller random patches");
        double count = s.patches * std::log1p(-old_fraction) / std::log1p(-new_fraction);
        if (count > 4096.49)
            throw std::invalid_argument("Preserving coverage would require more than 4096 patches");
        s.patches = std::max(1, int(std::round(count)));
    }
    return s;
}
State random_state(Config c, uint64_t seed, SeedSettings settings) {
    c.validate();
    settings.validate();
    auto maximum_side = [&](int n) { return std::clamp(int(c.radius * settings.size_max), 1, n); };
    uint64_t maximum_work = uint64_t(settings.patches) * maximum_side(c.width) * maximum_side(c.height);
    if (c.depth > 1)
        maximum_work *= maximum_side(c.depth);
    if (maximum_work > 16 * c.count())
        throw std::invalid_argument("Too many overlapping seed writes; reduce patch count or patch size");
    State s{c, std::vector<double>(c.count()), seed};
    s.initialization = settings;
    s.initialization_radius = c.radius;
    uint64_t rng = seed;
    // Keep the default random stream compatible with the previous ten-patch initializer.
    for (int patch = 0; patch < settings.patches; ++patch) {
        double ux = uniform(rng), uy = uniform(rng), uz = uniform(rng);
        int bx = int(ux * c.width), by = int(uy * c.height), bz = int(uz * c.depth);
        auto side = [&](int n) {
            return std::clamp(
                int(c.radius * (settings.size_min + (settings.size_max - settings.size_min) * uniform(rng))),
                1, n);
        };
        int w = side(c.width), h = side(c.height), d = c.depth > 1 ? side(c.depth) : 1;
        if (settings.layout != SeedLayout::Uniform) {
            constexpr double pi = 3.14159265358979323846;
            double dx, dy, dz = 0;
            if (settings.layout == SeedLayout::Pair) {
                double distance = (patch == 0 ? -.5 : .5) * settings.spread * c.radius;
                double azimuth = settings.angle * pi / 180;
                double tilt = c.depth > 1 ? settings.elevation * pi / 180 : 0;
                dx = distance * std::cos(tilt) * std::cos(azimuth);
                dy = distance * std::cos(tilt) * std::sin(azimuth);
                dz = distance * std::sin(tilt);
            } else {
                // Uniform centres in a disk/ball, not a square/cube. Random draws do not depend
                // on spread, so changing spacing retains the patch sizes and random contents.
                double radius = settings.spread * c.radius * (c.depth > 1 ? std::cbrt(ux) : std::sqrt(ux));
                double z = c.depth > 1 ? 2 * uz - 1 : 0;
                double horizontal = radius * std::sqrt(std::max(0., 1 - z * z));
                dx = horizontal * std::cos(2 * pi * uy);
                dy = horizontal * std::sin(2 * pi * uy);
                dz = radius * z;
            }
            auto origin = [](int n, int side, double offset) {
                return wrap(int(std::lround((n - side) * .5 + offset)), n);
            };
            bx = origin(c.width, w, dx);
            by = origin(c.height, h, dy);
            bz = c.depth > 1 ? origin(c.depth, d, dz) : 0;
        }
        for (int z = 0; z < d; ++z)
            for (int y = 0; y < h; ++y)
                for (int x = 0; x < w; ++x)
                    s.values[(size_t((bz + z) % c.depth) * c.height + (by + y) % c.height) * c.width +
                             (bx + x) % c.width] =
                        settings.value_min + (settings.value_max - settings.value_min) * uniform(rng);
    }
    return s;
}
double advance_value(double a, double u, const Config &c) {
    double g = 2 * gaussian(u, c.mu, c.sigma) - 1;
    if (c.rule == Rule::Classic)
        return std::clamp(a + c.dt * g, 0., 1.);
    // Experimental smooth bounded reaction: da/dt=b(u)(1-a)-d(u)a.
    // Exponential Euler freezes u during this step and preserves [0,1].
    double b = softplus(c.smoothness * g) / c.smoothness, d = softplus(-c.smoothness * g) / c.smoothness;
    double rate = b + d, equilibrium = b / rate;
    return a + (equilibrium - a) * (-std::expm1(-rate * c.dt));
}
Measures measure(const std::vector<double> &v, const std::vector<double> *previous) {
    if (v.empty() || (previous && previous->size() != v.size()))
        throw std::invalid_argument("Measurement size mismatch");
    Measures m;
    m.minimum = 1;
    double delta = 0;
    for (size_t i = 0; i < v.size(); ++i) {
        double a = v[i];
        m.mean += a;
        m.minimum = std::min(m.minimum, a);
        m.maximum = std::max(m.maximum, a);
        if (previous) {
            double d = a - (*previous)[i];
            delta += d * d;
        }
    }
    m.mean /= v.size();
    m.rms_change = std::sqrt(delta / v.size());
    return m;
}
double recurrence_error(const State &a, const State &b, int dx, int dy, int dz) {
    if (a.config.width != b.config.width || a.config.height != b.config.height ||
        a.config.depth != b.config.depth || a.values.size() != a.config.count() ||
        b.values.size() != b.config.count())
        throw std::invalid_argument("Recurrence dimensions differ");
    const auto &c = a.config;
    double sum = 0;
    for (int z = 0; z < c.depth; ++z)
        for (int y = 0; y < c.height; ++y)
            for (int x = 0; x < c.width; ++x) {
                size_t i = (size_t(z) * c.height + y) * c.width + x,
                       j = (size_t(wrap(z + dz, c.depth)) * c.height + wrap(y + dy, c.height)) * c.width +
                           wrap(x + dx, c.width);
                double d = b.values[j] - a.values[i];
                sum += d * d;
            }
    return std::sqrt(sum / a.values.size());
}
Translation find_translation(const State &a, const State &b) {
    if (a.config.width != b.config.width || a.config.height != b.config.height ||
        a.config.depth != b.config.depth)
        throw std::invalid_argument("Translation dimensions differ");
    validate_values(a);
    validate_values(b);
    const auto &c = a.config;
    auto av = a.values, bv = b.values;
    std::vector<double> correlation(c.count());
    size_t nc = size_t(c.depth) * c.height * (c.width / 2 + 1);
    std::lock_guard<std::mutex> lock(fftw_mutex);
    struct Resources {
        fftw_complex *a = nullptr, *b = nullptr;
        fftw_plan pa = nullptr, pb = nullptr, pi = nullptr;
        ~Resources() {
            if (pa)
                fftw_destroy_plan(pa);
            if (pb)
                fftw_destroy_plan(pb);
            if (pi)
                fftw_destroy_plan(pi);
            fftw_free(a);
            fftw_free(b);
        }
    } r;
    r.a = fftw_alloc_complex(nc);
    r.b = fftw_alloc_complex(nc);
    if (!r.a || !r.b)
        throw std::bad_alloc();
    r.pa = fftw_plan_dft_r2c_3d(c.depth, c.height, c.width, av.data(), r.a, FFTW_ESTIMATE);
    r.pb = fftw_plan_dft_r2c_3d(c.depth, c.height, c.width, bv.data(), r.b, FFTW_ESTIMATE);
    r.pi = fftw_plan_dft_c2r_3d(c.depth, c.height, c.width, r.a, correlation.data(), FFTW_ESTIMATE);
    if (!r.pa || !r.pb || !r.pi)
        throw std::runtime_error("Correlation plan failed");
    fftw_execute(r.pa);
    fftw_execute(r.pb);
    for (size_t i = 0; i < nc; ++i) {
        double re = r.a[i][0], im = r.a[i][1];
        r.a[i][0] = re * r.b[i][0] + im * r.b[i][1];
        r.a[i][1] = re * r.b[i][1] - im * r.b[i][0];
    }
    fftw_execute(r.pi);
    size_t best = size_t(std::max_element(correlation.begin(), correlation.end()) - correlation.begin());
    Translation t;
    t.dx = int(best % c.width);
    t.dy = int((best / c.width) % c.height);
    t.dz = int(best / (size_t(c.width) * c.height));
    if (t.dx > c.width / 2)
        t.dx -= c.width;
    if (t.dy > c.height / 2)
        t.dy -= c.height;
    if (t.dz > c.depth / 2)
        t.dz -= c.depth;
    t.rms = recurrence_error(a, b, t.dx, t.dy, t.dz);
    return t;
}
struct CpuSolver::Impl {
    State state;
    std::vector<double> neighborhood;
    fftw_complex *frequency = nullptr, *kernel = nullptr;
    fftw_plan forward = nullptr, backward = nullptr;
    size_t ncomplex = 0;
    Timings timing;
    explicit Impl(State s) : state(std::move(s)), neighborhood(state.values.size()) {
        validate_values(state);
        const auto &c = state.config;
        state.backend = "CPU double / FFTW / core-2";
        ncomplex = size_t(c.depth) * c.height * (c.width / 2 + 1);
        frequency = fftw_alloc_complex(ncomplex);
        kernel = fftw_alloc_complex(ncomplex);
        if (!frequency || !kernel) {
            cleanup();
            throw std::bad_alloc();
        }
        {
            std::lock_guard<std::mutex> lock(fftw_mutex);
            forward = fftw_plan_dft_r2c_3d(c.depth, c.height, c.width, state.values.data(), frequency,
                                           FFTW_ESTIMATE);
            backward = fftw_plan_dft_c2r_3d(c.depth, c.height, c.width, frequency, neighborhood.data(),
                                            FFTW_ESTIMATE);
        }
        if (!forward || !backward) {
            cleanup();
            throw std::runtime_error("FFTW plan failed");
        }
        try {
            rebuild_kernel();
        } catch (...) {
            cleanup();
            throw;
        }
    }
    void cleanup() {
        std::lock_guard<std::mutex> lock(fftw_mutex);
        if (forward)
            fftw_destroy_plan(forward);
        if (backward)
            fftw_destroy_plan(backward);
        fftw_free(frequency);
        fftw_free(kernel);
        forward = backward = nullptr;
        frequency = kernel = nullptr;
    }
    ~Impl() {
        cleanup();
    }
    void rebuild_kernel() {
        auto k = make_kernel(state.config);
        const auto &c = state.config;
        std::lock_guard<std::mutex> lock(fftw_mutex);
        auto p = fftw_plan_dft_r2c_3d(c.depth, c.height, c.width, k.data(), kernel, FFTW_ESTIMATE);
        if (!p)
            throw std::runtime_error("Kernel FFT plan failed");
        fftw_execute(p);
        fftw_destroy_plan(p);
    }
};
CpuSolver::CpuSolver(State s) : impl(std::make_unique<Impl>(std::move(s))) {}
CpuSolver::~CpuSolver() = default;
void CpuSolver::step() {
    auto &p = *impl;
    auto t0 = Clock::now();
    fftw_execute(p.forward);
    auto t1 = Clock::now();
    for (size_t i = 0; i < p.ncomplex; ++i) {
        double re = p.frequency[i][0], im = p.frequency[i][1];
        p.frequency[i][0] = re * p.kernel[i][0] - im * p.kernel[i][1];
        p.frequency[i][1] = re * p.kernel[i][1] + im * p.kernel[i][0];
    }
    auto t2 = Clock::now();
    fftw_execute(p.backward);
    auto t3 = Clock::now();
    double n = double(p.state.values.size());
    for (size_t i = 0; i < p.state.values.size(); ++i)
        p.state.values[i] = advance_value(p.state.values[i], p.neighborhood[i] / n, p.state.config);
    auto t4 = Clock::now();
    p.timing = {ms(t0, t1), ms(t1, t2), ms(t2, t3), ms(t3, t4), ms(t0, t4)};
    ++p.state.steps;
    p.state.time += p.state.config.dt;
}
State CpuSolver::snapshot() {
    return impl->state;
}
const Timings &CpuSolver::timings() const {
    return impl->timing;
}
void CpuSolver::parameters(const Config &c) {
    c.validate();
    auto &old = impl->state.config;
    if (c.width != old.width || c.height != old.height || c.depth != old.depth)
        throw std::invalid_argument("Changing dimensions requires reset");
    bool rebuild = c.radius != old.radius;
    Config previous = old;
    old = c;
    try {
        if (rebuild)
            impl->rebuild_kernel();
    } catch (...) {
        old = previous;
        throw;
    }
}

// Versions 2/3/4 use little endian IEEE-754 doubles. Version 4 adds seed placement controls.
// Version 1 import supports the original 2D Leniav1 format.
void save_state(const State &s, const std::string &filename) {
    validate_values(s);
    if (s.backend.size() > 256)
        throw std::runtime_error("Backend name too long");
    auto destination = std::filesystem::u8path(filename);
    auto temporary = destination;
    temporary += ".writing-" + std::to_string(Clock::now().time_since_epoch().count());
    struct RemoveTemporary {
        std::filesystem::path path;
        ~RemoveTemporary() {
            std::error_code error;
            std::filesystem::remove(path, error);
        }
    } cleanup{temporary};
    std::ofstream f(temporary, std::ios::binary | std::ios::trunc);
    if (!f)
        throw std::runtime_error("Cannot open checkpoint for writing");
    f.write("LeniaS4\0", 8);
    const auto &c = s.config;
    for (int v : {c.width, c.height, c.depth, c.radius})
        write(f, int32_t(v));
    for (double v : {c.mu, c.sigma, c.dt, c.smoothness})
        write(f, v);
    write(f, uint32_t(c.rule));
    write(f, s.seed);
    write(f, s.steps);
    write(f, s.time);
    uint32_t len = uint32_t(s.backend.size());
    if (len > 256)
        throw std::runtime_error("Backend name too long");
    write(f, len);
    f.write(s.backend.data(), len);
    write(f, uint64_t(s.values.size()));
    write(f, uint32_t(bool(s.initialization)));
    if (s.initialization) {
        const auto &r = *s.initialization;
        write(f, int32_t(r.patches));
        write(f, int32_t(s.initialization_radius));
        for (double v : {r.size_min, r.size_max, r.value_min, r.value_max})
            write(f, v);
        write(f, uint32_t(r.layout));
        for (double v : {r.spread, r.angle, r.elevation})
            write(f, v);
    }
    f.write(reinterpret_cast<const char *>(s.values.data()), s.values.size() * sizeof(double));
    if (!f)
        throw std::runtime_error("Checkpoint write failed");
    f.close();
    if (!f)
        throw std::runtime_error("Checkpoint close failed");
    std::filesystem::rename(temporary, destination);
}
State load_state(const std::string &filename) {
    std::ifstream f(std::filesystem::u8path(filename), std::ios::binary);
    if (!f)
        throw std::runtime_error("Cannot open checkpoint");
    std::array<char, 8> signature{};
    if (!f.read(signature.data(), 8))
        throw std::runtime_error("Truncated checkpoint");
    State s;
    if (std::memcmp(signature.data(), "Leniav1\0", 8) == 0) {
        int32_t h, w, r;
        read(f, h);
        read(f, w);
        read(f, r);
        if (h < 1 || w < 1 || h > 11 || w > 11)
            throw std::runtime_error("Invalid legacy dimensions");
        s.config = {1 << w, 1 << h, 1, r};
        read(f, s.config.mu);
        read(f, s.config.sigma);
        read(f, s.config.dt);
        s.seed = 0;
        s.backend = "Legacy v1 import";
    } else if (std::memcmp(signature.data(), "LeniaS2\0", 8) == 0 ||
               std::memcmp(signature.data(), "LeniaS3\0", 8) == 0 ||
               std::memcmp(signature.data(), "LeniaS4\0", 8) == 0) {
        int32_t dims[4];
        for (auto &v : dims)
            read(f, v);
        s.config.width = dims[0];
        s.config.height = dims[1];
        s.config.depth = dims[2];
        s.config.radius = dims[3];
        read(f, s.config.mu);
        read(f, s.config.sigma);
        read(f, s.config.dt);
        read(f, s.config.smoothness);
        uint32_t rule;
        read(f, rule);
        s.config.rule = Rule(rule);
        read(f, s.seed);
        read(f, s.steps);
        read(f, s.time);
        uint32_t len;
        read(f, len);
        if (len > 256)
            throw std::runtime_error("Invalid metadata length");
        s.backend.resize(len);
        if (!f.read(s.backend.data(), len))
            throw std::runtime_error("Truncated metadata");
        uint64_t n;
        read(f, n);
        s.config.validate();
        if (n != s.config.count())
            throw std::runtime_error("Checkpoint cell count mismatch");
        if (signature[6] >= '3') {
            uint32_t present;
            read(f, present);
            if (present > 1)
                throw std::runtime_error("Invalid seed recipe flag");
            if (present) {
                SeedSettings r;
                int32_t patches;
                read(f, patches);
                r.patches = patches;
                int32_t radius;
                read(f, radius);
                s.initialization_radius = radius;
                read(f, r.size_min);
                read(f, r.size_max);
                read(f, r.value_min);
                read(f, r.value_max);
                if (signature[6] >= '4') {
                    uint32_t layout;
                    read(f, layout);
                    r.layout = SeedLayout(layout);
                    read(f, r.spread);
                    read(f, r.angle);
                    read(f, r.elevation);
                }
                r.validate();
                s.initialization = r;
            }
        }
    } else
        throw std::runtime_error("Unknown checkpoint format");
    s.config.validate();
    auto pos = f.tellg();
    f.seekg(0, std::ios::end);
    auto remaining = f.tellg() - pos;
    if (remaining != std::streamoff(s.config.count() * sizeof(double)))
        throw std::runtime_error("Checkpoint payload size mismatch (legacy 3D files are unsupported)");
    f.seekg(pos);
    s.values.resize(s.config.count());
    if (!f.read(reinterpret_cast<char *>(s.values.data()), s.values.size() * sizeof(double)))
        throw std::runtime_error("Truncated state");
    validate_values(s);
    return s;
}
} // namespace lenia
