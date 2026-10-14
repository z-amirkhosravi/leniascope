#pragma once
#include "simulation.h"

namespace lenia {
struct Morphology {
    uint64_t active_cells = 0, components = 0, largest_cells = 0, winding_components = 0;
    uint32_t winding_axes = 0; // Union over components: X=1, Y=2, Z=4.
    void validate(const Config &) const;
};
// Cells strictly above threshold, joined through faces (4 neighbors in 2D, 6 in 3D).
// Winding requires a closed path with nonzero torus displacement, not merely a seam crossing.
// Does not mutate the field or assign organism/morphology labels.
Morphology measure_morphology(const State &, double threshold = .01);
const char *morphology_csv_header();
// Unmeasured legacy data yields empty columns. Largest share is undefined for an empty mask.
std::string morphology_csv(const std::optional<Morphology> &);
std::string winding_axes_text(uint32_t);
} // namespace lenia
