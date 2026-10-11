#pragma once
namespace lenia {
struct ViewSettings {
    int view = 1, axis = 2, palette = 0;
    float slice = .5f, yaw = .65f, pitch = .3f, distance = 1.9f, opacity = 2, threshold = .02f, clip = 1,
          quality = .75f, zoom = 1;
    float offset[3] = {0, 0, 0};
};
} // namespace lenia
