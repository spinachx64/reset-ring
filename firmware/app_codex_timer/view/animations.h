#pragma once
#include <algorithm>
#include <cstdint>
namespace quota {
struct Tween {
    float from = 0, to = 0;
    int64_t start = 0, duration = 1;
    float value(int64_t now) const {
        float t = std::max(0.f, std::min(1.f, float(now-start)/duration));
        return from + (to-from)*(1-(1-t)*(1-t)*(1-t));
    }
    void target(float target, int64_t now, int64_t span) {
        from = value(now); to = target; start = now; duration = span;
    }
};
}
