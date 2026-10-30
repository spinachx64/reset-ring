#pragma once
#include <apps/common/key_manager/key_manager.h>
#include <mooncake.h>
#include <memory>
#include "model/quota_state.h"
#include "controller/mode_controller.h"
#include "controller/orientation.h"
#include "view/watch_face.h"
class QuotaWatchApp : public mooncake::AppAbility {
public:
    QuotaWatchApp();
    void onCreate() override;
    void onOpen() override;
    void onRunning() override;
    void onClose() override;
private:
    std::unique_ptr<input::KeyManager> keys;
    quota::Store store;
    quota::ModeController modes;
    quota::Orientation orientation{180, quota::OrientationTracking::Continuous}; // Verified BMI270-to-panel mounting offset.
    quota::WatchFace face;
    int64_t lastDraw=0,lastImu=0,lastRefresh=-5000000;
    bool wasConnected=false;
    void refresh(int64_t now);
};
