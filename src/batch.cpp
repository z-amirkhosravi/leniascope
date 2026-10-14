#include "batch.h"
#include "file_integrity.h"
#include "gallery.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <stdexcept>

namespace lenia {
namespace fs = std::filesystem;
namespace {
void require(bool ok, const char *message) {
    if (!ok)
        throw std::invalid_argument(message);
}
std::string read(const fs::path &path, size_t limit = 512 * 1024) {
    require(fs::file_size(path) <= limit, "Batch metadata is too large");
    std::ifstream in(path, std::ios::binary);
    require(bool(in), "Cannot read batch metadata");
    std::ostringstream out;
    out << in.rdbuf();
    require(!in.bad(), "Batch metadata read failed");
    return out.str();
}
void write(const fs::path &path, const std::string &text) {
    auto temporary = path;
    temporary += ".tmp";
    std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
    out << text;
    out.close();
    require(bool(out), "Cannot save batch metadata");
    fs::rename(temporary, path);
}
fs::path run_path(const fs::path &root, int i) {
    require(i >= 0 && i < 256, "Invalid batch run index");
    std::ostringstream name;
    name << "run-" << std::setfill('0') << std::setw(4) << i + 1;
    return root / name.str();
}
void text_valid(const std::string &s, size_t limit) {
    require(s.size() <= limit && s.find('\0') == std::string::npos, "Invalid batch text");
}
Config config_at(const State &s, BatchAxis axis, double value) {
    Config c = s.config;
    if (axis == BatchAxis::Mu)
        c.mu = value;
    else if (axis == BatchAxis::Sigma)
        c.sigma = value;
    else if (axis == BatchAxis::Radius) {
        require(value >= 1 && value <= 1024 && value == std::floor(value), "Kernel radii must be integers");
        c.radius = int(value);
    }
    c.validate();
    return c;
}
SeedSettings recipe_at(const State &s, BatchAxis axis, double value) {
    require(bool(s.initialization), "This field has no recorded seed recipe");
    auto recipe = *s.initialization;
    if (axis == BatchAxis::Spread) {
        require(recipe.layout != SeedLayout::Uniform, "Separation needs a pair or cluster recipe");
        recipe.spread = value;
    } else if (axis == BatchAxis::Angle) {
        require(recipe.layout == SeedLayout::Pair, "Angle needs a pair recipe");
        recipe.angle = value;
    }
    recipe.validate();
    return recipe;
}
std::string encode_plan(const BatchResult &r) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    const auto &p = r.plan;
    out << std::setprecision(17) << "LeniascopeBatch 1\n"
        << std::quoted(r.source_id) << ' ' << r.source_hash << '\n'
        << batch_axis_name(p.axis) << ' ' << p.reseed << ' ' << p.repetitions << ' ' << p.first_seed << ' '
        << p.backend << '\n'
        << p.observation.steps << ' ' << p.observation.sample_every << ' ' << p.observation.activity_threshold
        << '\n'
        << p.values.size();
    for (double value : p.values)
        out << ' ' << value;
    out << '\n';
    return out.str();
}
BatchResult decode_plan(const std::string &text) {
    std::istringstream in(text);
    in.imbue(std::locale::classic());
    BatchResult r;
    std::string magic, axis;
    int version = 0, count = 0;
    auto &p = r.plan;
    in >> magic >> version >> std::quoted(r.source_id) >> r.source_hash >> axis >> p.reseed >>
        p.repetitions >> p.first_seed >> p.backend >> p.observation.steps >> p.observation.sample_every >>
        p.observation.activity_threshold >> count;
    require(bool(in) && magic == "LeniascopeBatch" && version == 1 && count >= 1 && count <= 256,
            "Invalid batch plan");
    p.axis = batch_axis(axis);
    p.values.resize(count);
    for (double &v : p.values)
        in >> v;
    require(bool(in) && (in >> std::ws).eof(), "Invalid batch values or trailing data");
    text_valid(r.source_id, 1024);
    p.validate();
    return r;
}
std::string csv_text(const std::string &text) {
    std::string out = "\"";
    for (char c : text) {
        if (c == '"')
            out += '"';
        out += c;
    }
    return out + '"';
}
} // namespace
const char *batch_axis_name(BatchAxis axis) {
    switch (axis) {
    case BatchAxis::Mu:
        return "mu";
    case BatchAxis::Sigma:
        return "sigma";
    case BatchAxis::Radius:
        return "radius";
    case BatchAxis::Spread:
        return "spread";
    case BatchAxis::Angle:
        return "angle";
    }
    throw std::invalid_argument("Unknown batch parameter");
}
BatchAxis batch_axis(const std::string &name) {
    for (auto axis :
         {BatchAxis::Mu, BatchAxis::Sigma, BatchAxis::Radius, BatchAxis::Spread, BatchAxis::Angle})
        if (name == batch_axis_name(axis))
            return axis;
    throw std::invalid_argument("Batch parameter must be mu, sigma, radius, spread, or angle");
}
std::vector<double> batch_values(const std::string &text) {
    std::istringstream in(text);
    std::string part;
    std::vector<double> values;
    require(!text.empty() && text.back() != ',', "Enter a comma-separated list of values");
    while (std::getline(in, part, ',')) {
        std::istringstream number(part);
        number.imbue(std::locale::classic());
        double value = 0;
        number >> value;
        require(bool(number) && (number >> std::ws).eof() && std::isfinite(value), "Invalid batch value");
        values.push_back(value);
        require(values.size() <= 256, "At most 256 parameter values");
    }
    return values;
}
int BatchPlan::count() const {
    return int(values.size()) * repetitions;
}
void BatchPlan::validate() const {
    batch_axis_name(axis);
    observation.validate();
    require(!values.empty() && values.size() <= 256 && repetitions >= 1 && repetitions <= 256 &&
                count() <= 256,
            "Use 1..256 total batch runs");
    require(backend == "cpu" || backend == "gpu" || backend == "cuda", "Invalid batch backend");
    require(reseed || repetitions == 1, "Repeated seeds require seed-recipe mode");
    require(reseed || (axis != BatchAxis::Spread && axis != BatchAxis::Angle),
            "Separation and angle require seed-recipe mode");
    require(first_seed <= std::numeric_limits<uint64_t>::max() - (repetitions - 1), "Seed range overflows");
    for (size_t i = 0; i < values.size(); ++i) {
        require(std::isfinite(values[i]), "Batch values must be finite");
        require(std::find(values.begin(), values.begin() + i, values[i]) == values.begin() + i,
                "Duplicate batch parameter value");
    }
}
void BatchPlan::validate(const State &s) const {
    validate();
    s.config.validate();
    for (double value : values) {
        config_at(s, axis, value);
        if (reseed)
            recipe_at(s, axis, value);
    }
    require(reseed || s.steps <= std::numeric_limits<uint64_t>::max() - observation.steps,
            "Batch would overflow the source step counter");
}
std::string batch_csv(const BatchResult &r) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::setprecision(17)
        << "run,parameter,value,seed,status,steps,elapsed,mean,active_fraction,relative_mismatch,"
           "dx,dy,dz,shift_valid,initial_rounding_rms,backend,initial_hash,final_hash,label\n";
    for (const auto &run : r.runs) {
        const auto &report = run.report;
        const auto &last = report.samples.back();
        out << run.index + 1 << ',' << batch_axis_name(r.plan.axis) << ','
            << r.plan.values.at(run.index / r.plan.repetitions) << ',' << report.seed << ','
            << (report.stopped ? "partial" : "complete") << ',' << report.completed_steps << ','
            << last.elapsed << ',' << last.mean << ',' << last.active_fraction << ',';
        if (report.reference_rms > 0)
            out << last.relative_rms;
        out << ',';
        if (last.shift_valid)
            out << last.dx << ',' << last.dy << ',' << last.dz;
        else
            out << ",,";
        out << ',' << last.shift_valid << ',' << report.initial_rounding_rms << ','
            << csv_text(report.backend) << ',' << run.initial_hash << ',' << run.final_hash << ','
            << csv_text(run.label) << '\n';
    }
    return out.str();
}
BatchResult run_batch(const fs::path &root, const State &source, const BatchPlan &plan,
                      const std::string &source_id, const BatchSolver &factory,
                      const std::function<bool(int, int)> &progress) {
    plan.validate(source);
    text_valid(source_id, 1024);
    require(bool(factory), "Batch needs a solver factory");
    require(source.values.size() == source.config.count(), "Invalid batch source field size");
    for (double value : source.values)
        require(std::isfinite(value) && value >= 0 && value <= 1, "Invalid batch source values");
    require(!fs::exists(root), "Batch destination already exists");
    if (!root.parent_path().empty())
        fs::create_directories(root.parent_path());
    require(fs::create_directory(root), "Cannot create batch directory");
    BatchResult result;
    result.plan = plan;
    result.source_id = source_id;
    // A missing plan marks an interrupted setup. Individual run directories commit by rename.
    save_state(source, (root / "source.lenia").u8string());
    result.source_hash = file_checksum(root / "source.lenia");
    write(root / "plan.txt", encode_plan(result));
    write(root / "summary.csv", batch_csv(result));
    try {
        for (int i = 0; i < plan.count(); ++i) {
            if (progress && !progress(i, 0)) {
                result.status = "stopped";
                break;
            }
            double value = plan.values[i / plan.repetitions];
            auto config = config_at(source, plan.axis, value);
            State initial = plan.reseed ? random_state(config, plan.first_seed + i % plan.repetitions,
                                                       recipe_at(source, plan.axis, value))
                                        : source;
            initial.config = config;
            auto staging = root / ".run-writing";
            require(fs::create_directory(staging), "Cannot create run staging directory");
            save_state(initial, (staging / "initial.lenia").u8string());
            BatchRun run;
            run.index = i;
            run.initial_hash = file_checksum(staging / "initial.lenia");
            auto solver = factory(initial, plan.backend);
            require(bool(solver), "Batch solver unavailable");
            run.report = behavior_study(initial, *solver, plan.observation,
                                        [&](int step) { return !progress || progress(i, step); });
            run.report.source_id = run_path({}, i).u8string() + "/initial.lenia";
            run.report.source_hash = run.initial_hash;
            save_state(solver->snapshot(), (staging / "final.lenia").u8string());
            run.final_hash = file_checksum(staging / "final.lenia");
            save_behavior_files(staging / "behavior", run.report);
            write(staging / "hashes.txt",
                  std::to_string(run.initial_hash) + " " + std::to_string(run.final_hash) + "\n");
            fs::rename(staging, run_path(root, i));
            result.runs.push_back(std::move(run));
            write(root / "summary.csv", batch_csv(result));
            if (result.runs.back().report.stopped) {
                result.status = "stopped";
                break;
            }
        }
        if (int(result.runs.size()) == plan.count() && !result.runs.back().report.stopped)
            result.status = "complete";
        write(root / (result.status == "complete" ? "complete.txt" : "stopped.txt"), result.status + "\n");
    } catch (const std::exception &e) {
        result.status = "incomplete";
        result.error = e.what();
        // Completed runs remain readable even when a solver or disk write fails later.
        try {
            write(root / "error.txt", result.error);
        } catch (...) {
        }
    }
    return result;
}
BatchResult read_batch(const fs::path &root) {
    auto result = decode_plan(read(root / "plan.txt", 32768));
    for (int i = 0; i < result.plan.count(); ++i) {
        auto path = run_path(root, i);
        if (!fs::exists(path))
            break;
        BatchRun run;
        run.index = i;
        run.report = decode_behavior(read(path / "behavior" / "report.txt"));
        std::istringstream hashes(read(path / "hashes.txt", 128));
        hashes >> run.initial_hash >> run.final_hash;
        require(bool(hashes) && (hashes >> std::ws).eof() && run.report.source_hash == run.initial_hash,
                "Invalid batch field hashes");
        require(run.report.options.steps == result.plan.observation.steps &&
                    run.report.options.sample_every == result.plan.observation.sample_every &&
                    run.report.options.activity_threshold == result.plan.observation.activity_threshold,
                "Batch observation settings disagree");
        require(run.report.source_id == run_path({}, i).u8string() + "/initial.lenia" &&
                    (result.runs.empty() || !result.runs.back().report.stopped),
                "Invalid batch run sequence or provenance");
        double value = result.plan.values[i / result.plan.repetitions];
        require((result.plan.axis != BatchAxis::Mu || run.report.config.mu == value) &&
                    (result.plan.axis != BatchAxis::Sigma || run.report.config.sigma == value) &&
                    (result.plan.axis != BatchAxis::Radius || run.report.config.radius == value),
                "Batch parameter disagrees with report");
        if (result.plan.reseed)
            require(run.report.seed == result.plan.first_seed + i % result.plan.repetitions,
                    "Batch seed disagrees with plan");
        if (fs::exists(path / "label.txt"))
            run.label = read(path / "label.txt", 256);
        result.runs.push_back(std::move(run));
    }
    if (fs::exists(root / "complete.txt")) {
        require(int(result.runs.size()) == result.plan.count() &&
                    std::none_of(result.runs.begin(), result.runs.end(),
                                 [](const auto &r) { return r.report.stopped; }),
                "Incomplete batch marked complete");
        result.status = "complete";
    } else if (fs::exists(root / "stopped.txt"))
        result.status = "stopped";
    if (fs::exists(root / "error.txt"))
        result.error = read(root / "error.txt", 8192);
    return result;
}
State load_batch_field(const fs::path &root, int index, bool final) {
    auto result = read_batch(root);
    require(index >= 0 && index < int(result.runs.size()), "Batch run has no saved endpoint");
    auto &run = result.runs[index];
    auto path = run_path(root, index) / (final ? "final.lenia" : "initial.lenia");
    auto hash = final ? run.final_hash : run.initial_hash;
    require(file_checksum(path) == hash, "Batch field checksum mismatch");
    auto state = load_state(path.u8string());
    require(file_checksum(path) == hash && state.config == run.report.config &&
                state.seed == run.report.seed &&
                state.steps == run.report.initial_step + (final ? run.report.completed_steps : 0),
            "Batch checkpoint provenance mismatch");
    return state;
}
void label_batch_run(const fs::path &root, int index, const std::string &label) {
    text_valid(label, 256);
    auto result = read_batch(root);
    require(index >= 0 && index < int(result.runs.size()), "Invalid batch label index");
    write(run_path(root, index) / "label.txt", label);
    result.runs[index].label = label;
    write(root / "summary.csv", batch_csv(result));
}
} // namespace lenia
