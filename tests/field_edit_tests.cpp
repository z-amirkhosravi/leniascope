#include "src/field_edit.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
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
template <class F> void rejects(F f) {
    bool failed = false;
    try {
        f();
    } catch (const std::exception &) {
        failed = true;
    }
    check(failed, "Editor accepted invalid input");
}
std::string bytes(const std::filesystem::path &p) {
    std::ifstream in(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}
void write(const std::filesystem::path &p, const std::string &s) {
    std::ofstream out(p, std::ios::binary);
    out << s;
}
} // namespace
void field_edit_tests() {
    State source = random_state({8, 8, 1, 2}, 718);
    for (size_t i = 0; i < source.values.size(); ++i)
        source.values[i] = double(i) / 64;
    source.steps = 50;
    source.time = 5;
    auto original = source;
    auto p = copy_patch(source, {7, -1, 3, 2});
    check(p.values == std::vector<double>({63. / 64, 56. / 64, 57. / 64, 7. / 64, 0, 1. / 64}),
          "Toroidal copy differs");
    auto q = rotate_patch(p, 90);
    check(q.width == 2 && q.height == 3 &&
              q.values == std::vector<double>({7. / 64, 63. / 64, 0, 56. / 64, 1. / 64, 57. / 64}),
          "Clockwise quarter turn differs");
    check(rotate_patch(p, -90).values == rotate_patch(p, 270).values, "Negative angle differs");
    check(rotate_patch(p, 180).values ==
              std::vector<double>({1. / 64, 0, 7. / 64, 57. / 64, 56. / 64, 63. / 64}),
          "Half turn differs");
    check(rotate_patch(p, 360).values == p.values, "Full turn changed values");
    auto turns = p;
    for (int i = 0; i < 4; ++i)
        turns = rotate_patch(turns, 90);
    check(turns.values == p.values && turns.coverage == p.coverage && turns.width == p.width,
          "Four turns lost cells");
    auto dest = source;
    std::fill(dest.values.begin(), dest.values.end(), .5);
    place_patch(dest, q, -1, 7, PatchBlend::Replace, false);
    check(dest.values[63] == 7. / 64 && dest.values[56] == 63. / 64 && dest.values[7] == 0 &&
              dest.values[0] == 56. / 64 && dest.values[15] == 1. / 64 && dest.values[8] == 57. / 64 &&
              dest.values[9] == .5,
          "Wrapped placement or copied zero differs");
    Patch2D masked{3, 1, {.1, 0, 0}, {.25, 0, 1}, "mask fixture"};
    place_patch(dest, masked, 2, 2, PatchBlend::Replace, false);
    check(dest.values[18] == .475 && dest.values[19] == .5 && dest.values[20] == 0,
          "Replacement coverage differs");
    place_patch(dest, masked, 2, 3, PatchBlend::Maximum, false);
    check(dest.values[26] == .5 && dest.values[27] == .5 && dest.values[28] == .5, "Maximum destroyed cells");
    // An impulse's bilinear tent at 45 degrees has four equal axial neighbours.
    Patch2D impulse{1, 1, {.8}, {1}, "impulse"};
    auto turn = rotate_patch(impulse, 45);
    double axial = std::pow(1 - std::sqrt(.5), 2);
    check(turn.width == 3 && turn.height == 3 && turn.values[4] == .8 &&
              std::abs(turn.coverage[1] - axial) < 1e-14 && std::abs(turn.values[3] - .8 * axial) < 1e-14 &&
              turn.values[0] == 0,
          "Bilinear rotation does not match analytic impulse");
    turn.validate();
    auto rotated = rotate_patch(p, 17);
    rotated.validate();
    check(rotated.values == rotate_patch(p, 377).values &&
              p.values == copy_patch(source, {7, 7, 3, 2}).values,
          "Rotation changed its source or accumulated error");
    auto patch = random_patch(7, 7, PatchShape::Ellipse, 77, .2, .7);
    check(patch.coverage[0] == 0 && patch.coverage[24] == 1 && patch.values[0] == 0 &&
              patch.values == random_patch(7, 7, PatchShape::Ellipse, 77, .2, .7).values &&
              patch.values != random_patch(7, 7, PatchShape::Ellipse, 78, .2, .7).values,
          "Random patch shape or seed differs");
    for (size_t i = 0; i < patch.values.size(); ++i)
        check(patch.coverage[i] == 0 || (patch.values[i] >= .2 && patch.values[i] <= .7),
              "Random patch value out of range");
    auto square = random_patch(7, 7, PatchShape::Rectangle, 77, .2, .7);
    for (size_t i = 0; i < patch.values.size(); ++i)
        check(square.coverage[i] == 1 && (!patch.coverage[i] || patch.values[i] == square.values[i]),
              "Shape changed random stream");
    FieldDraft draft(source, false);
    check(!draft.changed() && draft.result().seed == 718 && draft.result().steps == 50,
          "Opening draft changed metadata");
    draft.stamp(copy_patch(source, {0, 0, 8, 8}), 0, 0, PatchBlend::Replace);
    check(!draft.changed() && draft.undo_count() == 0, "No-op edit became dirty");
    draft.erase({7, 7, 2, 2}, true);
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x)
            check(draft.state().values[y * 8 + x] ==
                      ((x == 7 || x == 0) && (y == 7 || y == 0) ? source.values[y * 8 + x] : 0),
                  "Isolation changed cells inside selection or retained cells outside");
    auto kept = draft.state().values;
    draft.erase({7, 7, 2, 2});
    check(std::all_of(draft.state().values.begin(), draft.state().values.end(),
                      [](double v) { return v == 0; }),
          "Erase failed");
    draft.undo();
    check(draft.state().values == kept, "Erase undo differs");
    draft.undo();
    check(!draft.changed() && draft.result().values == source.values && draft.result().steps == 50,
          "Undo did not restore source metadata");
    draft.redo();
    check(draft.state().values == kept, "Redo differs");
    draft.stamp(p, 2, 2, PatchBlend::Maximum);
    check(draft.redo_count() == 0, "New edit retained abandoned future");
    auto edited = draft.result();
    check(edited.seed == 0 && edited.steps == 0 && edited.time == 0 && !edited.initialization &&
              edited.initialization_radius == 0 && edited.edits.find("parent=") != std::string::npos &&
              edited.edits.find("step=50") != std::string::npos &&
              edited.edits.find("stamp copy") != std::string::npos && source.values == original.values &&
              source.steps == original.steps,
          "Draft lost provenance or mutated parent");
    CpuSolver evolved(edited);
    evolved.step();
    check(evolved.snapshot().edits == edited.edits, "CPU evolution lost construction history");
    FieldDraft continuation(evolved.snapshot(), false);
    continuation.clear();
    check(continuation.result().edits.find(edited.edits) == 0,
          "Re-editing discarded earlier construction history");
    auto invalid = draft.state().values;
    rejects([&] { draft.erase({0, 0, 9, 1}); });
    rejects(
        [&] { draft.stamp(random_patch(9, 1, PatchShape::Rectangle, 1, 0, 1), 0, 0, PatchBlend::Maximum); });
    check(draft.state().values == invalid, "Rejected edit mutated draft");
    rejects([&] { rotate_patch(p, std::numeric_limits<double>::quiet_NaN()); });
    rejects([&] { random_patch(1, 1, PatchShape::Ellipse, 1, .8, .2); });
    rejects([&] { FieldDraft invalid3d(random_state({8, 8, 8, 2}, 1), false); });
    auto bad_patch = p;
    bad_patch.values[0] = 1.1;
    rejects([&] { place_patch(dest, bad_patch, 0, 0, PatchBlend::Maximum, false); });
    auto float_source = source;
    float_source.values[10] = .1;
    FieldDraft gpu(float_source, true), cpu(float_source, false);
    auto decimal = random_patch(1, 1, PatchShape::Rectangle, 1, .1, .1);
    gpu.stamp(decimal, 0, 0, PatchBlend::Replace);
    cpu.stamp(decimal, 0, 0, PatchBlend::Replace);
    check(gpu.state().values[0] == double(float(.1)) && cpu.state().values[0] == .1 &&
              gpu.state().values[10] == .1,
          "Precision policy changed untouched cells or skipped GPU rounding");
    gpu.undo();
    check(gpu.state().values == float_source.values, "Undo rounded the original field");
    auto long_history = source;
    long_history.edits.assign(16200, 'x');
    FieldDraft full(long_history, false);
    full.clear();
    check(full.result().edits.find("Earlier construction history compacted;") == 0 &&
              full.result().edits.size() < 16384,
          "Long history made a field permanently uneditable");
    rejects([&] {
        long_history.edits.push_back('\0');
        FieldDraft invalid_history(long_history, false);
    });
    FieldDraft bounded(source, false);
    for (int i = 0; i < 40; ++i)
        bounded.stamp(random_patch(1, 1, PatchShape::Rectangle, i, i % 2 ? .8 : .2, i % 2 ? .8 : .2), 0, 0,
                      PatchBlend::Replace);
    check(bounded.undo_count() == 32, "Undo history operation limit failed");
    for (int i = 0; i < 32; ++i)
        bounded.undo();
    check(bounded.undo_count() == 0 && bounded.redo_count() == 32 && bounded.changed(),
          "Bounded undo lost retained journal");
    bool history_full = false;
    for (int i = 0; i < 300 && !history_full; ++i) {
        auto before = bounded.result();
        try {
            bounded.stamp(random_patch(1, 1, PatchShape::Rectangle, i, i % 2 ? .8 : .2, i % 2 ? .8 : .2), 0,
                          0, PatchBlend::Replace);
        } catch (const std::invalid_argument &) {
            history_full = true;
            check(bounded.result().values == before.values && bounded.result().edits == before.edits,
                  "History limit damaged draft");
        }
    }
    check(history_full, "Construction history exceeded size limit");
    FieldDraft reopened(bounded.result(), false);
    reopened.clear();
    check(reopened.result().edits.size() <= 16384, "Compacted history still prevents editing");
    namespace fs = std::filesystem;
    auto path = fs::temp_directory_path() /
                ("lenia-editor-" +
                 std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".lenia");
    struct Cleanup {
        fs::path p;
        ~Cleanup() {
            std::error_code e;
            fs::remove(p, e);
        }
    } cleanup{path};
    save_state(edited, path.u8string());
    auto file = bytes(path);
    auto reloaded = load_state(path.u8string());
    check(file.substr(0, 7) == "LeniaS5" && reloaded.values == edited.values &&
              reloaded.edits == edited.edits && reloaded.steps == 0 && !reloaded.initialization,
          "Edited checkpoint round trip differs");
    auto expect_invalid = [&](std::string corrupt) {
        write(path, corrupt);
        rejects([&] { load_state(path.u8string()); });
    };
    // No seed recipe: history length follows header, backend and recipe-present flag.
    size_t history = 100 + edited.backend.size();
    auto corrupt = file;
    for (int i = 0; i < 4; ++i)
        corrupt[history + i] = char(255);
    expect_invalid(corrupt);
    corrupt = file;
    corrupt[history + 4] = '\0';
    expect_invalid(corrupt);
    expect_invalid(file.substr(0, history + 6));
    write(path, file);
    auto too_long = edited;
    too_long.edits.assign(16385, 'x');
    rejects([&] { save_state(too_long, path.u8string()); });
    check(bytes(path) == file, "Failed checkpoint write damaged saved field");
    save_state(source, path.u8string());
    check(bytes(path).substr(0, 7) == "LeniaS4" && load_state(path.u8string()).edits.empty(),
          "Unedited checkpoint changed format");
    std::cout << "PASS field editor: periodic geometry, exact/continuous rotations, coverage, deterministic "
                 "patches, undo, precision and construction history\n";
}
