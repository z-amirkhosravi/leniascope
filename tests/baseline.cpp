#include "headers/cmap.h"
#include "headers/lengrid.h"
#include "src/simulation.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>

using Clock = std::chrono::steady_clock;
template <class F> double time_ms(F f) {
    const auto start = Clock::now();
    f();
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}
int main() {
    std::cout << std::fixed << std::setprecision(4);
    for (bool volume : {false, true}) {
        std::unique_ptr<LeniaBase> g;
        if (volume)
            g.reset(new LeniaGrid3D(7, 7, 7, 26, .15, .017, .1));
        else
            g.reset(new LeniaGrid(10, 10, 104, .15, .017, .1));
        for (int z = 0; z < g->get_depth(); ++z)
            for (int y = 0; y < g->get_height(); ++y)
                for (int x = 0; x < g->get_width(); ++x)
                    g->set(x, y, z, ((x * 17 + y * 31 + z * 13) % 101) / 100.0);
        const auto &map = cmap::cmap_list.at(L"jet");
        cmap::CMap colors(map.at("red"), map.at("green"), map.at("blue"));
        volatile uint32_t checksum = 0;
        double step = time_ms([&] {
                          for (int n = 0; n < 10; ++n)
                              g->evolve();
                      }) /
                      10;
        int scale = volume ? 4 : 1;
        double draw = time_ms([&] {
                          for (int n = 0; n < 10; ++n)
                              for (int y = 0; y < g->get_height(); ++y)
                                  for (int t = 0; t < scale; ++t)
                                      for (int x = 0; x < g->get_width(); ++x)
                                          for (int u = 0; u < scale; ++u)
                                              checksum = colors(g->get(x, y, volume ? 64 : 0));
                      }) /
                      10;
        std::cout << (volume ? "3D 128^3" : "2D 1024^2") << ",step_ms=" << step << ",color_ms=" << draw
                  << '\n';
    }
    // Independent periodic convolution detects transposed FFT dimensions.
    LeniaGrid g(3, 4, 2, .15, .017, .1);
    const int w = 16, h = 8;
    double initial[w * h], kernel[w * h] = {}, sum = 0;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            initial[y * w + x] = .08 + .12 * ((x * 17 + y * 31) % 101) / 100.;
            g.set(x, y, initial[y * w + x]);
        }
    for (int y = -1; y <= 2; ++y)
        for (int x = -1; x <= 2; ++x) {
            double r = std::sqrt(double(x * x + y * y)) / 2;
            if (r < 1) {
                double k = std::exp(-.5 * std::pow((r - .5) / .15, 2));
                kernel[((y + h) % h) * w + (x + w) % w] = k;
                sum += k;
            }
        }
    g.evolve();
    double error = 0;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            double u = 0;
            for (int j = 0; j < h; ++j)
                for (int i = 0; i < w; ++i)
                    u += kernel[j * w + i] / sum * initial[((y - j + h) % h) * w + (x - i + w) % w];
            double expected = std::clamp(
                initial[y * w + x] + .1 * (2 * std::exp(-.5 * std::pow((u - .15) / .017, 2)) - 1), 0., 1.);
            error = std::max(error, std::abs(expected - g.get(x, y)));
        }
    std::cout << "rectangular_convolution_max_error=" << error << '\n';
    for (bool volume : {false, true}) {
        lenia::Config cfg{16, 16, volume ? 16 : 1, 3};
        std::unique_ptr<LeniaBase> old;
        if (volume)
            old = std::make_unique<LeniaGrid3D>(4, 4, 4, 3, .15, .017, .1);
        else
            old = std::make_unique<LeniaGrid>(4, 4, 3, .15, .017, .1);
        lenia::State state{cfg, std::vector<double>(cfg.count())};
        for (int z = 0; z < cfg.depth; ++z)
            for (int y = 0; y < cfg.height; ++y)
                for (int x = 0; x < cfg.width; ++x) {
                    double a = .08 + .12 * ((x * 17 + y * 31 + z * 13) % 101) / 100.;
                    state.values[(z * cfg.height + y) * cfg.width + x] = a;
                    old->set(x, y, z, a);
                }
        lenia::CpuSolver modern(state);
        double error = 0;
        for (int step = 0; step < 5; ++step) {
            old->evolve();
            modern.step();
        }
        auto result = modern.snapshot();
        for (int z = 0; z < cfg.depth; ++z)
            for (int y = 0; y < cfg.height; ++y)
                for (int x = 0; x < cfg.width; ++x)
                    error = std::max(error, std::abs(old->get(x, y, z) -
                                                     result.values[(z * cfg.height + y) * cfg.width + x]));
        if (error > 1e-12) {
            std::cerr << "FAIL legacy equivalence " << error << '\n';
            return 1;
        }
        std::cout << "PASS legacy " << (volume ? "3D" : "2D")
                  << " five-step equivalence error=" << std::scientific << error << '\n';
    }
}
