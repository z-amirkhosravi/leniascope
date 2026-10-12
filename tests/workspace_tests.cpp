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
    legacy.replace(legacy.find("Workspace 2"), 11, "Workspace 1");
    auto old = decode_preferences(legacy);
    check(old.seeds[0].patches == 10 && old.keep_seed_coverage[0] && !old.scale_radius[1],
          "Legacy preference migration");
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
    snapshot.states[0] = std::make_shared<State>(random_state({16, 8, 1, 3}, 17));
    snapshot.states[1] = std::make_shared<State>(random_state({8, 8, 8, 3}, 29));
    {
        WorkspaceWriter writer(directory);
        writer.request(p, snapshot);
        writer.flush();
        check(writer.status().error.empty(), "Initial workspace write failed");
        auto loaded = restore_workspace(directory);
        check(loaded.revision == 1 && loaded.states[0]->values == snapshot.states[0]->values &&
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
    std::cout << "PASS workspace settings, both sessions, checksums, interrupted/corrupt recovery, failed "
                 "writes, shutdown\n";
}
