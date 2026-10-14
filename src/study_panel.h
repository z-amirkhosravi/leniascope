#pragma once
#include "batch.h"
#include "gallery.h"
#include "runner.h"
#include "viewer.h"
#include <atomic>
#include <future>

namespace lenia {
// Studies own an immutable source and independent presentation, never the live Runner's field.
// Current-field snapshots are archived separately; existing gallery reports remain readable in place.
class StudyPanel {
  public:
    StudyPanel(std::filesystem::path archive, bool writable, std::shared_ptr<GpuContext>);
    ~StudyPanel();
    bool busy() const;
    bool can_change_source() const;
    void use_current(Runner &, const std::array<ViewSettings, 2> &);
    void open_source(std::filesystem::path, std::string);
    void tick(Runner &, std::array<ViewSettings, 2> &);
    void draw_controls(Runner &, const std::array<ViewSettings, 2> &, float dpi, bool closing);
    double draw_scene(float dpi, bool closing);
    bool take_activation();
    bool take_simulation_request();
    void stop_study() {
        study_stop = true;
    }
    const std::string &close_error() const {
        return write_error;
    }
    void retry_save();
    bool smoke(Runner &, const std::array<ViewSettings, 2> &);
    const char *smoke_capture_name() const;

  private:
    struct SavedBatch {
        std::string id;
        BatchResult result;
    };
    struct ShapeMeasurement {
        Morphology shape;
        double threshold = .01;
        std::vector<ShapeComponent> components;
        std::vector<uint32_t> order;
        std::unique_ptr<Renderer> renderer;
    };
    struct Result {
        std::filesystem::path source_root;
        GalleryCatalog catalog;
        std::optional<GalleryEntry> entry;
        std::optional<State> state;
        std::unique_ptr<Renderer> renderer;
        std::optional<BehaviorReport> report;
        std::optional<GalleryStudies> studies;
        std::optional<std::vector<SavedBatch>> batches;
        std::optional<SavedBatch> batch;
        std::optional<ShapeMeasurement> shape;
        std::string studies_source;
    };
    enum class Job {
        None,
        Scan,
        Select,
        Capture,
        Load,
        Study,
        SaveStudy,
        Batch,
        BatchLabel,
        Shape
    };
    Job job = Job::None;
    std::filesystem::path archive_root, root;
    bool writable, activate = false, simulation_requested = false, results_requested = false;
    std::shared_ptr<GpuContext> gpu;
    GalleryCatalog archive;
    std::optional<GalleryEntry> selected;
    std::unique_ptr<Renderer> source_renderer;
    std::future<Result> work;
    std::future<State> capture;
    std::future<void> restore;
    std::optional<State> captured;
    std::array<ViewSettings, 2> capture_views;
    ViewSettings shape_view, restore_view;
    ViewerUi viewer_ui;
    int restore_mode = 0, tool = 0;
    std::string message, write_error;
    BehaviorOptions study_options;
    bool study_settings_open = true, study_zoom = false;
    int study_backend = 0, study_index = 0;
    GalleryStudies studies;
    std::optional<BehaviorReport> pending_study;
    std::atomic<bool> study_stop{false};
    std::atomic<int> study_progress{0}, batch_progress{0};
    BatchPlan batch_plan;
    bool batch_settings_open = true;
    std::vector<SavedBatch> batches;
    int batch_index = 0, batch_run = 0;
    char batch_value_text[2048] = "0.14, 0.15, 0.16", batch_label[257]{};
    double shape_threshold = .01;
    std::optional<ShapeMeasurement> shape;
    uint32_t shape_component = 0;
    bool shape_scalar = false;
    int smoke_phase = 0, smoke_wait = 0;
    bool smoke_current = false;
    std::string smoke_signature;
    std::future<State> smoke_check;
    void save_capture();
    void reset_source();
    void restore_source();
    void begin_shape();
    void draw_components(float dpi);
    void draw_shape(float dpi, bool closing);
    static void draw_shape_summary(const std::optional<Morphology> &, const Config &, double threshold);
    void begin_batch();
    void draw_batch(float dpi, bool closing);
    void draw_batch_results(float dpi, bool closing);
    void batch_field(bool final);
    void batch_annotation();
    static std::vector<SavedBatch> read_batches(const std::filesystem::path &, std::vector<std::string> &);
    void begin_study();
    void save_study();
    void draw_study(float dpi, bool closing);
    void draw_study_results(float dpi);
};
} // namespace lenia
