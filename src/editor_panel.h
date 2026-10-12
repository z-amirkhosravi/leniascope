#pragma once
#include "field_edit.h"
#include "renderer.h"
#include "runner.h"

namespace lenia {
class EditorPanel {
  public:
    explicit EditorPanel(std::shared_ptr<GpuContext>);
    void open(Runner &, const ViewSettings &);
    bool active() const {
        return opened;
    }
    void tick();
    void draw(Runner &, float dpi, bool closing);
    bool smoke(Runner &, const ViewSettings &);
    bool smoke_preview_ready() const {
        return smoke_phase == 3 && smoke_frames == 3;
    }

  private:
    Renderer preview;
    ViewSettings view;
    bool opened = false, upload = false, dragging = false, float32 = false;
    std::future<State> capture, verification;
    std::future<void> applying;
    std::unique_ptr<FieldDraft> draft;
    std::optional<Patch2D> clipboard, rotated;
    Region2D selection;
    int tool = 0, blend = 0, origin[2]{}, drag_start[2]{};
    double angle = 0, center[2]{};
    int patch_size[2]{32, 32}, shape = 1;
    double value_min = 0, value_max = 1;
    uint64_t patch_seed = 1;
    std::string message;
    int smoke_phase = 0, smoke_frames = 0;
    State smoke_source, smoke_expected;
    bool busy() const;
    void copy();
    void transform();
    void stamp();
    void apply(Runner &);
    void discard();
    std::pair<int, int> anchor() const;
};
} // namespace lenia
