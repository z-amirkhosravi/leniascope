#include "gallery.h"
#include "file_integrity.h"
#include "workspace.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <locale>
#include <random>
#include <sstream>
#include <stdexcept>

namespace lenia {
namespace {
namespace fs = std::filesystem;
void require(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
bool valid_id(const std::string &id) {
    return id.size() == 32 && id.find_first_not_of("0123456789abcdef") == std::string::npos;
}
fs::path directory(const fs::path &root, const std::string &id) {
    require(valid_id(id), "Invalid gallery entry ID");
    return root / id;
}
std::string identity() {
    static std::atomic<uint64_t> sequence{0};
    std::random_device rng;
    std::ostringstream s;
    s << std::hex << std::setfill('0') << std::setw(16)
      << uint64_t(std::chrono::high_resolution_clock::now().time_since_epoch().count()) << std::setw(16)
      << ((uint64_t(rng()) << 32) ^ rng() ^ ++sequence);
    return s.str();
}
void validate(const GalleryNotes &a) {
    require(!a.title.empty() && a.title.find_first_not_of(" \t\r\n") != std::string::npos &&
                a.title.size() <= 127 && a.tags.size() <= 255 && a.notes.size() <= 4095,
            "A title is required; gallery text exceeds its limit");
    for (const auto *s : {&a.title, &a.tags, &a.notes})
        require(s->find('\0') == std::string::npos, "Gallery text contains a null character");
    require(std::isfinite(a.observed_time) && a.observed_time >= 0 && a.observed_time <= 1e12,
            "Observation duration must be finite and nonnegative");
}
std::vector<uint8_t> read(const fs::path &path, size_t limit) {
    auto size = fs::file_size(path);
    require(size <= limit, "Gallery metadata or preview is too large");
    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    std::ifstream in(path, std::ios::binary);
    in.read(reinterpret_cast<char *>(bytes.data()), std::streamsize(bytes.size()));
    require(bool(in), "Cannot read gallery file");
    return bytes;
}
void write(const fs::path &path, const char *data, size_t size) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(data, std::streamsize(size));
    out.close();
    require(bool(out), "Cannot write gallery file");
}
std::string summary(const State &s) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    auto &c = s.config;
    out << std::setprecision(17) << c.width << ' ' << c.height << ' ' << c.depth << ' ' << c.radius << ' '
        << c.mu << ' ' << c.sigma << ' ' << c.dt << ' ' << int(c.rule) << ' ' << c.smoothness << '\n'
        << s.seed << ' ' << s.steps << ' ' << s.time << ' ' << std::quoted(s.backend) << '\n'
        << bool(s.initialization);
    if (s.initialization) {
        auto &a = *s.initialization;
        out << ' ' << s.initialization_radius << ' ' << a.patches << ' ' << a.size_min << ' ' << a.size_max
            << ' ' << a.value_min << ' ' << a.value_max << ' ' << uint32_t(a.layout) << ' ' << a.spread << ' '
            << a.angle << ' ' << a.elevation;
    }
    if (!s.edits.empty())
        out << '\n' << std::quoted(s.edits);
    return out.str();
}
std::string encode(const GalleryEntry &e) {
    validate(e.annotation);
    Preferences prefs;
    prefs.views[e.state.config.depth > 1] = e.view;
    std::ostringstream out;
    out.imbue(std::locale::classic());
    auto &a = e.annotation;
    out << std::setprecision(17) << "LeniascopeGallery " << (e.state.edits.empty() ? 2 : 3) << '\n'
        << e.created << ' ' << e.field_hash << ' ' << e.preview_hash << '\n'
        << std::quoted(a.title) << '\n'
        << std::quoted(a.tags) << '\n'
        << std::quoted(a.notes) << '\n'
        << a.reference << ' ' << a.observed_time << '\n'
        << summary(e.state) << '\n'
        << e.measures.mean << ' ' << e.measures.minimum << ' ' << e.measures.maximum << '\n'
        << std::quoted(encode_preferences(prefs)) << '\n';
    return out.str();
}
GalleryPreview decode_preview(const std::vector<uint8_t> &bmp) {
    require(bmp.size() >= 54, "Truncated gallery preview");
    auto u32 = [&](size_t at) {
        return uint32_t(bmp[at]) | (uint32_t(bmp[at + 1]) << 8) | (uint32_t(bmp[at + 2]) << 16) |
               (uint32_t(bmp[at + 3]) << 24);
    };
    int32_t width = int32_t(u32(18)), height = int32_t(u32(22));
    require(bmp[0] == 'B' && bmp[1] == 'M' && u32(10) == 54 && u32(14) == 40 && bmp[26] == 1 &&
                bmp[27] == 0 && bmp[28] == 32 && bmp[29] == 0 && u32(30) == 0 && width > 0 && width <= 1024 &&
                height < 0 && height >= -1024,
            "Unsupported gallery preview format");
    require(bmp.size() == 54 + size_t(width) * size_t(-height) * 4 && u32(2) == bmp.size(),
            "Invalid gallery preview size");
    return {width, -height, {bmp.begin() + 54, bmp.end()}};
}
} // namespace
GalleryEntry read_gallery_entry(const fs::path &root, const std::string &id) {
    auto dir = directory(root, id);
    auto bytes = read(dir / "entry.txt", 65536);
    std::istringstream in(std::string(bytes.begin(), bytes.end()));
    in.imbue(std::locale::classic());
    GalleryEntry e;
    e.id = id;
    std::string magic, prefs;
    int version = 0, rule = 0;
    bool seeded = false;
    auto &a = e.annotation;
    auto &s = e.state;
    auto &c = s.config;
    in >> magic >> version >> e.created >> e.field_hash >> e.preview_hash >> std::quoted(a.title) >>
        std::quoted(a.tags) >> std::quoted(a.notes) >> a.reference >> a.observed_time >> c.width >>
        c.height >> c.depth >> c.radius >> c.mu >> c.sigma >> c.dt >> rule >> c.smoothness >> s.seed >>
        s.steps >> s.time >> std::quoted(s.backend) >> seeded;
    if (seeded) {
        s.initialization.emplace();
        auto &r = *s.initialization;
        in >> s.initialization_radius >> r.patches >> r.size_min >> r.size_max >> r.value_min >> r.value_max;
        if (version >= 2) {
            uint32_t layout = 0;
            in >> layout >> r.spread >> r.angle >> r.elevation;
            r.layout = SeedLayout(layout);
        }
        r.validate();
        require(s.initialization_radius > 0 && s.initialization_radius <= 4096, "Invalid seed radius");
    }
    if (version >= 3)
        in >> std::quoted(s.edits);
    require(s.edits.size() <= 16384 && s.edits.find('\0') == std::string::npos,
            "Invalid field construction history");
    in >> e.measures.mean >> e.measures.minimum >> e.measures.maximum >> std::quoted(prefs);
    require(bool(in) && magic == "LeniascopeGallery" && (version >= 1 && version <= 3) && e.created > 0,
            "Invalid gallery metadata");
    in >> std::ws;
    require(in.eof(), "Unexpected gallery metadata");
    require(rule >= 0 && rule <= 2, "Unknown gallery rule");
    c.rule = Rule(rule);
    c.validate();
    validate(a);
    require(std::isfinite(s.time) && s.time >= 0 && !s.backend.empty() && s.backend.size() <= 1024,
            "Invalid gallery provenance");
    auto &m = e.measures;
    require(std::isfinite(m.mean) && std::isfinite(m.minimum) && std::isfinite(m.maximum) && 0 <= m.minimum &&
                m.minimum <= m.maximum && m.maximum <= 1 && m.mean >= -1e-8 && m.mean <= 1 + 1e-8,
            "Invalid gallery measurements");
    e.view = decode_preferences(prefs).views[c.depth > 1];
    require(fs::is_regular_file(dir / "state.lenia"), "Gallery checkpoint is missing");
    return e;
}
GalleryCatalog list_gallery(const fs::path &root) {
    GalleryCatalog catalog;
    if (!fs::exists(root))
        return catalog;
    for (const auto &dir : fs::directory_iterator(root)) {
        auto id = dir.path().filename().u8string();
        if (!valid_id(id) || !dir.is_directory())
            continue;
        try {
            catalog.entries.push_back(read_gallery_entry(root, id));
        } catch (const std::exception &e) {
            catalog.errors.push_back(id + ": " + e.what());
        }
    }
    std::sort(catalog.entries.begin(), catalog.entries.end(), [](const auto &a, const auto &b) {
        return a.created != b.created ? a.created > b.created : a.id > b.id;
    });
    return catalog;
}
GalleryEntry save_gallery_entry(const fs::path &root, const State &s, const ViewSettings &view,
                                const GalleryNotes &notes, const std::vector<uint8_t> &bmp) {
    validate(notes);
    s.config.validate();
    decode_preview(bmp);
    require(s.values.size() == s.config.count(), "A gallery entry needs the actual field");
    GalleryEntry e;
    e.id = identity();
    e.annotation = notes;
    e.view = view;
    e.created =
        std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
            .count();
    e.measures = measure(s.values);
    e.state = {s.config, {}, s.seed, s.steps, s.time, s.backend, s.initialization, s.initialization_radius,
               s.edits};
    encode(e); // Validate view and annotations before disk writes.
    fs::create_directories(root);
    auto staging = root / (".writing-" + e.id), dest = directory(root, e.id);
    require(!fs::exists(dest) && fs::create_directory(staging), "Gallery ID collision; retry saving");
    try {
        save_state(s, (staging / "state.lenia").u8string());
        write(staging / "preview.bmp", reinterpret_cast<const char *>(bmp.data()), bmp.size());
        e.field_hash = file_checksum(staging / "state.lenia");
        e.preview_hash = file_checksum(staging / "preview.bmp");
        auto text = encode(e);
        write(staging / "entry.txt", text.data(), text.size());
        fs::rename(staging, dest);
    } catch (...) {
        std::error_code ignored;
        fs::remove_all(staging, ignored);
        throw;
    }
    return e;
}
State load_gallery_state(const fs::path &root, const std::string &id) {
    auto e = read_gallery_entry(root, id);
    auto file = directory(root, id) / "state.lenia";
    require(fs::file_size(file) <= e.state.config.count() * sizeof(double) + 32768,
            "Gallery checkpoint is too large");
    require(file_checksum(file) == e.field_hash, "Gallery checkpoint checksum mismatch");
    auto s = load_state(file.u8string());
    require(summary(s) == summary(e.state), "Gallery provenance disagrees with checkpoint");
    auto m = measure(s.values);
    require(m.mean == e.measures.mean && m.minimum == e.measures.minimum && m.maximum == e.measures.maximum,
            "Gallery measurements disagree with checkpoint");
    return s;
}
GalleryPreview load_gallery_preview(const fs::path &root, const std::string &id) {
    auto e = read_gallery_entry(root, id);
    auto path = directory(root, id) / "preview.bmp";
    require(fs::file_size(path) <= 54 + 1024 * 1024 * 4, "Gallery preview is too large");
    require(file_checksum(path) == e.preview_hash, "Gallery preview checksum mismatch");
    return decode_preview(read(path, 54 + 1024 * 1024 * 4));
}
GalleryEntry annotate_gallery_entry(const fs::path &root, const std::string &id, const GalleryNotes &notes) {
    auto e = read_gallery_entry(root, id);
    e.annotation = notes;
    auto text = encode(e), temporary = std::string(".notes-") + identity();
    auto dir = directory(root, id), path = dir / temporary;
    try {
        write(path, text.data(), text.size());
        fs::rename(path, dir / "entry.txt");
    } catch (...) {
        std::error_code ignored;
        fs::remove(path, ignored);
        throw;
    }
    return e;
}
void save_behavior_files(const fs::path &destination, const BehaviorReport &report) {
    auto metadata = encode_behavior(report), csv = behavior_csv(report);
    require(!fs::exists(destination), "Study destination already exists");
    auto parent = destination.parent_path();
    if (!parent.empty())
        fs::create_directories(parent);
    auto staging = parent / (".study-writing-" + identity());
    require(fs::create_directory(staging), "Cannot create study staging directory");
    try {
        write(staging / "report.txt", metadata.data(), metadata.size());
        write(staging / "samples.csv", csv.data(), csv.size());
        fs::rename(staging, destination);
    } catch (...) {
        std::error_code ignored;
        fs::remove_all(staging, ignored);
        throw;
    }
}
GalleryStudy save_gallery_study(const fs::path &root, const std::string &id, const BehaviorReport &report) {
    auto entry = read_gallery_entry(root, id);
    require(report.source_id == id && report.source_hash == entry.field_hash &&
                report.config == entry.state.config && report.initial_step == entry.state.steps &&
                report.seed == entry.state.seed && report.initial_time == entry.state.time,
            "Study does not match its gallery source");
    GalleryStudy saved{identity(), report};
    save_behavior_files(directory(root, id) / "studies" / saved.id, report);
    return saved;
}
GalleryStudies list_gallery_studies(const fs::path &root, const std::string &id) {
    auto entry = read_gallery_entry(root, id);
    auto path = directory(root, id) / "studies";
    GalleryStudies result;
    if (!fs::exists(path))
        return result;
    for (const auto &dir : fs::directory_iterator(path)) {
        auto study_id = dir.path().filename().u8string();
        if (!valid_id(study_id) || !dir.is_directory())
            continue;
        try {
            auto metadata = read(dir.path() / "report.txt", 131072);
            auto report = decode_behavior({metadata.begin(), metadata.end()});
            require(report.source_id == id && report.source_hash == entry.field_hash &&
                        report.config == entry.state.config && report.initial_step == entry.state.steps &&
                        report.initial_time == entry.state.time && report.seed == entry.state.seed,
                    "Study source no longer matches entry");
            auto csv = read(dir.path() / "samples.csv", 131072);
            require(std::string(csv.begin(), csv.end()) == behavior_csv(report),
                    "Study CSV disagrees with report");
            result.entries.push_back({study_id, std::move(report)});
        } catch (const std::exception &e) {
            result.errors.push_back(study_id + ": " + e.what());
        }
    }
    std::sort(result.entries.begin(), result.entries.end(),
              [](const auto &a, const auto &b) { return a.id > b.id; });
    return result;
}
} // namespace lenia
