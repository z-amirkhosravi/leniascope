#include "behavior.h"
#include <algorithm>
#include <chrono>
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
struct Signal {
    double mean = 0, rms = 0, contrast = 0, active = 0;
};
Signal signal(const State &s, double threshold) {
    require(s.values.size() == s.config.count() && !s.values.empty(), "Invalid behavior field size");
    Signal result;
    double square = 0;
    for (double a : s.values) {
        require(std::isfinite(a) && a >= 0 && a <= 1, "Invalid behavior field values");
        result.mean += a;
        square += a * a;
        result.active += a > threshold;
    }
    result.mean /= s.values.size();
    result.rms = std::sqrt(square / s.values.size());
    result.active /= s.values.size();
    // Two passes avoid cancellation for nearly constant fields.
    square = 0;
    for (double a : s.values)
        square += (a - result.mean) * (a - result.mean);
    result.contrast = std::sqrt(square / s.values.size());
    return result;
}
void validate(const BehaviorReport &r) {
    r.config.validate();
    r.options.validate();
    require(r.completed_steps >= 0 && r.completed_steps <= r.options.steps &&
                (r.stopped || r.completed_steps == r.options.steps),
            "Invalid study completion");
    for (const auto *s : {&r.source_id, &r.source_backend, &r.backend})
        require(s->size() <= 1024 && s->find('\0') == std::string::npos, "Invalid study provenance");
    for (double x : {r.initial_time, r.reference_rms, r.initial_rounding_rms, r.wall_seconds})
        require(std::isfinite(x) && x >= 0, "Invalid study metadata");
    require(r.samples.size() >= 1 && r.samples.size() <= 257 && r.samples.front().step == 0 &&
                r.samples.back().step == r.completed_steps,
            "Invalid study samples");
    int previous = -1;
    for (const auto &s : r.samples) {
        require(s.step > previous && s.step <= r.completed_steps &&
                    (s.step == r.completed_steps || s.step % r.options.sample_every == 0),
                "Invalid study sample order");
        for (double x : {s.elapsed, s.mean, s.active_fraction, s.raw_rms, s.aligned_rms, s.relative_rms})
            require(std::isfinite(x) && x >= 0, "Invalid study measurement");
        require(std::abs(s.elapsed - s.step * r.config.dt) <= 1e-10 * std::max(1., s.elapsed) &&
                    s.mean <= 1 + 1e-8 && s.active_fraction <= 1 && s.dx >= -r.config.width / 2 &&
                    s.dx <= r.config.width / 2 && s.dy >= -r.config.height / 2 &&
                    s.dy <= r.config.height / 2 && s.dz >= -r.config.depth / 2 && s.dz <= r.config.depth / 2,
                "Study measurements out of range");
        previous = s.step;
    }
}
void sample_row(std::ostream &out, const BehaviorSample &s, char separator) {
    out << s.step << separator << s.elapsed << separator << s.mean << separator << s.active_fraction
        << separator << s.raw_rms << separator << s.aligned_rms << separator << s.relative_rms << separator
        << s.dx << separator << s.dy << separator << s.dz << separator << s.shift_valid << '\n';
}
} // namespace
void BehaviorOptions::validate() const {
    require(steps >= 1 && steps <= 1000000 && sample_every >= 1 && sample_every <= steps &&
                (steps + sample_every - 1) / sample_every <= 256,
            "Use 1..1,000,000 steps and a sampling interval giving at most 256 samples");
    require(std::isfinite(activity_threshold) && activity_threshold >= 0 && activity_threshold <= 1,
            "Activity threshold must be in [0,1]");
}
BehaviorReport behavior_study(const State &source, Solver &solver, const BehaviorOptions &options,
                              const std::function<bool(int)> &progress) {
    options.validate();
    source.config.validate();
    require(source.steps <= std::numeric_limits<uint64_t>::max() - options.steps,
            "Study would overflow the source step counter");
    auto started = std::chrono::steady_clock::now();
    auto reference = solver.snapshot();
    require(reference.config == source.config && reference.steps == source.steps &&
                reference.time == source.time && reference.seed == source.seed,
            "Study solver must start from the source checkpoint");
    signal(source, options.activity_threshold);
    auto baseline = signal(reference, options.activity_threshold);
    BehaviorReport r;
    r.config = source.config;
    r.options = options;
    r.source_backend = source.backend;
    r.backend = reference.backend;
    r.initial_step = source.steps;
    r.initial_time = source.time;
    r.seed = source.seed;
    r.reference_rms = baseline.rms;
    r.initial_rounding_rms = recurrence_error(source, reference, 0, 0, 0);
    r.samples.push_back({0, 0, baseline.mean, baseline.active});
    auto record = [&] {
        auto state = solver.snapshot();
        require(state.config == reference.config && state.steps == reference.steps + r.completed_steps,
                "Study solver changed parameters or skipped steps");
        auto current = signal(state, options.activity_threshold);
        BehaviorSample s;
        s.step = r.completed_steps;
        s.elapsed = s.step * r.config.dt;
        s.mean = current.mean;
        s.active_fraction = current.active;
        s.raw_rms = recurrence_error(reference, state, 0, 0, 0);
        s.aligned_rms = s.raw_rms;
        if (baseline.contrast > 1e-12 && current.contrast > 1e-12) {
            auto t = find_translation(reference, state);
            s.dx = t.dx;
            s.dy = t.dy;
            s.dz = t.dz;
            s.aligned_rms = t.rms;
            s.shift_valid = t.unique;
        }
        s.relative_rms = baseline.rms > 0 ? s.aligned_rms / baseline.rms : 0;
        r.samples.push_back(s);
    };
    while (r.completed_steps < options.steps) {
        if (progress && !progress(r.completed_steps)) {
            r.stopped = true;
            break;
        }
        solver.step();
        ++r.completed_steps;
        if (r.completed_steps % options.sample_every == 0)
            record();
    }
    if (r.samples.back().step != r.completed_steps)
        record();
    if (progress)
        progress(r.completed_steps);
    r.wall_seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    validate(r);
    return r;
}
std::string encode_behavior(const BehaviorReport &r) {
    validate(r);
    std::ostringstream out;
    out.imbue(std::locale::classic());
    auto &c = r.config;
    out << std::setprecision(17) << "LeniascopeBehavior 1\n"
        << std::quoted(r.source_id) << ' ' << r.source_hash << '\n'
        << std::quoted(r.source_backend) << '\n'
        << std::quoted(r.backend) << '\n'
        << c.width << ' ' << c.height << ' ' << c.depth << ' ' << c.radius << ' ' << c.mu << ' ' << c.sigma
        << ' ' << c.dt << ' ' << uint32_t(c.rule) << ' ' << c.smoothness << '\n'
        << r.initial_step << ' ' << r.initial_time << ' ' << r.seed << '\n'
        << r.options.steps << ' ' << r.options.sample_every << ' ' << r.options.activity_threshold << '\n'
        << r.completed_steps << ' ' << r.stopped << ' ' << r.reference_rms << ' ' << r.initial_rounding_rms
        << ' ' << r.wall_seconds << '\n'
        << r.samples.size() << '\n';
    for (const auto &s : r.samples)
        sample_row(out, s, ' ');
    return out.str();
}
BehaviorReport decode_behavior(const std::string &text) {
    require(text.size() <= 131072, "Study report is too large");
    std::istringstream in(text);
    in.imbue(std::locale::classic());
    BehaviorReport r;
    std::string magic;
    int version = 0;
    uint32_t rule = 0;
    size_t count = 0;
    auto &c = r.config;
    in >> magic >> version >> std::quoted(r.source_id) >> r.source_hash >> std::quoted(r.source_backend) >>
        std::quoted(r.backend) >> c.width >> c.height >> c.depth >> c.radius >> c.mu >> c.sigma >> c.dt >>
        rule >> c.smoothness >> r.initial_step >> r.initial_time >> r.seed >> r.options.steps >>
        r.options.sample_every >> r.options.activity_threshold >> r.completed_steps >> r.stopped >>
        r.reference_rms >> r.initial_rounding_rms >> r.wall_seconds >> count;
    require(bool(in) && magic == "LeniascopeBehavior" && version == 1 && count >= 1 && count <= 257,
            "Invalid study report header");
    c.rule = Rule(rule);
    r.samples.resize(count);
    for (auto &s : r.samples)
        in >> s.step >> s.elapsed >> s.mean >> s.active_fraction >> s.raw_rms >> s.aligned_rms >>
            s.relative_rms >> s.dx >> s.dy >> s.dz >> s.shift_valid;
    require(bool(in), "Truncated study report");
    in >> std::ws;
    require(in.eof(), "Unexpected study data");
    validate(r);
    return r;
}
std::string behavior_csv(const BehaviorReport &r) {
    validate(r);
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::setprecision(17)
        << "step,elapsed,mean,active_fraction,raw_rms,aligned_rms,relative_rms,dx,dy,dz,shift_valid\n";
    for (const auto &s : r.samples) {
        // Leave the normalized metric empty for an empty reference, and displacement empty
        // when the correlation does not identify a unique location (including flat fields).
        out << s.step << ',' << s.elapsed << ',' << s.mean << ',' << s.active_fraction << ',' << s.raw_rms
            << ',' << s.aligned_rms << ',';
        if (r.reference_rms > 0)
            out << s.relative_rms;
        out << ',';
        if (s.shift_valid)
            out << s.dx << ',' << s.dy << ',' << s.dz;
        else
            out << ",,";
        out << ',' << s.shift_valid << '\n';
    }
    return out.str();
}
} // namespace lenia
