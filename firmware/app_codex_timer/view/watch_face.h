#pragma once
#include "../model/quota_state.h"
#include "animations.h"
#include <lvgl.h>
#include <string>
namespace quota {
class WatchFace {
public:
    void create();
    void destroy();
    void update(const Window& win, int mode, int64_t nowUs);
    void retarget(const Window& win, int mode, bool reset, bool switched, int64_t nowUs);
    void refreshFeedback(int64_t nowUs) { refreshAt = nowUs; lastActivityAt = nowUs; }
    void orient(int degrees, int64_t nowUs);
private:
    lv_obj_t *root=nullptr, *foreground=nullptr, *ring=nullptr, *content=nullptr, *percentage=nullptr, *percentageSign=nullptr, *percentageLayer=nullptr, *softMark=nullptr;
    lv_obj_t *digits[11]{}, *oldDigits[11]{};
    lv_draw_buf_t* backdropBuffers[2]{};
    lv_image_dsc_t backdropImages[2]{};
    void buildBackdrop(int width,int height);
    std::string shown;
    Tween progress, colorMix, rotation;
    int mode=0, angle=0;
    bool dashed=false, dim=false;
    float sweep=0, colorBlend=0;
    uint32_t color=0x5E7667;
    int64_t refreshAt=-1000000, switchAt=-1000000, resetAt=-2000000, digitAt[11]{};
    float resetTarget=0;
    int64_t protectionStartedAt=-1, lastActivityAt=0;
    static void drawBackdrop(lv_event_t* event);
    static void drawRing(lv_event_t* event);
};
}
