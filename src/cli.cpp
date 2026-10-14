#include "batch.h"
#include "behavior.h"
#include "file_integrity.h"
#include "gallery.h"
#include "gpu_solver.h"
#include "numerics.h"
#include "simulation.h"
#include "study_backend.h"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
using namespace lenia;
int main(int argc, char **argv) {
    try {
        if (argc < 2 || std::string(argv[1]) == "--help") {
            std::cout
                << "Leniascope laboratory CLI / checkpoint schema 4\n"
                   "run [--dimensions 2|3] [--size 128] [--radius 26] [--seed 1]\n"
                   "    [--patches 10] [--patch-min 0.6] [--patch-max 1.2]  (sides / radius)\n"
                   "    [--value-min 0] [--value-max 1]\n"
                   "    [--layout uniform|cluster|pair] [--spread 1.5] (cluster radius / pair separation in "
                   "R)\n"
                   "    [--angle 0] [--elevation 0] (pair direction, degrees; pair defaults to 2 patches)\n"
                   "    [--steps 100] [--dt 0.1] [--mu 0.15] [--sigma 0.017]\n"
                   "    [--backend cpu|gpu|cuda] [--rule classic|smooth] [--sharpness 12]\n"
                   "    [--input checkpoint.lenia] [--output checkpoint.lenia]\n"
                   "    [--csv metrics.csv] [--sample-every 10] [--checkpoint-every 0]\n"
                   "    [--sweep-mu 0.13,0.14,0.15]\n"
                   "compare first.lenia second.lenia  (best integer torus translation)\n"
                   "converge checkpoint.lenia [--steps 20] [--levels 4]  (CPU timestep study, CSV stdout)\n"
                   "study checkpoint.lenia --output new-directory [--steps 1000] [--sample-every 20]\n"
                   "    [--backend cpu|gpu|cuda] [--threshold 0.01] (behavior report and CSV; input "
                   "unchanged)\n"
                   "batch checkpoint.lenia --output new-directory --parameter mu|sigma|radius|spread|angle\n"
                   "    --values 0.14,0.15,0.16 [--steps 1000] [--sample-every 100] [--threshold 0.01]\n"
                   "    [--backend cpu|gpu|cuda] [--seeds 3 --first-seed 1]\n"
                   "    --seeds regenerates the saved recipe; otherwise replays the exact saved field.\n"
                   "GPU uses float32; CPU uses float64. Sweep outputs append .run-N.\n";
            return 0;
        }
        std::string command = argv[1];
        if (command == "batch") {
            if (argc < 3)
                throw std::invalid_argument("batch needs a checkpoint path");
            BatchPlan plan;
            std::string output;
            std::map<std::string, bool> seen;
            for (int i = 3; i < argc; i += 2) {
                std::string key = argv[i];
                if (i + 1 >= argc || seen[key])
                    throw std::invalid_argument("Duplicate or incomplete batch option");
                seen[key] = true;
                std::string value = argv[i + 1];
                size_t end = 0;
                if (key == "--output")
                    output = value;
                else if (key == "--parameter")
                    plan.axis = batch_axis(value);
                else if (key == "--values")
                    plan.values = batch_values(value);
                else if (key == "--backend")
                    plan.backend = value;
                else if (key == "--first-seed") {
                    if (value.empty() || value.find_first_not_of("0123456789") != std::string::npos)
                        throw std::invalid_argument("Invalid batch seed");
                    plan.first_seed = std::stoull(value);
                } else if (key == "--threshold") {
                    plan.observation.activity_threshold = std::stod(value, &end);
                    if (end != value.size())
                        throw std::invalid_argument("Invalid batch threshold");
                } else if (key == "--steps" || key == "--sample-every" || key == "--seeds") {
                    int number = std::stoi(value, &end);
                    if (end != value.size())
                        throw std::invalid_argument("Invalid batch integer");
                    if (key == "--steps")
                        plan.observation.steps = number;
                    else if (key == "--sample-every")
                        plan.observation.sample_every = number;
                    else {
                        plan.reseed = true;
                        plan.repetitions = number;
                    }
                } else
                    throw std::invalid_argument("Unknown batch option: " + key);
            }
            if (output.empty() || !seen["--parameter"] || !seen["--values"] ||
                (seen["--first-seed"] && !plan.reseed))
                throw std::invalid_argument(
                    "Specify batch output, parameter and values; first-seed requires seeds");
            auto input = std::filesystem::u8path(argv[2]);
            auto hash = file_checksum(input);
            auto source = load_state(argv[2]);
            if (file_checksum(input) != hash)
                throw std::runtime_error("Batch input changed while loading");
            auto result = run_batch(std::filesystem::u8path(output), source, plan,
                                    input.filename().u8string(), study_solver);
            std::cout << batch_csv(result);
            std::cerr << "Batch " << result.status << ": " << result.runs.size() << '/' << plan.count()
                      << " runs saved in " << output << '\n';
            if (!result.error.empty())
                std::cerr << result.error << '\n';
            return result.status == "complete" ? 0 : 2;
        }
        if (command == "study") {
            if (argc < 3)
                throw std::invalid_argument("study needs a checkpoint path");
            BehaviorOptions settings;
            std::string backend = "cpu", output;
            std::map<std::string, bool> seen;
            for (int i = 3; i < argc; i += 2) {
                std::string key = argv[i];
                if (i + 1 >= argc || seen[key] ||
                    (key != "--steps" && key != "--sample-every" && key != "--backend" &&
                     key != "--threshold" && key != "--output"))
                    throw std::invalid_argument("Unknown, duplicate or incomplete study option");
                seen[key] = true;
                std::string value = argv[i + 1];
                size_t end = 0;
                if (key == "--backend")
                    backend = value;
                else if (key == "--output")
                    output = value;
                else if (key == "--threshold") {
                    settings.activity_threshold = std::stod(value, &end);
                    if (end != value.size())
                        throw std::invalid_argument("Invalid study threshold");
                } else {
                    int number = std::stoi(value, &end);
                    if (end != value.size())
                        throw std::invalid_argument("Invalid study integer");
                    (key == "--steps" ? settings.steps : settings.sample_every) = number;
                }
            }
            settings.validate();
            if (output.empty() || std::filesystem::exists(std::filesystem::u8path(output)))
                throw std::invalid_argument("Specify --output as a new report directory");
            auto input = std::filesystem::u8path(argv[2]);
            auto hash = file_checksum(input);
            auto source = load_state(argv[2]);
            if (file_checksum(input) != hash)
                throw std::runtime_error("Study input changed while loading");
            auto solver = study_solver(source, backend);
            auto report = behavior_study(source, *solver, settings);
            report.source_id = input.filename().u8string();
            report.source_hash = hash;
            save_behavior_files(std::filesystem::u8path(output), report);
            std::cout << behavior_csv(report);
            return 0;
        }
        if (command == "converge") {
            if (argc < 3)
                throw std::invalid_argument("converge needs a checkpoint path");
            int steps = 20, levels = 4;
            std::map<std::string, bool> seen;
            for (int i = 3; i < argc; i += 2) {
                std::string name = argv[i];
                if (i + 1 >= argc || (name != "--steps" && name != "--levels") || seen[name])
                    throw std::invalid_argument("Unknown, duplicate, or incomplete convergence option");
                seen[name] = true;
                std::string text = argv[i + 1];
                size_t end;
                int value = std::stoi(text, &end);
                if (end != text.size())
                    throw std::invalid_argument("Invalid convergence integer");
                (name == "--steps" ? steps : levels) = value;
            }
            auto initial = load_state(argv[2]);
            auto report = timestep_study(initial, steps, levels);
            std::cout << std::setprecision(17) << "dt,steps,duration,rms_to_finest,max_to_finest,mean\n";
            for (const auto &sample : report)
                std::cout << sample.dt << ',' << sample.steps << ',' << sample.duration << ','
                          << sample.rms_to_finest << ',' << sample.max_to_finest << ',' << sample.mean
                          << '\n';
            return 0;
        }
        if (command == "compare") {
            if (argc != 4)
                throw std::invalid_argument("compare needs two checkpoint paths");
            auto a = load_state(argv[2]), b = load_state(argv[3]);
            auto t = find_translation(a, b);
            std::cout << std::setprecision(17) << "period,dx,dy,dz,rms\n"
                      << b.time - a.time << ',' << t.dx << ',' << t.dy << ',' << t.dz << ',' << t.rms << '\n';
            return 0;
        }
        if (command != "run")
            throw std::invalid_argument("Unknown command; use --help");
        std::map<std::string, std::string> options;
        const std::vector<std::string> known = {
            "--dimensions", "--size",      "--radius",       "--seed",
            "--steps",      "--dt",        "--mu",           "--sigma",
            "--backend",    "--rule",      "--sharpness",    "--input",
            "--output",     "--csv",       "--sample-every", "--checkpoint-every",
            "--sweep-mu",   "--patches",   "--patch-min",    "--patch-max",
            "--value-min",  "--value-max", "--layout",       "--spread",
            "--angle",      "--elevation"};
        for (int i = 2; i < argc; i += 2) {
            if (i + 1 >= argc || std::find(known.begin(), known.end(), argv[i]) == known.end())
                throw std::invalid_argument("Unknown or incomplete option: " + std::string(argv[i]));
            if (!options.emplace(argv[i], argv[i + 1]).second)
                throw std::invalid_argument("Duplicate option");
        }
        auto get = [&](const char *key, const char *fallback) {
            auto it = options.find(key);
            return it == options.end() ? std::string(fallback) : it->second;
        };
        auto integer = [&](const char *key, const char *fallback) {
            auto s = get(key, fallback);
            size_t end = 0;
            int n = std::stoi(s, &end);
            if (end != s.size())
                throw std::invalid_argument("Invalid integer");
            return n;
        };
        auto number = [&](const char *key, const char *fallback) {
            auto s = get(key, fallback);
            size_t end = 0;
            double n = std::stod(s, &end);
            if (end != s.size())
                throw std::invalid_argument("Invalid number");
            return n;
        };
        State initial;
        bool from_file = options.count("--input") != 0;
        if (from_file)
            initial = load_state(options.at("--input"));
        SeedSettings seed_settings;
        if (from_file) {
            for (const char *key : {"--seed", "--patches", "--patch-min", "--patch-max", "--value-min",
                                    "--value-max", "--layout", "--spread", "--angle", "--elevation"})
                if (options.count(key))
                    throw std::invalid_argument("Initialization options cannot be used with --input");
        }
        auto layout = get("--layout", "uniform");
        if (layout != "uniform" && layout != "cluster" && layout != "pair")
            throw std::invalid_argument("Layout must be uniform, cluster, or pair");
        seed_settings.layout = layout == "pair"      ? SeedLayout::Pair
                               : layout == "cluster" ? SeedLayout::Cluster
                                                     : SeedLayout::Uniform;
        seed_settings.patches = integer("--patches", layout == "pair" ? "2" : "10");
        seed_settings.spread = number("--spread", "1.5");
        seed_settings.angle = number("--angle", "0");
        seed_settings.elevation = number("--elevation", "0");
        seed_settings.size_min = number("--patch-min", "0.6");
        seed_settings.size_max = number("--patch-max", "1.2");
        seed_settings.value_min = number("--value-min", "0");
        seed_settings.value_max = number("--value-max", "1");
        seed_settings.validate();
        Config c = from_file ? initial.config : Config{};
        int dimensions = integer("--dimensions", c.depth > 1 ? "3" : "2");
        if (dimensions != 2 && dimensions != 3)
            throw std::invalid_argument("Dimensions must be 2 or 3");
        if (from_file && options.count("--dimensions") && dimensions != (c.depth > 1 ? 3 : 2))
            throw std::invalid_argument("Cannot change dimensions of a loaded checkpoint");
        if (options.count("--size")) {
            c.width = c.height = integer("--size", "128");
            c.depth = dimensions == 3 ? c.width : 1;
        } else if (!from_file)
            c.depth = dimensions == 3 ? c.width : 1;
        if (options.count("--radius"))
            c.radius = integer("--radius", "26");
        if (options.count("--mu"))
            c.mu = number("--mu", ".15");
        if (options.count("--sigma"))
            c.sigma = number("--sigma", ".017");
        if (options.count("--dt"))
            c.dt = number("--dt", ".1");
        if (options.count("--sharpness"))
            c.smoothness = number("--sharpness", "12");
        std::string rule = get("--rule", c.rule == Rule::Classic ? "classic" : "smooth");
        if (rule != "classic" && rule != "smooth")
            throw std::invalid_argument("Unknown rule");
        c.rule = rule == "classic" ? Rule::Classic : Rule::SmoothReaction;
        std::string backend = get("--backend", "cpu");
        if (backend != "cpu" && backend != "gpu" && backend != "cuda")
            throw std::invalid_argument("Unknown backend");
        int steps = integer("--steps", "100"), every = integer("--sample-every", "10"),
            checkpoint = integer("--checkpoint-every", "0");
        if (steps < 0 || every < 1 || checkpoint < 0)
            throw std::invalid_argument("Invalid step/sample counts");
        std::string seed_text = get("--seed", "1");
        size_t seed_end = 0;
        uint64_t seed = std::stoull(seed_text, &seed_end);
        if (seed_end != seed_text.size() || seed_text[0] == '-')
            throw std::invalid_argument("Invalid seed");
        std::vector<double> means{c.mu};
        if (options.count("--sweep-mu")) {
            means.clear();
            std::istringstream list(options.at("--sweep-mu"));
            std::string item;
            while (std::getline(list, item, ',')) {
                size_t end = 0;
                double m = std::stod(item, &end);
                if (end != item.size())
                    throw std::invalid_argument("Invalid sweep mean");
                means.push_back(m);
            }
            if (means.empty())
                throw std::invalid_argument("Empty sweep");
        }
        std::cout << std::setprecision(9);
        for (size_t run = 0; run < means.size(); ++run) {
            c.mu = means[run];
            c.validate();
            State s;
            if (from_file) {
                s = initial;
                if (c.width != s.config.width || c.height != s.config.height || c.depth != s.config.depth)
                    throw std::invalid_argument("Cannot resize a loaded checkpoint");
                s.config = c;
            } else
                s = random_state(c, seed, seed_settings);
            std::unique_ptr<Solver> solver;
            if (backend == "gpu" || backend == "cuda")
                solver = std::make_unique<GpuSolver>(s, nullptr, backend == "cuda");
            else
                solver = std::make_unique<CpuSolver>(s);
            auto path = [&](const char *option) {
                auto result = get(option, "");
                if (!result.empty() && means.size() > 1)
                    result += ".run-" + std::to_string(run);
                return result;
            };
            auto output = path("--output");
            auto csv_path = path("--csv");
            if (checkpoint && !output.size())
                throw std::invalid_argument("Periodic checkpoints need --output");
            std::ofstream csv;
            if (!csv_path.empty()) {
                csv.open(std::filesystem::u8path(csv_path));
                if (!csv)
                    throw std::runtime_error("Cannot open CSV");
                csv << std::setprecision(17)
                    << "schema,backend,width,height,depth,radius,mu,sigma,dt,rule,sharpness,seed,step,time,"
                       "mean,min,max,rms_change,compute_ms,forward_ms,multiply_ms,inverse_ms,update_ms,seed_"
                       "radius,seed_"
                       "patches,seed_size_min,seed_size_max,seed_value_min,seed_value_max,seed_layout,"
                       "seed_spread,seed_angle,seed_elevation\n";
            }
            auto record = [&](const State &state, const std::vector<double> *previous) {
                if (!csv.is_open())
                    return;
                auto m = measure(state.values, previous);
                auto t = solver->timings();
                csv << 4 << ',' << backend << ',' << c.width << ',' << c.height << ',' << c.depth << ','
                    << c.radius << ',' << c.mu << ',' << c.sigma << ',' << c.dt << ',' << uint32_t(c.rule)
                    << ',' << c.smoothness << ',' << state.seed << ',' << state.steps << ',' << state.time
                    << ',' << m.mean << ',' << m.minimum << ',' << m.maximum << ',' << m.rms_change << ','
                    << t.total_ms << ',' << t.forward_ms << ',' << t.multiply_ms << ',' << t.inverse_ms << ','
                    << t.update_ms;
                if (state.initialization) {
                    const auto &r = *state.initialization;
                    csv << ',' << state.initialization_radius << ',' << r.patches << ',' << r.size_min << ','
                        << r.size_max << ',' << r.value_min << ',' << r.value_max << ',' << uint32_t(r.layout)
                        << ',' << r.spread << ',' << r.angle << ',' << r.elevation;
                } else
                    csv << ",,,,,,,,,,";
                csv << '\n';
                if (!csv)
                    throw std::runtime_error("CSV write failed");
            };
            record(s, nullptr);
            auto previous_sample = s.values;
            double compute_ms = 0;
            auto start = std::chrono::steady_clock::now();
            for (int i = 1; i <= steps; ++i) {
                solver->step();
                compute_ms += solver->timings().total_ms;
                if (i % every == 0 || i == steps || (checkpoint && i % checkpoint == 0)) {
                    auto next = solver->snapshot();
                    if (i % every == 0 || i == steps) {
                        record(next, &previous_sample);
                        previous_sample = next.values;
                    }
                    if (checkpoint && i % checkpoint == 0)
                        save_state(next, output + ".step-" + std::to_string(next.steps));
                    s = std::move(next);
                }
            }
            if (!output.empty())
                save_state(solver->snapshot(), output);
            double wall = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
            auto m = measure(s.values);
            std::cout << "backend=" << backend << " shape=" << c.width << 'x' << c.height << 'x' << c.depth
                      << " mu=" << c.mu << " steps=" << steps
                      << " mean_compute_ms=" << (steps ? compute_ms / steps : 0)
                      << " wall_steps_per_second=" << (wall > 0 ? steps / wall : 0)
                      << " final_mean=" << m.mean << '\n';
        }
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "Error: " << e.what() << '\n';
        return 1;
    }
}
