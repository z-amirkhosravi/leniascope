#pragma once
#include "gallery.h"
#include "renderer.h"
#include "runner.h"
#include <atomic>
#include <future>

namespace lenia {
class GalleryPanel {
  public:
    GalleryPanel(std::filesystem::path root, bool writable, std::shared_ptr<GpuContext>);
    ~GalleryPanel();
    void open();
    bool active() const {
        return opened;
    }
    bool busy() const;
    const std::string &close_error() const {
        return write_error;
    }
    void retry_save() {
        if (!write_error.empty() && !busy())
            pending_study ? save_study() : save();
    }
    void stop_study() {
        study_stop = true;
    }
    void tick(Runner &, std::array<ViewSettings, 2> &);
    void draw(Runner &, const std::array<ViewSettings, 2> &, float dpi, bool closing);
    // App-owned smoke path exercises the same capture/save/select/restore operations as the controls.
    bool smoke(Runner &, const std::array<ViewSettings, 2> &);

  private:
    struct Result {
        GalleryCatalog catalog;
        std::optional<GalleryEntry> entry;
        std::optional<GalleryPreview> preview;
        std::optional<State> state;
        std::optional<BehaviorReport> report;
        std::optional<GalleryStudies> studies;
        std::string studies_source;
    };
    enum class Job {
        None,
        Scan,
        Select,
        Save,
        Edit,
        Load,
        Study,
        SaveStudy
    } job = Job::None;
    std::filesystem::path root;
    bool writable, opened = false, editing = false;
    std::shared_ptr<GpuContext> gpu;
    GalleryCatalog catalog;
    std::optional<GalleryEntry> selected;
    ComPtr<ID3D11ShaderResourceView> image;
    float image_aspect = 4.f / 3;
    std::future<Result> work;
    std::future<State> capture;
    std::future<void> restore;
    std::optional<State> captured;
    std::vector<uint8_t> captured_preview;
    std::array<ViewSettings, 2> capture_views;
    ViewSettings restore_view;
    int restore_mode = 0;
    std::string message, editing_id, saved_id, write_error;
    char title[128]{}, tags[256]{}, notes[4096]{}, filter[128]{};
    bool reference = false;
    double observed_time = 0;
    int dimension_filter = 0, smoke_phase = 0;
    std::string smoke_signature;
    std::future<State> smoke_check;
    BehaviorOptions study_options;
    bool study_settings_open = true;
    bool study_zoom = false;
    int study_backend = 0, study_index = 0;
    GalleryStudies studies;
    std::optional<BehaviorReport> pending_study;
    std::atomic<bool> study_stop{false};
    std::atomic<int> study_progress{0};
    void begin_study();
    void save_study();
    void draw_study(float dpi, bool closing);
    void scan();
    void select(std::string);
    void begin_capture(Runner &, const std::array<ViewSettings, 2> &);
    void set_notes(const GalleryNotes &);
    void save();
    void load(Runner &);
    void texture(const GalleryPreview &);
};
} // namespace lenia
