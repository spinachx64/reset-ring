#include "app_codex_timer.h"
#include "ble/ble_nus.h"
#include <hal/hal.h>
#include <assets/assets.h>
#include <mooncake_log.h>
#include <esp_timer.h>
#include <esp_heap_caps.h>
#include <cstring>
LV_IMAGE_DECLARE(icon_codex);
QuotaWatchApp::QuotaWatchApp() {
    setAppInfo().name="Codex StopWatch";
    setAppInfo().icon=(void*)&icon_codex;
}
void QuotaWatchApp::onCreate() {
    ble_nus::start("Codex Watch");
    modes.load();
}
void QuotaWatchApp::onOpen() {
    keys=std::make_unique<input::KeyManager>();
    orientationLogUntil=esp_timer_get_time()+30000000;
    lastOrientationLog=0;
    LvglLockGuard lock;
    face.create();
    face.retarget(store.windows[modes.mode()],modes.mode(),false,false,esp_timer_get_time());
    mclog::tagInfo("quota", "display {}x{}, PSRAM free {}",lv_display_get_horizontal_resolution(nullptr),
        lv_display_get_vertical_resolution(nullptr),heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
}
void QuotaWatchApp::refresh(int64_t now) {
    if (now-lastRefresh>=5000000 && ble_nus::connected()) { ble_nus::request_refresh(); lastRefresh=now; }
}
void QuotaWatchApp::onRunning() {
    const auto now=esp_timer_get_time();
    auto event=keys->update();
    if (event==input::KeyEvent::GoHome) { close(); return; }
    bool switched=event==input::KeyEvent::GoNext;
    if (switched) { modes.toggle(); GetHAL().vibrate(30,60); }
    if (event==input::KeyEvent::GoPrevious) {
        refresh(now);
        GetHAL().vibrate(30,60);
        LvglLockGuard lock;
        face.refreshFeedback(now);
        mclog::tagInfo("quota", "manual refresh requested");
    }
    bool connected=ble_nus::connected();
    if (connected!=wasConnected) {
        mclog::tagInfo("quota", "BLE {}",connected?"connected":"disconnected");
        wasConnected=connected;
    }
    bool resets[2]={false,false}, changed=false;
    char line[256];
    int64_t receivedUs=now;
    if (ble_nus::poll_line(line,sizeof(line),&receivedUs)) {
        changed=store.apply(line,strlen(line),receivedUs,resets);
        if (changed) mclog::tagInfo("quota", "accepted q={}, h={}, w={}",store.sequence,
            store.windows[0].hasUsage?store.windows[0].remaining():-1,
            store.windows[1].hasUsage?store.windows[1].remaining():-1);
        else mclog::tagWarn("quota", "rejected invalid or old packet");
    }
    auto& win=store.windows[modes.mode()];
    if (win.status(now)==quota::Status::ResetPending) refresh(now);
    int direction=orientation.angle();
    if (now-lastImu>=25000) {
        const auto sampleGap=now-lastImu;
        lastImu=now; GetHAL().updateImuData(); auto data=GetHAL().getImuData();
        direction=orientation.update(data.accelX,data.accelY,data.accelZ,now);
        if (now<orientationLogUntil && now-lastOrientationLog>=1000000) {
            lastOrientationLog=now;
            mclog::tagInfo("orientation", "x={:.3f} y={:.3f} z={:.3f} gap={}ms out={} state={} stable={}ms",
                data.accelX,data.accelY,data.accelZ,sampleGap/1000,direction,
                orientation.holdReason(),orientation.settledForUs(now)/1000);
        }
    }
    if (now-lastDraw>=25000 || changed || switched) {
        lastDraw=now;
        LvglLockGuard lock;
        if (changed || switched) face.retarget(win,modes.mode(),resets[modes.mode()],switched,now);
        face.orient(direction,now);
        face.update(win,modes.mode(),now);
    }
}
void QuotaWatchApp::onClose() {
    keys.reset();
    LvglLockGuard lock;
    face.destroy();
}
