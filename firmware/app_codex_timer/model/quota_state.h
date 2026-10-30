#pragma once
#include "../controller/countdown_clock.h"
#include <cstddef>
namespace quota {
enum class Source { Unavailable, Api, Estimated, Stale };
enum class Status { WaitData, Live, Estimated, Stale, ResetPending };
struct Window {
    float used = 0;
    bool hasUsage = false;
    int64_t resetSec = 0, updatedUs = 0;
    Source source = Source::Unavailable;
    CountdownClock clock;
    float remaining() const { return std::max(0.f, std::min(100.f, 100.f - used)); }
    Status status(int64_t nowUs) const {
        if (clock.valid && clock.remaining(nowUs) == 0) return Status::ResetPending;
        if (source == Source::Estimated) return Status::Estimated;
        if (source == Source::Stale || (source == Source::Api && nowUs - updatedUs > 21600000000LL)) return Status::Stale;
        return source == Source::Api ? Status::Live : Status::WaitData;
    }
    bool dim(int64_t nowUs) const { return source == Source::Stale || nowUs - updatedUs > 900000000LL; }
};
struct Store {
    Window windows[2];
    int64_t sequence = 0;
    int64_t lastApiReset[2] = {0, 0};
    bool apply(const char* line, size_t length, int64_t receivedUs, bool resetConfirmed[2]);
};
}
