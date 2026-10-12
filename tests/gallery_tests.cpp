#include "src/file_integrity.h"
#include "src/gallery.h"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#ifdef _WIN32
#include "src/runner.h"
#endif
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
    check(failed, "Gallery accepted invalid input");
}
std::vector<uint8_t> preview() {
    std::vector<uint8_t> b(54 + 4 * 3 * 4, 255);
    std::fill(b.begin(), b.begin() + 54, 0);
    auto u32 = [&](int pos, uint32_t n) {
        for (int i = 0; i < 4; ++i)
            b[pos + i] = uint8_t(n >> (8 * i));
    };
    b[0] = 'B';
    b[1] = 'M';
    u32(2, uint32_t(b.size()));
    u32(10, 54);
    u32(14, 40);
    u32(18, 4);
    u32(22, uint32_t(-3));
    b[26] = 1;
    b[28] = 32;
    return b;
}
std::string bytes(const std::filesystem::path &p) {
    std::ifstream f(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), {}};
}
void write(const std::filesystem::path &p, const std::string &s) {
    std::ofstream f(p, std::ios::binary);
    f << s;
}
} // namespace
void gallery_tests(bool cpu_only) {
    namespace fs = std::filesystem;
    auto root =
        fs::temp_directory_path() /
        ("lenia-gallery-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    struct Cleanup {
        fs::path p;
        ~Cleanup() {
            std::error_code ec;
            fs::remove_all(p, ec);
        }
    } cleanup{root};
    check(list_gallery(root).entries.empty() && !fs::exists(root), "Reading empty gallery wrote files");
    auto bmp = preview();
    GalleryNotes notes{"Orbium? / candidate", "motion, test", "Quoted \"note\"\nSecond line \\ torus", false,
                       12.5};
    std::vector<GalleryEntry> entries;
    for (auto c : {Config{16, 8, 1, 3}, Config{8, 4, 16, 2}}) {
        SeedSettings recipe{23, .8, 1.5, .2, .7, SeedLayout::Cluster, .75, 0, 0};
        CpuSolver solver(random_state(c, 77, recipe));
        for (int i = 0; i < 3; ++i)
            solver.step();
        auto state = solver.snapshot();
        ViewSettings v;
        v.view = c.depth > 1 ? 1 : 0;
        v.palette = 4;
        v.offset[0] = .25f;
        auto e = save_gallery_entry(root, state, v, notes, bmp);
        auto restored = load_gallery_state(root, e.id);
        check(restored.values == state.values && restored.config == state.config && restored.steps == 3 &&
                  restored.time == state.time && restored.initialization &&
                  restored.initialization->patches == 23 &&
                  restored.initialization->layout == SeedLayout::Cluster &&
                  restored.initialization->spread == .75,
              "Gallery changed checkpoint or recipe");
        auto info = read_gallery_entry(root, e.id);
        check(info.state.values.empty() && info.annotation.notes == notes.notes && info.view.palette == 4 &&
                  info.view.offset[0] == .25f,
              "Gallery browse metadata or view differs");
        auto image = load_gallery_preview(root, e.id);
        check(image.width == 4 && image.height == 3 &&
                  image.bgra == std::vector<uint8_t>(bmp.begin() + 54, bmp.end()),
              "Gallery preview differs");
        auto field = bytes(root / e.id / "state.lenia");
        auto edit = notes;
        edit.reference = true;
        edit.title = "Reference revised";
        annotate_gallery_entry(root, e.id, edit);
        check(read_gallery_entry(root, e.id).annotation.reference &&
                  bytes(root / e.id / "state.lenia") == field,
              "Annotation edit changed field");
        entries.push_back(e);
    }
    check(entries[0].id != entries[1].id && list_gallery(root).entries.size() == 2,
          "Gallery entries overwrite one another");
    fs::create_directories(root / ".writing-interrupted");
    write(root / ".writing-interrupted" / "entry.txt", "partial");
    auto broken = std::string(32, 'a');
    fs::create_directory(root / broken);
    write(root / broken / "entry.txt", "invalid");
    auto catalog = list_gallery(root);
    check(catalog.entries.size() == 2 && catalog.errors.size() == 1,
          "Corrupt/incomplete entry hid valid gallery");
    rejects([&] { read_gallery_entry(root, "../outside"); });
    auto first = root / entries[0].id;
    auto original = bytes(first / "state.lenia"), bad = original;
    bad.back() ^= 1;
    write(first / "state.lenia", bad);
    rejects([&] { load_gallery_state(root, entries[0].id); });
    write(first / "state.lenia", original);
    auto image = bytes(first / "preview.bmp");
    write(first / "preview.bmp", "bad");
    rejects([&] { load_gallery_preview(root, entries[0].id); });
    check(!load_gallery_state(root, entries[0].id).values.empty(), "Missing preview lost usable checkpoint");
    write(first / "preview.bmp", image);
    auto metadata = bytes(first / "entry.txt");
    write(first / "entry.txt", metadata + "junk");
    rejects([&] { load_gallery_state(root, entries[0].id); });
    write(first / "entry.txt", metadata);
    auto invalid = notes;
    invalid.observed_time = std::numeric_limits<double>::quiet_NaN();
    rejects([&] { annotate_gallery_entry(root, entries[0].id, invalid); });
    check(bytes(first / "entry.txt") == metadata, "Failed note edit damaged entry");
    invalid = notes;
    invalid.title.clear();
    rejects([&] { save_gallery_entry(root, random_state({8, 8, 1, 3}, 1), ViewSettings{}, invalid, bmp); });
    auto blocked = root / "not-a-directory";
    write(blocked, "file");
    auto view = ViewSettings{};
    view.view = 0;
    rejects([&] { save_gallery_entry(blocked, random_state({8, 8, 1, 3}, 1), view, notes, bmp); });
    auto constant = random_state({8, 8, 1, 3}, 1);
    std::fill(constant.values.begin(), constant.values.end(), .1);
    auto uniform = save_gallery_entry(root, constant, view, notes, bmp);
    check(load_gallery_state(root, uniform.id).values == constant.values,
          "Uniform-field mean rounding rejected entry");
    auto constructed = constant;
    constructed.edits = "Field edit v1; parent=fixture\nCopy \"organism\"; rotate 17 degrees\n";
    auto construction = save_gallery_entry(root, constructed, view, notes, bmp);
    check(read_gallery_entry(root, construction.id).state.edits == constructed.edits &&
              load_gallery_state(root, construction.id).edits == constructed.edits &&
              bytes(root / construction.id / "entry.txt").find("LeniascopeGallery 3") == 0,
          "Gallery lost edited field provenance");
    annotate_gallery_entry(root, construction.id, {"Edited field", "", "New notes"});
    check(load_gallery_state(root, construction.id).edits == constructed.edits,
          "Gallery annotation lost construction history");
    auto before_study = bytes(root / uniform.id / "entry.txt");
    auto before_field = bytes(root / uniform.id / "state.lenia");
    CpuSolver study_solver(constant);
    auto study = behavior_study(constant, study_solver, {7, 3, .01});
    study.source_id = uniform.id;
    study.source_hash = uniform.field_hash;
    auto saved_study = save_gallery_study(root, uniform.id, study);
    save_gallery_study(root, uniform.id, study);
    auto studies = list_gallery_studies(root, uniform.id);
    check(studies.entries.size() == 2 && studies.errors.empty() &&
              encode_behavior(studies.entries.front().report) == encode_behavior(study) &&
              bytes(root / uniform.id / "entry.txt") == before_study &&
              bytes(root / uniform.id / "state.lenia") == before_field,
          "Behavior study overwrote source or lost its report");
    auto wrong_source = study;
    ++wrong_source.source_hash;
    rejects([&] { save_gallery_study(root, uniform.id, wrong_source); });
    auto report_dir = root / uniform.id / "studies" / saved_study.id;
    rejects([&] { save_behavior_files(report_dir, study); });
    write(report_dir / "samples.csv", "corrupt");
    studies = list_gallery_studies(root, uniform.id);
    check(studies.entries.size() == 1 && studies.errors.size() == 1,
          "Corrupt study hid another valid report");
    // A version-1 gallery points at a version-3 checkpoint. Editing its notes must keep
    // that checkpoint intact, even though newly written metadata uses the new schema.
    auto legacy = save_gallery_entry(root, constant, view, {"Legacy fixture", "", ""}, bmp);
    auto legacy_dir = root / legacy.id;
    auto checkpoint = bytes(legacy_dir / "state.lenia");
    uint32_t backend_length = 0;
    for (int i = 0; i < 4; ++i)
        backend_length |= uint32_t(static_cast<unsigned char>(checkpoint[84 + i])) << (8 * i);
    checkpoint[6] = '3';
    checkpoint.erase(96 + backend_length + 44, 28); // Only the new placement fields.
    write(legacy_dir / "state.lenia", checkpoint);
    std::istringstream modern(bytes(legacy_dir / "entry.txt"));
    std::vector<std::string> rows(10);
    for (auto &row : rows)
        std::getline(modern, row);
    rows[0] = "LeniascopeGallery 1";
    rows[1] = std::to_string(legacy.created) + " " +
              std::to_string(file_checksum(legacy_dir / "state.lenia")) + " " +
              std::to_string(legacy.preview_hash);
    for (int i = 0; i < 4; ++i)
        rows[8].resize(rows[8].find_last_of(' '));
    std::string prefs;
    modern >> std::quoted(prefs);
    for (int i = 0; i < 2; ++i)
        prefs.resize(prefs.find_last_of('\n', prefs.size() - 2) + 1);
    prefs.replace(prefs.find("Workspace 4"), 11, "Workspace 3");
    std::ostringstream old_metadata;
    for (const auto &row : rows)
        old_metadata << row << '\n';
    old_metadata << std::quoted(prefs) << '\n';
    write(legacy_dir / "entry.txt", old_metadata.str());
    check(load_gallery_state(root, legacy.id).values == constant.values &&
              read_gallery_entry(root, legacy.id).state.initialization->layout == SeedLayout::Uniform,
          "Old gallery/checkpoint pair no longer loads");
    annotate_gallery_entry(root, legacy.id, {"Legacy notes edited", "", "Still the same field"});
    check(load_gallery_state(root, legacy.id).values == constant.values &&
              bytes(legacy_dir / "state.lenia") == checkpoint,
          "Editing old gallery metadata changed its checkpoint");
#ifdef _WIN32
    // Exact library loading must not silently narrow a CPU reference to float32.
    Runner runner(!cpu_only);
    auto before = runner.pause_and_snapshot().get();
    runner.restore(constant).get();
    auto after = runner.pause_and_snapshot().get();
    check(!runner.frame()->gpu && !after.volume && after.states[0]->values == constant.values &&
              after.states[1]->values == before.states[1]->values,
          "Exact restore narrowed reference or lost inactive session");
    auto malformed = constant;
    malformed.values.pop_back();
    rejects([&] { runner.restore(malformed).get(); });
    check(runner.capture_current().get().values == constant.values,
          "Failed restore replaced current session");
#endif
    std::cout << "PASS gallery exact fields/provenance/views/previews, annotation edits, independent "
                 "entries, corruption isolation, interrupted writes and validation\n";
}
