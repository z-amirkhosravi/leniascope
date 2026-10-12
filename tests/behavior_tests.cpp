#include "src/behavior.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace lenia;
namespace {
void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
class ShiftSolver final : public Solver {
    State s;
    bool fade;
    Timings t;

  public:
    ShiftSolver(State input, bool fading = false) : s(std::move(input)), fade(fading) {}
    void step() override {
        auto before = s.values;
        auto &c = s.config;
        for (int z = 0; z < c.depth; ++z)
            for (int y = 0; y < c.height; ++y)
                for (int x = 0; x < c.width; ++x) {
                    size_t to =
                        (size_t((z + 1) % c.depth) * c.height + (y + c.height - 1) % c.height) * c.width +
                        (x + 1) % c.width;
                    s.values[to] = before[(size_t(z) * c.height + y) * c.width + x] * (fade ? .5 : 1);
                }
        ++s.steps;
        s.time += c.dt;
    }
    State snapshot() override {
        return s;
    }
    void parameters(const Config &) override {}
    const Timings &timings() const override {
        return t;
    }
};
State localized(Config c) {
    State s{c, std::vector<double>(c.count()), 7};
    s.values[0] = .5;
    s.values[c.width - 1] = .25;
    s.values[c.width + 2] = 1;
    if (c.depth > 1)
        s.values[c.width * c.height + c.width + 3] = .75;
    return s;
}
} // namespace
void behavior_tests() {
    for (int depth : {1, 4}) {
        auto source = localized({16, 8, depth, 2});
        ShiftSolver solver(source);
        auto report = behavior_study(source, solver, {7, 2, .1});
        check(report.samples.size() == 5 && report.samples.back().step == 7 &&
                  report.samples.back().elapsed == 7 * source.config.dt,
              "Study missed final partial interval");
        for (size_t i = 1; i < report.samples.size(); ++i) {
            const auto &s = report.samples[i];
            int expected_y = (8 - s.step % 8) % 8;
            if (expected_y > 4)
                expected_y -= 8;
            int expected_z = depth > 1 ? s.step % depth : 0;
            if (expected_z > depth / 2)
                expected_z -= depth;
            check(s.shift_valid && s.dx == s.step && s.dy == expected_y && s.dz == expected_z &&
                      s.aligned_rms < 1e-14 && s.relative_rms < 1e-14 && s.raw_rms > 0,
                  "Known periodic translation/sign or recurrence was measured incorrectly");
        }
        check(source.values == localized(source.config).values && source.steps == 0,
              "Study changed its source");
        report.source_id = "A \"quoted\" source";
        report.source_hash = 123456;
        auto text = encode_behavior(report);
        check(encode_behavior(decode_behavior(text)) == text,
              "Study persistence lost measurements/provenance");
        for (auto broken : {text + "junk", text.substr(0, text.size() / 2)}) {
            bool failed = false;
            try {
                decode_behavior(broken);
            } catch (...) {
                failed = true;
            }
            check(failed, "Malformed report accepted");
        }
        ShiftSolver stopped(source);
        auto partial = behavior_study(source, stopped, {20, 4, .1}, [](int step) { return step < 3; });
        check(partial.stopped && partial.completed_steps == 3 && partial.samples.size() == 2 &&
                  partial.samples.back().step == 3,
              "Stop did not preserve the actual endpoint");
        ShiftSolver immediate(source);
        auto zero = behavior_study(source, immediate, {20, 4, .1}, [](int) { return false; });
        check(zero.stopped && zero.completed_steps == 0 && zero.samples.size() == 1,
              "Immediate cancellation stepped");
    }
    auto source = localized({16, 8, 1, 2});
    ShiftSolver fading(source, true);
    auto loss = behavior_study(source, fading, {1, 1, .1});
    check(std::abs(loss.samples.back().relative_rms - .5) < 1e-14, "Relative error obscured amplitude loss");
    auto padded = localized({32, 16, 1, 2});
    ShiftSolver padded_fading(padded, true);
    auto padded_loss = behavior_study(padded, padded_fading, {1, 1, .1});
    check(std::abs(padded_loss.samples.back().relative_rms - .5) < 1e-14,
          "Empty background diluted normalized error");
    source.values.assign(source.values.size(), 0);
    ShiftSolver empty(source);
    auto blank = behavior_study(source, empty, {2, 1, .1});
    check(blank.reference_rms == 0 && !blank.samples.back().shift_valid &&
              behavior_csv(blank).find(",,,,,0") != std::string::npos,
          "Empty field got spurious motion/error");
    source.values.assign(source.values.size(), .3);
    ShiftSolver flat(source);
    check(!behavior_study(source, flat, {2, 1, .1}).samples.back().shift_valid,
          "Flat field got spurious motion");
    source.values.assign(source.values.size(), 0);
    source.values[0] = source.values[8] = 1;
    ShiftSolver repeated(source);
    auto ambiguity = behavior_study(source, repeated, {2, 1, .1});
    check(!ambiguity.samples.back().shift_valid && ambiguity.samples.back().aligned_rms == 0,
          "Repeated identical patterns got a unique displacement");
    auto true_source = random_state({16, 16, 1, 3}, 51);
    CpuSolver observed(true_source), control(true_source);
    auto actual = behavior_study(true_source, observed, {9, 4, .01});
    for (int i = 0; i < 9; ++i)
        control.step();
    check(observed.snapshot().values == control.snapshot().values && actual.initial_rounding_rms == 0,
          "Measurements changed CPU evolution");
    auto exhausted = localized({16, 8, 1, 2});
    exhausted.steps = std::numeric_limits<uint64_t>::max();
    ShiftSolver exhausted_solver(exhausted);
    bool overflow_rejected = false;
    try {
        behavior_study(exhausted, exhausted_solver, {1, 1, .01});
    } catch (const std::invalid_argument &) {
        overflow_rejected = true;
    }
    check(overflow_rejected && exhausted_solver.snapshot().steps == exhausted.steps,
          "Study overflowed the step counter");
    for (auto options : {BehaviorOptions{0, 1, .01}, {1000, 1, .01}, {2, 3, .01}, {2, 1, -1.}}) {
        bool failed = false;
        try {
            options.validate();
        } catch (...) {
            failed = true;
        }
        check(failed, "Invalid/unbounded study settings accepted");
    }
    std::cout << "PASS behavior translation/torus signs, relative mismatch, empty/flat/tied fields, "
                 "cancellation, persistence and unchanged evolution\n";
}
