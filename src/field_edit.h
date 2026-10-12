#pragma once
#include "simulation.h"
#include <deque>

namespace lenia {
struct Region2D {
    int x = 0, y = 0, width = 1, height = 1;
    void validate(const Config &) const;
};
enum class PatchShape {
    Rectangle,
    Ellipse
};
enum class PatchBlend {
    Maximum,
    Replace
};
struct Patch2D {
    int width = 0, height = 0;
    // Premultiplied scalar values and coverage. Zero coverage leaves the destination untouched.
    std::vector<double> values, coverage;
    std::string source;
    void validate() const;
};
uint64_t field_identity(const State &);
Patch2D copy_patch(const State &, Region2D);
Patch2D random_patch(int width, int height, PatchShape, uint64_t seed, double minimum, double maximum);
// Always rotate from the original clipboard to avoid accumulating interpolation error.
Patch2D rotate_patch(const Patch2D &, double clockwise_degrees);
// Top-left coordinates wrap on the torus. Oversized patches are rejected, not folded onto themselves.
void place_patch(State &, const Patch2D &, int x, int y, PatchBlend, bool float32);

class FieldDraft {
  public:
    explicit FieldDraft(State source, bool float32);
    const State &state() const {
        return current;
    }
    bool changed() const {
        return !journal.empty();
    }
    size_t undo_count() const {
        return past.size();
    }
    size_t redo_count() const {
        return future.size();
    }
    void stamp(const Patch2D &, int x, int y, PatchBlend);
    void erase(Region2D, bool keep_only = false);
    void clear();
    void undo();
    void redo();
    // Changed fields start a new experiment at t=0; parent counters and operations are recorded.
    State result() const;
    const std::string &operations() const {
        return journal;
    }

  private:
    State current;
    bool float32;
    std::string prefix, journal;
    struct Revision {
        std::vector<double> values;
        std::string journal;
    };
    std::deque<Revision> past, future;
    void commit(std::vector<double>, std::string operation);
};
} // namespace lenia
