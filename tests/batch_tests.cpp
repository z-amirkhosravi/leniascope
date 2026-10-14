#include "src/batch.h"
#include "src/file_integrity.h"
#include "src/study_backend.h"
#include <chrono>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace lenia;
namespace {
void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
void rejects(const std::function<void()> &f) {
    bool failed = false;
    try {
        f();
    } catch (const std::exception &) {
        failed = true;
    }
    check(failed, "Batch accepted invalid input");
}
} // namespace
void batch_tests(bool cpu_only) {
    namespace fs = std::filesystem;
    auto root =
        fs::temp_directory_path() /
        ("lenia-batch-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    struct Cleanup {
        fs::path path;
        ~Cleanup() {
            std::error_code error;
            fs::remove_all(path, error);
        }
    } cleanup{root};
    SeedSettings recipe;
    recipe.layout = SeedLayout::Pair;
    recipe.patches = 2;
    recipe.spread = 1;
    auto source = random_state({16, 16, 1, 3}, 481, recipe);
    source.steps = 19;
    source.time = 1.9;
    auto original = source.values;
    BatchPlan plan;
    plan.values = {.14, .16};
    plan.observation = {7, 3, .01};
    auto result = run_batch(root / "replay", source, plan, "saved \"Orbium\"", study_solver);
    check(result.status == "complete" && result.runs.size() == 2 && source.values == original &&
              source.steps == 19,
          "Batch changed its input or did not complete");
    check(batch_csv(read_batch(root / "replay")) == batch_csv(result), "Batch roundtrip lost results");
    for (int i = 0; i < 2; ++i) {
        auto initial = load_batch_field(root / "replay", i, false);
        auto final = load_batch_field(root / "replay", i, true);
        check(initial.values == original && initial.config.mu == plan.values[i] && initial.steps == 19,
              "Replay did not use the same saved field");
        CpuSolver control(initial);
        for (int step = 0; step < 7; ++step)
            control.step();
        check(final.values == control.snapshot().values && final.steps == 26,
              "Batch differs from independent evolution");
    }
    label_batch_run(root / "replay", 0, "transient, \"twin\"\nuncertain");
    check(read_batch(root / "replay").runs[0].label == "transient, \"twin\"\nuncertain" &&
              batch_csv(read_batch(root / "replay")).find("\"transient, \"\"twin\"\"\nuncertain\"") !=
                  std::string::npos,
          "Outcome label / CSV escaping lost");
    label_batch_run(root / "replay", 0, "tubes"); // Replace an existing annotation on Windows as well.
    check(load_batch_field(root / "replay", 0, false).values == original, "Label changed starting field");
    rejects([&] { run_batch(root / "replay", source, plan, "", study_solver); });
    rejects([&] { load_batch_field(root / "replay", -1, false); });
    auto partial = run_batch(root / "partial", source, plan, "", study_solver,
                             [](int run, int step) { return run == 0 || step < 2; });
    check(partial.status == "stopped" && partial.runs.size() == 2 && !partial.runs[0].report.stopped &&
              partial.runs[1].report.completed_steps == 2 && partial.runs[1].report.stopped &&
              load_batch_field(root / "partial", 1, true).steps == 21,
          "Stopping lost a completed run or partial endpoint");
    auto immediate =
        run_batch(root / "immediate", source, plan, "", study_solver, [](int, int) { return false; });
    check(immediate.status == "stopped" && read_batch(root / "immediate").runs.empty(),
          "Immediate cancellation started a run");
    int launches = 0;
    auto failed =
        run_batch(root / "failed", source, plan, "", [&](const State &s, const std::string &backend) {
            if (++launches == 2)
                throw std::runtime_error("Injected solver failure");
            return study_solver(s, backend);
        });
    check(failed.status == "incomplete" && failed.runs.size() == 1 &&
              read_batch(root / "failed").error == "Injected solver failure" &&
              load_batch_field(root / "failed", 0, true).steps == 26,
          "Solver failure discarded a committed run or claimed success");
    // An interrupted process never leaves a false completion marker.
    fs::remove(root / "replay" / "complete.txt");
    check(read_batch(root / "replay").status == "incomplete", "Interrupted batch marked complete");
    std::ofstream(root / "replay" / "run-0001" / "final.lenia", std::ios::binary | std::ios::app) << 'X';
    rejects([&] { load_batch_field(root / "replay", 0, true); });
    plan.reseed = true;
    plan.repetitions = 2;
    plan.first_seed = 216;
    auto seeded = run_batch(root / "seeds", source, plan, "", study_solver);
    check(seeded.status == "complete" && seeded.runs.size() == 4, "Repeated seed batch failed");
    for (int i = 0; i < 4; ++i) {
        auto initial = load_batch_field(root / "seeds", i, false);
        auto expected = random_state(initial.config, 216 + i % 2, recipe);
        check(initial.steps == 0 && initial.time == 0 && initial.seed == 216 + i % 2 &&
                  initial.values == expected.values,
              "Seed recipe was not replayed independently");
        check(initial.values == load_batch_field(root / "seeds", i % 2, false).values,
              "Seed sequence was not paired across parameter values");
    }
    plan.axis = BatchAxis::Spread;
    plan.values = {.75, 2.};
    auto pairs = run_batch(root / "pairs", source, plan, "", study_solver);
    check(pairs.status == "complete", "Pair separation batch failed");
    for (int i = 0; i < 4; ++i) {
        auto initial = load_batch_field(root / "pairs", i, false);
        auto expected_recipe = recipe;
        expected_recipe.spread = plan.values[i / 2];
        check(initial.values == random_state(initial.config, 216 + i % 2, expected_recipe).values,
              "Pair sweep did not retain actual starting fields");
    }
    auto invalid = plan;
    invalid.first_seed = std::numeric_limits<uint64_t>::max();
    rejects([&] { invalid.validate(source); });
    invalid = plan;
    invalid.reseed = false;
    invalid.repetitions = 1;
    rejects([&] { invalid.validate(source); });
    invalid = plan;
    invalid.axis = BatchAxis::Radius;
    invalid.values = {2.5};
    rejects([&] { invalid.validate(source); });
    invalid.values = {9};
    rejects([&] { invalid.validate(source); });
    for (const auto &text : {"1,", "nan", "1junk", "", "1,,2"})
        rejects([&] { batch_values(text); });
    check(batch_values(" 1, 2.5 , 3e-1 ") == std::vector<double>({1, 2.5, .3}), "Batch number parsing");
    invalid = plan;
    invalid.values = {1, 1};
    rejects([&] { invalid.validate(); });
    invalid = plan;
    invalid.repetitions = 200;
    rejects([&] { run_batch(root / "invalid", source, invalid, "", study_solver); });
    check(!fs::exists(root / "invalid"), "Invalid plan wrote output");
#ifdef _WIN32
    if (!cpu_only) {
        for (const std::string backend : {"gpu", "cuda"}) {
#ifndef LENIA_ENABLE_CUDA
            if (backend == "cuda")
                continue;
#endif
            plan.backend = backend;
            plan.repetitions = 1;
            plan.values = {1.};
            auto actual = run_batch(root / backend, source, plan, "", study_solver);
            check(actual.status == "complete", "GPU batch failed");
            auto initial = load_batch_field(root / backend, 0, false);
            auto control = study_solver(initial, backend);
            for (int step = 0; step < plan.observation.steps; ++step)
                control->step();
            check(load_batch_field(root / backend, 0, true).values == control->snapshot().values,
                  "GPU batch differs from independent GPU run");
            std::cout << "PASS batch " << backend << " exact replay\n";
        }
    }
#endif
    std::cout << "PASS batch fixed fields, paired seeds, separation, cancellation, failure recovery, labels "
                 "and integrity\n";
}
