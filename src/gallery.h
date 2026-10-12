#pragma once
#include "behavior.h"
#include "simulation.h"
#include "view_settings.h"
#include <filesystem>

namespace lenia {
struct GalleryNotes {
    std::string title, tags, notes;
    bool reference = false;
    double observed_time = 0; // User-reported observation duration, not age of the field.
};
struct GalleryEntry {
    std::string id;
    int64_t created = 0; // UTC Unix seconds.
    GalleryNotes annotation;
    State state; // Metadata only when browsing; no grid allocation.
    ViewSettings view;
    Measures measures;
    uint64_t field_hash = 0, preview_hash = 0;
};
struct GalleryCatalog {
    std::vector<GalleryEntry> entries;
    std::vector<std::string> errors;
};
struct GalleryPreview {
    int width = 0, height = 0;
    std::vector<uint8_t> bgra;
};
// Each entry is committed by one directory rename. Incomplete staging directories are ignored.
GalleryEntry save_gallery_entry(const std::filesystem::path &, const State &, const ViewSettings &,
                                const GalleryNotes &, const std::vector<uint8_t> &bmp);
GalleryCatalog list_gallery(const std::filesystem::path &);
GalleryEntry read_gallery_entry(const std::filesystem::path &, const std::string &id);
State load_gallery_state(const std::filesystem::path &, const std::string &id);
GalleryPreview load_gallery_preview(const std::filesystem::path &, const std::string &id);
GalleryEntry annotate_gallery_entry(const std::filesystem::path &, const std::string &id,
                                    const GalleryNotes &);
struct GalleryStudy {
    std::string id;
    BehaviorReport report;
};
struct GalleryStudies {
    std::vector<GalleryStudy> entries;
    std::vector<std::string> errors;
};
// Append-only reports live beneath an entry; its field, preview and annotations are untouched.
GalleryStudy save_gallery_study(const std::filesystem::path &, const std::string &entry_id,
                                const BehaviorReport &);
GalleryStudies list_gallery_studies(const std::filesystem::path &, const std::string &entry_id);
// Writes a new standalone report directory for CLI experiments; never overwrites an existing one.
void save_behavior_files(const std::filesystem::path &destination, const BehaviorReport &);
} // namespace lenia
