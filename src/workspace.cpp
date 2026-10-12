#include "workspace.h"
#include "file_integrity.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <stdexcept>

namespace lenia {
namespace {
namespace fs = std::filesystem;
void bounded(double value, double low, double high) {
    if (!std::isfinite(value) || value < low || value > high)
        throw std::runtime_error("Workspace setting is out of range");
}
void validate(const Preferences &p) {
    for (const auto &s : p.seeds)
        s.validate();
    bounded(p.steps_per_second, 1, 240);
    bounded(p.window_width, 320, 16384);
    bounded(p.window_height, 240, 16384);
    for (const auto &v : p.views) {
        bounded(v.view, 0, 2);
        bounded(v.axis, 0, 2);
        bounded(v.palette, 0, 255);
        bounded(v.slice, 0, 1);
        bounded(v.yaw, -1e6, 1e6);
        bounded(v.pitch, -1.5, 1.5);
        bounded(v.distance, .69, 5.01);
        bounded(v.opacity, .09, 10.01);
        bounded(v.threshold, 0, 1);
        bounded(v.clip, 0, 1);
        bounded(v.quality, .29, 2.01);
        bounded(v.zoom, .24, 4.01);
        for (auto offset : v.offset)
            bounded(offset, 0, 1);
    }
    if (p.views[0].view != 0)
        throw std::runtime_error("The 2D workspace must use a slice view");
}
std::string read_text(const fs::path &path, uintmax_t maximum = 8192) {
    if (fs::file_size(path) > maximum)
        throw std::runtime_error("Workspace metadata is too large");
    std::ifstream file(path, std::ios::binary);
    if (!file)
        throw std::runtime_error("Cannot read workspace metadata");
    std::ostringstream text;
    text << file.rdbuf();
    if (file.bad())
        throw std::runtime_error("Workspace metadata read failed");
    return text.str();
}
void atomic_text(const fs::path &path, const std::string &text) {
    static std::atomic<uint64_t> sequence{0};
    auto temporary = path;
    temporary += ".writing-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
                 "-" + std::to_string(++sequence);
    try {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        file.write(text.data(), std::streamsize(text.size()));
        file.close();
        if (!file)
            throw std::runtime_error("Workspace metadata write failed");
        fs::rename(temporary, path);
    } catch (...) {
        std::error_code ignored;
        fs::remove(temporary, ignored);
        throw;
    }
}
struct Manifest {
    uint64_t revision = 0, hashes[2]{};
    bool present[2]{}, volume = true;
};
Manifest manifest(const fs::path &slot) {
    std::istringstream in(read_text(slot / "complete.txt", 1024));
    std::string magic;
    int version;
    Manifest m;
    if (!(in >> magic >> version >> m.revision >> m.present[0] >> m.present[1] >> m.volume >> m.hashes[0] >>
          m.hashes[1]) ||
        magic != "LeniascopeRecovery" || version != 1 || !m.revision || !m.present[m.volume])
        throw std::runtime_error("Invalid recovery manifest");
    in >> std::ws;
    if (!in.eof())
        throw std::runtime_error("Unexpected recovery metadata");
    return m;
}
fs::path field_path(const fs::path &slot, int mode) {
    return slot / (mode ? "field-3d.lenia" : "field-2d.lenia");
}
void write_recovery(const fs::path &directory, const SessionSnapshot &s, uint64_t revision) {
    if (!s.states[s.volume])
        throw std::runtime_error("No active session to recover");
    auto slot = directory / (revision % 2 ? "recovery-a" : "recovery-b");
    fs::create_directories(slot);
    // Invalidate the older slot before touching any of its fields. The other complete slot
    // remains recoverable if the process is interrupted or any subsequent write fails.
    fs::remove(slot / "complete.txt");
    uint64_t hashes[2]{};
    for (int i = 0; i < 2; ++i) {
        if (!s.states[i])
            continue;
        if ((s.states[i]->config.depth > 1) != bool(i))
            throw std::runtime_error("Recovery session dimension mismatch");
        auto path = field_path(slot, i);
        save_state(*s.states[i], path.u8string());
        hashes[i] = file_checksum(path);
    }
    std::ostringstream out;
    out << "LeniascopeRecovery 1\n"
        << revision << ' ' << bool(s.states[0]) << ' ' << bool(s.states[1]) << ' ' << s.volume << ' '
        << hashes[0] << ' ' << hashes[1] << '\n';
    atomic_text(slot / "complete.txt", out.str());
}
} // namespace
std::string encode_preferences(const Preferences &p) {
    validate(p);
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::setprecision(std::numeric_limits<double>::max_digits10) << "LeniascopeWorkspace 4\n"
        << p.steps_per_second << ' ' << p.gpu << ' ' << p.volume << ' ' << p.recovery << ' ' << p.window_width
        << ' ' << p.window_height << ' ' << p.maximized << '\n';
    for (const auto &v : p.views) {
        out << v.view << ' ' << v.axis << ' ' << v.palette << ' ' << v.slice << ' ' << v.yaw << ' ' << v.pitch
            << ' ' << v.distance << ' ' << v.opacity << ' ' << v.threshold << ' ' << v.clip << ' '
            << v.quality << ' ' << v.zoom;
        for (auto offset : v.offset)
            out << ' ' << offset;
        out << '\n';
    }
    for (int i = 0; i < 2; ++i) {
        const auto &s = p.seeds[i];
        out << s.patches << ' ' << s.size_min << ' ' << s.size_max << ' ' << s.value_min << ' ' << s.value_max
            << ' ' << p.keep_seed_coverage[i] << ' ' << p.scale_radius[i] << '\n';
    }
    out << p.cuda << '\n';
    for (const auto &s : p.seeds)
        out << uint32_t(s.layout) << ' ' << s.spread << ' ' << s.angle << ' ' << s.elevation << '\n';
    return out.str();
}
Preferences decode_preferences(const std::string &text) {
    std::istringstream in(text);
    in.imbue(std::locale::classic());
    Preferences p;
    std::string magic;
    int version;
    if (!(in >> magic >> version >> p.steps_per_second >> p.gpu >> p.volume >> p.recovery >> p.window_width >>
          p.window_height >> p.maximized) ||
        magic != "LeniascopeWorkspace" || (version < 1 || version > 4))
        throw std::runtime_error("Invalid workspace settings header");
    for (auto &v : p.views) {
        in >> v.view >> v.axis >> v.palette >> v.slice >> v.yaw >> v.pitch >> v.distance >> v.opacity >>
            v.threshold >> v.clip >> v.quality >> v.zoom;
        for (auto &offset : v.offset)
            in >> offset;
    }
    if (version >= 2)
        for (int i = 0; i < 2; ++i) {
            auto &s = p.seeds[i];
            in >> s.patches >> s.size_min >> s.size_max >> s.value_min >> s.value_max >>
                p.keep_seed_coverage[i] >> p.scale_radius[i];
        }
    if (version >= 3)
        in >> p.cuda;
    if (version >= 4)
        for (auto &s : p.seeds) {
            uint32_t layout = 0;
            in >> layout >> s.spread >> s.angle >> s.elevation;
            s.layout = SeedLayout(layout);
        }
    if (!in)
        throw std::runtime_error("Truncated workspace settings");
    in >> std::ws;
    if (!in.eof())
        throw std::runtime_error("Unexpected workspace settings");
    validate(p);
    return p;
}
static RestoredWorkspace read_workspace(const fs::path &directory, bool include_disabled) {
    RestoredWorkspace result;
    if (fs::exists(directory / "preferences.txt")) {
        try {
            result.preferences = decode_preferences(read_text(directory / "preferences.txt"));
        } catch (const std::exception &e) {
            result.message = std::string("Settings reset: ") + e.what();
        }
    }
    if (!result.preferences.recovery && !include_disabled)
        return result;
    struct Candidate {
        fs::path slot;
        Manifest data;
    };
    std::vector<Candidate> candidates;
    bool incomplete = false;
    for (const auto *name : {"recovery-a", "recovery-b"}) {
        auto slot = directory / name;
        if (!fs::exists(slot))
            continue;
        try {
            candidates.push_back({slot, manifest(slot)});
        } catch (...) {
            incomplete = true;
        }
    }
    std::sort(candidates.begin(), candidates.end(),
              [](const auto &a, const auto &b) { return a.data.revision > b.data.revision; });
    for (const auto &candidate : candidates) {
        try {
            SessionStates states;
            for (int i = 0; i < 2; ++i) {
                if (!candidate.data.present[i])
                    continue;
                auto path = field_path(candidate.slot, i);
                if (file_checksum(path) != candidate.data.hashes[i])
                    throw std::runtime_error("Recovery checksum mismatch");
                states[i] = std::make_shared<State>(load_state(path.u8string()));
                if ((states[i]->config.depth > 1) != bool(i))
                    throw std::runtime_error("Recovery session dimension mismatch");
            }
            auto current = manifest(candidate.slot);
            if (current.revision != candidate.data.revision ||
                current.hashes[0] != candidate.data.hashes[0] ||
                current.hashes[1] != candidate.data.hashes[1] ||
                current.present[0] != candidate.data.present[0] ||
                current.present[1] != candidate.data.present[1] || current.volume != candidate.data.volume)
                throw std::runtime_error("Recovery changed while being read");
            result.states = std::move(states);
            result.revision = candidate.data.revision;
            if (!result.states[result.preferences.volume])
                result.preferences.volume = candidate.data.volume;
            if (!result.message.empty())
                result.message += ". ";
            result.message +=
                incomplete ? "Recovered previous complete sessions; newer recovery was damaged or incomplete."
                           : "Restored saved sessions, paused.";
            return result;
        } catch (...) {
            incomplete = true;
        }
    }
    if (incomplete)
        result.message += " No complete recovery could be loaded; starting a new session.";
    return result;
}
RestoredWorkspace restore_workspace(const fs::path &directory) {
    return read_workspace(directory, false);
}
WorkspaceWriter::WorkspaceWriter(fs::path path) : directory(std::move(path)) {
    worker = std::thread([this] { loop(); });
}
WorkspaceWriter::~WorkspaceWriter() {
    {
        std::lock_guard<std::mutex> lock(mutex);
        stopping = true;
    }
    wake.notify_all();
    worker.join();
}
void WorkspaceWriter::request(Preferences p, std::optional<SessionSnapshot> sessions) {
    validate(p);
    {
        std::lock_guard<std::mutex> lock(mutex);
        pending_preferences = p;
        if (!p.recovery)
            pending_sessions.reset();
        else if (sessions)
            pending_sessions = std::move(sessions);
    }
    wake.notify_all();
}
WorkspaceWriter::Status WorkspaceWriter::status() const {
    std::lock_guard<std::mutex> lock(mutex);
    return {writing || bool(pending_preferences), checkpoints, error};
}
void WorkspaceWriter::flush() {
    std::unique_lock<std::mutex> lock(mutex);
    wake.wait(lock, [this] { return !writing && !pending_preferences; });
}
void WorkspaceWriter::loop() {
    // Find the newest *valid* slot, so a failed attempt never overwrites the only good recovery.
    uint64_t revision = 0;
    try {
        revision = read_workspace(directory, true).revision;
    } catch (const std::exception &e) {
        std::lock_guard<std::mutex> lock(mutex);
        error = e.what();
    }
    for (;;) {
        Preferences preferences;
        std::optional<SessionSnapshot> sessions;
        {
            std::unique_lock<std::mutex> lock(mutex);
            wake.wait(lock, [this] { return stopping || pending_preferences.has_value(); });
            if (!pending_preferences)
                return;
            preferences = *pending_preferences;
            sessions = std::move(pending_sessions);
            pending_preferences.reset();
            pending_sessions.reset();
            writing = true;
        }
        std::string failure;
        bool saved = false;
        try {
            fs::create_directories(directory);
            if (sessions && preferences.recovery) {
                revision = read_workspace(directory, true).revision;
                if (revision == UINT64_MAX)
                    throw std::runtime_error("Recovery revision overflow");
                write_recovery(directory, *sessions, revision + 1);
                ++revision;
                saved = true;
            }
            atomic_text(directory / "preferences.txt", encode_preferences(preferences));
        } catch (const std::exception &e) {
            failure = e.what();
        }
        {
            std::lock_guard<std::mutex> lock(mutex);
            writing = false;
            error = std::move(failure);
            if (saved)
                ++checkpoints;
        }
        wake.notify_all();
    }
}
} // namespace lenia
