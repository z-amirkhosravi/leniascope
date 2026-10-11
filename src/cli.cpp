#include "gpu_solver.h"
#include "simulation.h"
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
            std::cout << "Leniascope laboratory CLI / model schema 2\n"
                         "run [--dimensions 2|3] [--size 128] [--radius 26] [--seed 1]\n"
                         "    [--steps 100] [--dt 0.1] [--mu 0.15] [--sigma 0.017]\n"
                         "    [--backend cpu|gpu] [--rule classic|smooth] [--sharpness 12]\n"
                         "    [--input checkpoint.lenia] [--output checkpoint.lenia]\n"
                         "    [--csv metrics.csv] [--sample-every 10] [--checkpoint-every 0]\n"
                         "    [--sweep-mu 0.13,0.14,0.15]\n"
                         "compare first.lenia second.lenia  (best integer torus translation)\n"
                         "GPU uses float32; CPU uses float64. Sweep outputs append .run-N.\n";
            return 0;
        }
        std::string command = argv[1];
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
            "--dimensions", "--size", "--radius",       "--seed",
            "--steps",      "--dt",   "--mu",           "--sigma",
            "--backend",    "--rule", "--sharpness",    "--input",
            "--output",     "--csv",  "--sample-every", "--checkpoint-every",
            "--sweep-mu"};
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
        if (backend != "cpu" && backend != "gpu")
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
                s = random_state(c, seed);
            std::unique_ptr<Solver> solver;
            if (backend == "gpu")
                solver = std::make_unique<GpuSolver>(s);
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
                       "mean,min,max,rms_change,compute_ms,forward_ms,multiply_ms,inverse_ms,update_ms\n";
            }
            auto record = [&](const State &state, const std::vector<double> *previous) {
                if (!csv.is_open())
                    return;
                auto m = measure(state.values, previous);
                auto t = solver->timings();
                csv << 2 << ',' << backend << ',' << c.width << ',' << c.height << ',' << c.depth << ','
                    << c.radius << ',' << c.mu << ',' << c.sigma << ',' << c.dt << ',' << uint32_t(c.rule)
                    << ',' << c.smoothness << ',' << state.seed << ',' << state.steps << ',' << state.time
                    << ',' << m.mean << ',' << m.minimum << ',' << m.maximum << ',' << m.rms_change << ','
                    << t.total_ms << ',' << t.forward_ms << ',' << t.multiply_ms << ',' << t.inverse_ms << ','
                    << t.update_ms << '\n';
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
