#pragma once
#include "simulation.h"
#include "view_settings.h"
#include <array>
#include <condition_variable>
#include <filesystem>
#include <mutex>
#include <optional>
#include <thread>

namespace lenia {
using SessionStates = std::array<std::shared_ptr<const State>, 2>;
struct SessionSnapshot {
    SessionStates states;
    bool volume = true;
};
struct Preferences {
    std::array<ViewSettings, 2> views;
    std::array<SeedSettings, 2> seeds;
    std::array<bool, 2> keep_seed_coverage{true, true}, scale_radius{false, false};
    double steps_per_second = 60;
    int window_width = 1440, window_height = 940;
    bool gpu = true, volume = true, recovery = true, maximized = false, cuda = false;
    Preferences() {
        views[0].view = 0;
    }
};
// Portable text format; malformed or unsupported settings are rejected as a whole.
std::string encode_preferences(const Preferences &);
Preferences decode_preferences(const std::string &);
struct RestoredWorkspace {
    Preferences preferences;
    SessionStates states;
    uint64_t revision = 0;
    std::string message;
};
RestoredWorkspace restore_workspace(const std::filesystem::path &directory);

// One writer owns a workspace directory. The application holds an exclusive OS file lock.
// Pending requests coalesce; disk I/O never runs on the UI or simulation thread.
class WorkspaceWriter {
  public:
    explicit WorkspaceWriter(std::filesystem::path directory);
    ~WorkspaceWriter();
    WorkspaceWriter(const WorkspaceWriter &) = delete;
    WorkspaceWriter &operator=(const WorkspaceWriter &) = delete;
    void request(Preferences, std::optional<SessionSnapshot> = std::nullopt);
    struct Status {
        bool busy = false;
        uint64_t checkpoints = 0;
        std::string error;
    };
    Status status() const;
    void flush();

  private:
    void loop();
    std::filesystem::path directory;
    mutable std::mutex mutex;
    std::condition_variable wake;
    std::optional<Preferences> pending_preferences;
    std::optional<SessionSnapshot> pending_sessions;
    bool stopping = false, writing = false;
    uint64_t checkpoints = 0;
    std::string error;
    std::thread worker;
};
} // namespace lenia
