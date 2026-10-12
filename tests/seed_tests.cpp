#include "src/simulation.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace lenia;
namespace {
void require(bool b, const char *message) {
    if (!b)
        throw std::runtime_error(message);
}
double occupied(const State &s) {
    return double(std::count_if(s.values.begin(), s.values.end(), [](double x) { return x > 0; })) /
           s.values.size();
}
} // namespace
void seed_tests() {
    auto original = random_state({16, 8, 4, 2}, 123);
    uint64_t hash = 14695981039346656037ULL;
    for (size_t i = 0; i < original.values.size() * sizeof(double); ++i)
        hash = (hash ^ reinterpret_cast<const unsigned char *>(original.values.data())[i]) * 1099511628211ULL;
    // Frozen from the previous C++ initializer on the supported little-endian IEEE-754 platforms.
    require(hash == 6595798471243091720ULL, "Default seed stream changed");
    SeedSettings custom{23, .8, 1.5, .2, .7};
    auto a = random_state({32, 16, 8, 3}, 77, custom), b = random_state({32, 16, 8, 3}, 77, custom);
    require(a.values == b.values && a.initialization && a.initialization->patches == 23,
            "Custom seed repeatability/metadata");
    for (double value : a.values)
        require(value == 0 || (value >= .2 && value <= .7), "Seed value range");
    auto blank = custom;
    blank.patches = 0;
    require(occupied(random_state({8, 8, 8, 2}, 4, blank)) == 0, "Zero-patch field is not empty");
    auto full = random_state({8, 4, 1, 2}, 15, {1, 4, 4, .75, .75});
    require(std::all_of(full.values.begin(), full.values.end(), [](double x) { return x == .75; }),
            "Full patch did not wrap/clip correctly");
    for (int which = 0; which < 5; ++which) {
        auto bad = custom;
        if (which == 0)
            bad.patches = -1;
        if (which == 1)
            bad.size_min = std::numeric_limits<double>::quiet_NaN();
        if (which == 2)
            bad.size_max = .1;
        if (which == 3)
            bad.value_max = 2;
        if (which == 4) {
            bad.patches = 4096;
            bad.size_min = bad.size_max = 4;
        }
        bool rejected = false;
        try {
            random_state({8, 8, 8, 2}, 1, bad);
        } catch (const std::exception &) {
            rejected = true;
        }
        require(rejected, "Invalid or excessive initialization accepted");
    }
    Config small{128, 128, 128, 26}, large{256, 256, 256, 26};
    auto expanded = resize_seed_settings(small, large, {});
    require(expanded.patches == 80, "3D resize failed to preserve patch density");
    std::cout << "128^3 R26: expected coverage " << 100 * seed_coverage(small, {})
              << "%; 256^3 fixed 10 patches: " << 100 * seed_coverage(large, {}) << "%; adjusted "
              << expanded.patches << " patches: " << 100 * seed_coverage(large, expanded) << "%\n";
    large.radius = 52;
    require(resize_seed_settings(small, large, {}).patches == 10,
            "Radius-scaled resize changed count unexpectedly");
    require(resize_seed_settings({1024, 1024, 1, 104}, {2048, 2048, 1, 104}, {}).patches == 40,
            "2D area scaling is incorrect");
    require(resize_seed_settings(small, large, blank).patches == 0, "Empty resize lost empty recipe");
    // Ensemble check against the analytical overlap estimate; no claim about subsequent survival.
    Config c{32, 32, 32, 6}, d{64, 64, 64, 6};
    auto adjusted = resize_seed_settings(c, d, {});
    double low = 0, sparse = 0, dense = 0;
    for (uint64_t seed = 1; seed <= 64; ++seed) {
        low += occupied(random_state(c, seed));
        sparse += occupied(random_state(d, seed));
        dense += occupied(random_state(d, seed, adjusted));
    }
    low /= 64;
    sparse /= 64;
    dense /= 64;
    require(sparse / low > .11 && sparse / low < .16,
            "Fixed-count dilution differs from expected volume scaling");
    require(dense / low > .88 && dense / low < 1.12,
            "Coverage-preserving resize remains systematically diluted");
    require(std::abs(low - seed_coverage(c, {})) < .006 &&
                std::abs(dense - seed_coverage(d, adjusted)) < .006,
            "Coverage estimate disagrees with sampled initializations");
    std::cout << "PASS initialization controls, default compatibility, periodic boxes, density scaling, "
                 "bounded work and seeded ensemble\n";
}
