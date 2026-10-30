#include "orientation.h"
#include <cmath>
#include <algorithm>
namespace quota {
int Orientation::update(float x, float y, float z, int64_t nowUs) {
    const float g = std::sqrt(x*x + y*y + z*z);
    const float planar = std::hypot(x,y);
    if (!std::isfinite(g) || g < .75f || g > 1.25f) {
        reason = "acceleration"; candidate = -1; initialized = false; flatLocked = true; resetSettling(); return stable;
    }
    // Lock within ~16 degrees of flat; resume above ~22 degrees. Separate
    // thresholds prevent chattering while picking the watch up or putting it down.
    if (planar/g < (flatLocked ? .38f : .28f)) {
        reason = "flat"; flatLocked = true; candidate = -1; initialized = false; resetSettling(); return stable;
    }
    flatLocked = false;
    if (!initialized) { fx = x; fy = y; initialized = true; }
    else {
        // Reject a single abrupt direction spike; sustained turns resume next sample.
        if (std::hypot(x-fx,y-fy) > .8f) {
            reason = "turning"; candidate = -1; fx = x; fy = y; lastSampleUs = nowUs; resetSettling(); return stable;
        }
        float dt = std::max(.005f,std::min(.25f,float(nowUs-lastSampleUs)/1000000.f));
        float alpha = 1.f-std::exp(-dt/.12f);
        fx += alpha*(x-fx); fy += alpha*(y-fy);
    }
    lastSampleUs = nowUs;
    // HAL swaps BMI270 axes. The X sign and panel offset were verified on device.
    float degrees = std::fmod(std::atan2(-fx,fy)*180.f/3.14159265f+panelOffset+360.f,360.f);
    if (tracking == OrientationTracking::Continuous) {
        // Keep a rolling window: noisy samples and slow GUI frames cannot keep
        // restarting a timer forever. Compare angles across wrap via shortest deltas.
        poses[poseNext] = {degrees,nowUs};
        poseNext = (poseNext+1)%32;
        poseCount = std::min(32u,poseCount+1);
        float low = 0, high = 0;
        int64_t oldestUs = nowUs;
        unsigned observed = 0;
        for (unsigned i=0;i<poseCount;++i) {
            const auto& sample = poses[(poseNext+31-i)%32];
            float delta = shortestRotationDelta(sample.degrees,degrees);
            low = std::min(low,delta); high = std::max(high,delta);
            oldestUs = sample.atUs; ++observed;
            // Include at least three samples even when the UI runs slowly.
            if (nowUs-oldestUs >= 280000 && observed >= 3) break;
        }
        bool quiet = high-low <= 4.f;
        settleSinceUs = quiet ? oldestUs : -1;
        if (observed < 3 || nowUs-oldestUs < 280000) reason = "settling";
        else if (!quiet) reason = "turning";
        else if (std::abs(shortestRotationDelta(degrees,stable)) < 2.5f) reason = "aligned";
        else if (nowUs-lastPublishUs < 450000) reason = "cadence";
        else {
            stable = static_cast<int>(std::round(degrees))%360;
            lastPublishUs = nowUs; reason = "updated";
        }
        candidate = -1;
        return stable;
    }
    int nearest = (static_cast<int>(std::round(degrees/90))*90)%360;
    // Eight degrees beyond the 45-degree boundary, rather than the old 20.
    if (nearest == stable || std::abs(shortestRotationDelta(degrees,stable)) <= 53.f) {
        candidate = -1; return stable;
    }
    if (nearest != candidate) { candidate = nearest; since = nowUs; }
    if (nowUs-since >= 140000) { stable = candidate; candidate = -1; }
    return stable;
}
}
