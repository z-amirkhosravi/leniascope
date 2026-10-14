#include "src/workspace.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#ifdef _WIN32
#include "src/desktop_workspace.h"
#endif
using namespace lenia;
namespace {
void check(bool value, const char *text) {
    if (!value)
        throw std::runtime_error(text);
}
} // namespace
void workspace_tests() {
    namespace fs = std::filesystem;
    auto directory = fs::temp_directory_path() /
                     ("leniascope-workspace-test-" +
                      std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    struct Cleanup {
        fs::path path;
        ~Cleanup() {
            std::error_code e;
            fs::remove_all(path, e);
        }
    } cleanup{directory};
#ifdef _WIN32
    {
        WorkspaceAccess first(directory), second(directory);
        check(first.writable() && !second.writable(), "Concurrent windows can overwrite one workspace");
    }
    {
        WorkspaceAccess next(directory);
        check(next.writable(), "Workspace lock was not released");
    }
#endif
    Preferences p;
    p.steps_per_second = 123;
    p.gpu = false;
    p.views[0].palette = 3;
    p.views[1].yaw = -3.4f;
    p.views[1].offset[2] = .3f;
    p.maximized = true;
    p.seeds[0] = {23, .8, 1.5, .2, .7};
    p.keep_seed_coverage[0] = false;
    p.scale_radius[1] = true;
    p.cuda = true;
    p.seeds[1] = {2, .6, 1.2, 0, 1, SeedLayout::Pair, 1.75, -30, 20};
    auto encoded = encode_preferences(p);
    check(encode_preferences(decode_preferences(encoded)) == encoded, "Workspace settings round trip");
    for (const auto &bad :
         {std::string("garbage"), encoded.substr(0, encoded.size() / 2), encoded + "garbage"}) {
        bool rejected = false;
        try {
            decode_preferences(bad);
        } catch (...) {
            rejected = true;
        }
        check(rejected, "Malformed workspace settings accepted");
    }
    // Version 1 preferences remain readable; initialization controls receive defaults.
    auto legacy = encoded;
    size_t end = 0;
    for (int i = 0; i < 4; ++i)
        end = legacy.find('\n', end) + 1;
    legacy.resize(end);
    legacy.replace(legacy.find("Workspace 4"), 11, "Workspace 1");
    auto old = decode_preferences(legacy);
    check(old.seeds[0].patches == 10 && old.keep_seed_coverage[0] && !old.scale_radius[1],
          "Legacy preference migration");
    auto v2 = encoded;
    end = 0;
    for (int i = 0; i < 6; ++i)
        end = v2.find('\n', end) + 1;
    v2.resize(end);
    v2.replace(v2.find("Workspace 4"), 11, "Workspace 2");
    auto previous = decode_preferences(v2);
    check(!previous.cuda && previous.seeds[0].patches == 23, "Version 2 preference migration");
    auto v3 = v2 + "1\n";
    v3.replace(v3.find("Workspace 2"), 11, "Workspace 3");
    auto before_layouts = decode_preferences(v3);
    check(before_layouts.cuda && before_layouts.seeds[1].layout == SeedLayout::Uniform &&
              before_layouts.seeds[1].patches == 2,
          "Version 3 preference migration");
    auto invalid = p;
    invalid.views[1].yaw = std::numeric_limits<float>::quiet_NaN();
    bool rejected = false;
    try {
        encode_preferences(invalid);
    } catch (...) {
        rejected = true;
    }
    check(rejected, "NaN workspace settings accepted");
    SessionSnapshot snapshot;
    auto edited = random_state({16, 8, 1, 3}, 17);
    edited.edits = "Field edit v1; recovery fixture\n";
    snapshot.states[0] = std::make_shared<State>(edited);
    snapshot.states[1] = std::make_shared<State>(random_state({8, 8, 8, 3}, 29));
    {
        WorkspaceWriter writer(directory);
        writer.request(p, snapshot);
        writer.flush();
        check(writer.status().error.empty(), "Initial workspace write failed");
        auto loaded = restore_workspace(directory);
        check(loaded.revision == 1 && loaded.states[0]->values == snapshot.states[0]->values &&
                  loaded.states[0]->edits == edited.edits &&
                  loaded.states[1]->values == snapshot.states[1]->values &&
                  encode_preferences(loaded.preferences) == encoded,
              "Workspace sessions round trip");
        auto changed = snapshot;
        changed.states[1] = std::make_shared<State>(random_state({8, 8, 8, 3}, 37));
        writer.request(p, changed);
        writer.flush();
        check(restore_workspace(directory).states[1]->seed == 37, "Newest recovery not selected");
        // Flip a byte that may still describe a valid scalar; checksums must catch silent corruption.
        auto latest = directory / "recovery-b" / "field-3d.lenia";
        std::fstream file(latest, std::ios::binary | std::ios::in | std::ios::out);
        file.seekg(-1, std::ios::end);
        char byte;
        file.get(byte);
        file.seekp(-1, std::ios::end);
        file.put(byte ^ 1);
        file.close();
        loaded = restore_workspace(directory);
        check(loaded.revision == 1 && loaded.states[1]->seed == 29 && !loaded.message.empty(),
              "Damaged newest recovery did not fall back to complete previous sessions");
        auto broken = changed;
        auto bad_state = std::make_shared<State>(*changed.states[1]);
        bad_state->values[0] = -1;
        broken.states[1] = bad_state;
        writer.request(p, broken);
        writer.flush();
        check(!writer.status().error.empty() && restore_workspace(directory).revision == 1,
              "Failed recovery write destroyed the last good recovery");
        writer.request(p, changed);
        writer.flush();
        check(writer.status().error.empty() && restore_workspace(directory).states[1]->seed == 37,
              "Recovery writer did not recover after a failed write");
        auto disabled = p;
        disabled.recovery = false;
        writer.request(disabled);
        writer.flush();
        check(!restore_workspace(directory).states[1], "Disabled recovery restored a field");
    }
    {
        WorkspaceWriter writer(directory);
        writer.request(p, snapshot);
        // Destruction must drain accepted work; re-enabling recovery keeps revision ordering.
    }
    auto restored = restore_workspace(directory);
    check(restored.revision == 3 && restored.states[1]->seed == 29,
          "Writer shutdown/re-enable lost latest state");
    // An interrupted slot update has no commit marker. It must not hide the other complete slot.
    fs::remove(directory / "recovery-a" / "complete.txt");
    check(restore_workspace(directory).revision == 2, "Interrupted recovery did not fall back");
    auto checkpoint = directory / "manual.lenia";
    save_state(*snapshot.states[0], checkpoint.u8string());
    State bad = *snapshot.states[0];
    bad.values[0] = 2;
    try {
        save_state(bad, checkpoint.u8string());
    } catch (...) {
    }
    check(load_state(checkpoint.u8string()).values == snapshot.states[0]->values,
          "Failed manual save damaged the original checkpoint");
    // Persist immutable initial fields separately from each evolving session, including
    // parameter changes. Their corruption must invalidate the entire recovery slot.
    {
        auto location = directory / "initial-fields";
        auto anchored = snapshot;
        anchored.reset_states = snapshot.states;
        for (int i = 0; i < 2; ++i) {
            auto evolved = std::make_shared<State>(*snapshot.states[i]);
            evolved->values[0] = .123;
            evolved->config.radius = 2;
            evolved->steps = 50;
            evolved->time = 5;
            anchored.states[i] = evolved;
        }
        WorkspaceWriter writer(location);
        writer.request(p, anchored);
        writer.flush();
        check(writer.status().error.empty(), "Initial-field recovery write failed");
        auto loaded = restore_workspace(location);
        for (int i = 0; i < 2; ++i)
            check(loaded.states[i]->values == anchored.states[i]->values &&
                      loaded.states[i]->config.radius == 2 && loaded.reset_states[i] &&
                      loaded.reset_states[i]->values == snapshot.states[i]->values &&
                      loaded.reset_states[i]->config.radius == 3,
                  "Recovery lost initial field or confused it with evolved state/parameters");
        writer.request(p, anchored);
        writer.flush();
        fs::remove(location / "recovery-b" / "initial-3d.lenia");
        check(restore_workspace(location).revision == 1,
              "Missing initial field did not invalidate the recovery slot");
        writer.request(p, anchored);
        writer.flush();
        {
            auto path = location / "recovery-b" / "initial-2d.lenia";
            std::fstream f(path, std::ios::binary | std::ios::in | std::ios::out);
            f.seekg(-1, std::ios::end);
            char byte;
            f.get(byte);
            f.seekp(-1, std::ios::end);
            f.put(byte ^ 1);
        }
        check(restore_workspace(location).revision == 1,
              "Corrupt initial field did not invalidate the recovery slot");
        auto mismatch = anchored;
        mismatch.reset_states[0] = std::make_shared<State>(random_state({8, 8, 1, 2}, 1));
        writer.request(p, mismatch);
        writer.flush();
        check(!writer.status().error.empty() && restore_workspace(location).revision == 1,
              "Incompatible initial-field dimensions replaced a valid recovery");
        anchored.reset_states = {};
        for (int i = 0; i < 2; ++i) {
            writer.request(p, anchored);
            writer.flush();
        }
        loaded = restore_workspace(location);
        check(writer.status().error.empty() && !loaded.reset_states[0] && !loaded.reset_states[1] &&
                  !fs::exists(location / "recovery-a/initial-2d.lenia") &&
                  !fs::exists(location / "recovery-b/initial-3d.lenia"),
              "Random initialization retained obsolete initial fields on disk");
    }
    std::cout << "PASS workspace settings, both sessions, checksums, interrupted/corrupt recovery, failed "
                 "writes, shutdown\n";
}
