#pragma once
#include <cstdint>
#include <cmath>
namespace quota {
inline float shortestRotationDelta(float target, float current) {
    return std::remainder(target-current, 360.f);
}
enum class OrientationTracking { Cardinal, Continuous };
class Orientation {
public:
    explicit Orientation(int panelOffsetDegrees = 0,
                         OrientationTracking tracking = OrientationTracking::Cardinal)
        : panelOffset(panelOffsetDegrees), tracking(tracking) {}
    int update(float x, float y, float z, int64_t nowUs);
    int angle() const { return stable; }
    const char* holdReason() const { return reason; }
    int64_t settledForUs(int64_t nowUs) const { return settleSinceUs < 0 ? 0 : nowUs-settleSinceUs; }
private:
    int panelOffset = 0;
    OrientationTracking tracking;
    float fx = 0, fy = 0;
    bool initialized = false, flatLocked = true;
    int stable = 0, candidate = -1;
    int64_t since = 0, lastSampleUs = 0;
    int64_t settleSinceUs = -1, lastPublishUs = -450000;
    struct PoseSample { float degrees; int64_t atUs; };
    PoseSample poses[32]{};
    unsigned poseCount = 0, poseNext = 0;
    const char* reason = "boot";
    void resetSettling() { settleSinceUs = -1; poseCount = poseNext = 0; }
};
}
