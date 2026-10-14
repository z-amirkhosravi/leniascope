#pragma once
#include "batch.h"
#include "gallery.h"
#include "runner.h"
#include "viewer.h"
#include <atomic>
#include <future>

namespace lenia {
// Operations act on Runner's current field. Immutable inputs belong to background jobs/results,
// not to a second user workspace or a browsable snapshot collection.
class StudyPanel {
  public:
    StudyPanel(std::filesystem::path archive, bool writable, std::shared_ptr<GpuContext>);
    ~StudyPanel();
    bool busy() const;
    bool can_change_source() const;
    bool showing_components(const Frame *) const;
    Renderer &field_renderer(Renderer &, const Frame *) const;
    void allow_saving() {
        writable = true;
    }
    void open_source(std::filesystem::path, std::string);
    void tick(Runner &, std::array<ViewSettings, 2> &);
    void draw_controls(Runner &, const std::array<ViewSettings, 2> &, float dpi, bool closing);
    double draw_scene(Renderer &, const Frame *, const Config &, ViewSettings &, ViewerUi &, float dpi,
                      bool closing);
    bool take_activation();
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
        Config config;
        uint64_t revision = 0;
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
        std::optional<BehaviorReport> report;
        std::optional<GalleryStudies> studies;
        std::optional<std::vector<SavedBatch>> batches;
        std::optional<SavedBatch> batch;
        std::optional<ShapeMeasurement> shape;
        std::string studies_source;
    };
    enum class Job {
        None,
        Select,
        Capture,
        Load,
        Study,
        SaveStudy,
        Batch,
        BatchLabel,
        Shape
    };
    Job job = Job::None, requested = Job::None;
    std::filesystem::path archive_root, root;
    bool writable, activate = false, attach_restore = false, activate_on_restore = false,
                   controls_initialized = false;
    std::shared_ptr<GpuContext> gpu;
    std::optional<GalleryEntry> selected;
    std::optional<uint64_t> source_revision;
    std::future<Result> work;
    std::future<FieldCapture> capture;
    std::future<uint64_t> restore;
    std::optional<FieldCapture> captured;
    std::array<ViewSettings, 2> capture_views;
    ViewSettings restore_view;
    int restore_mode = 0, tool = 0, presentation = 0, requested_presentation = -1;
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
    void request(Job, Runner &, const std::array<ViewSettings, 2> &);
    void start_requested();
    void save_capture();
    void reset_source();
    void restore_source();
    void begin_shape(FieldCapture);
    void draw_components(float dpi, const ViewSettings &);
    void draw_shape(Runner &, const std::array<ViewSettings, 2> &, float dpi, bool closing);
    static void draw_shape_summary(const std::optional<Morphology> &, const Config &, double threshold);
    void begin_batch();
    void draw_batch(Runner &, const std::array<ViewSettings, 2> &, const State &, float dpi, bool closing);
    void draw_batch_results(float dpi, bool closing);
    void batch_field(bool final);
    void batch_annotation();
    static std::vector<SavedBatch> read_batches(const std::filesystem::path &, std::vector<std::string> &);
    void begin_study();
    void save_study();
    void draw_study(Runner &, const std::array<ViewSettings, 2> &, const State &, float dpi, bool closing);
    void draw_study_results(float dpi);
};
} // namespace lenia
