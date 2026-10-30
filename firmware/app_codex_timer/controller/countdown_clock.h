#pragma once
#include <algorithm>
#include <cstdint>
namespace quota {
struct CountdownClock {
    int64_t remainingAtAnchor = 0, anchorUs = 0;
    bool valid = false;
    void anchor(int64_t resetSec, int64_t sentMs, int64_t receivedUs) {
        valid = resetSec > 0;
        remainingAtAnchor = valid ? std::max<int64_t>(0, resetSec * 1000 - sentMs) : 0;
        anchorUs = receivedUs;
    }
    int64_t remaining(int64_t nowUs) const {
        return std::max<int64_t>(0, remainingAtAnchor - std::max<int64_t>(0, nowUs - anchorUs) / 1000);
    }
};
}
