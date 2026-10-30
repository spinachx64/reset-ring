#include "watch_face.h"
#include <src/misc/cache/instance/lv_image_cache.h>
#include "../controller/orientation.h"
#include <cmath>
#include <cstdio>
#include <cstring>
LV_FONT_DECLARE(quota_mono_68);
LV_FONT_DECLARE(quota_mono_49);
LV_FONT_DECLARE(quota_mono_44);
namespace quota {
namespace {
// Match the selected web design at its default 75% preview brightness.
constexpr uint32_t green=0x5E7667, amber=0x807257;
constexpr uint32_t timeColor=0x899086;
void clean(lv_obj_t* obj) {
    lv_obj_remove_style_all(obj);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE);
}
lv_obj_t* label(lv_obj_t* parent, const lv_font_t* font) {
    auto obj=lv_label_create(parent);
    clean(obj);
    lv_obj_set_style_text_font(obj,font,0);
    lv_obj_set_style_text_color(obj,lv_color_hex(timeColor),0);
    lv_obj_set_style_text_align(obj,LV_TEXT_ALIGN_CENTER,0);
    return obj;
}
// LVGL style setters invalidate even when the value is unchanged.
void textColor(lv_obj_t* obj,lv_color_t color) {
    if (!lv_color_eq(lv_obj_get_style_text_color(obj,LV_PART_MAIN),color)) lv_obj_set_style_text_color(obj,color,0);
}
void backgroundColor(lv_obj_t* obj,lv_color_t color) {
    if (!lv_color_eq(lv_obj_get_style_bg_color(obj,LV_PART_MAIN),color)) lv_obj_set_style_bg_color(obj,color,0);
}
void opacity(lv_obj_t* obj,int value) {
    if (lv_obj_get_style_opa(obj,LV_PART_MAIN)!=value) lv_obj_set_style_opa(obj,value,0);
}
void fontStyle(lv_obj_t* obj,const lv_font_t* font) {
    if (lv_obj_get_style_text_font(obj,LV_PART_MAIN)!=font) lv_obj_set_style_text_font(obj,font,0);
}
uint32_t mix(uint32_t a, uint32_t b, float f) {
    f=std::max(0.f,std::min(1.f,f));
    uint32_t out=0;
    for (int shift: {0,8,16}) out |= uint32_t(((a>>shift)&255)*(1-f)+((b>>shift)&255)*f)<<shift;
    return out;
}
}
void WatchFace::create() {
    root=lv_obj_create(lv_screen_active()); clean(root);
    const int width=lv_display_get_horizontal_resolution(nullptr);
    const int height=lv_display_get_vertical_resolution(nullptr);
    lv_obj_set_size(root,width,height);
    lv_obj_center(root);
    lv_obj_set_style_bg_opa(root,LV_OPA_COVER,0);
    lv_obj_set_style_bg_color(root,lv_color_hex(0x070A08),0);
    buildBackdrop(width,height);
    lv_obj_add_event_cb(root,drawBackdrop,LV_EVENT_DRAW_MAIN,this);
    ring=lv_obj_create(root); clean(ring);
    lv_obj_set_size(ring,width,height); lv_obj_center(ring);
    lv_obj_add_event_cb(ring,drawRing,LV_EVENT_DRAW_MAIN,this);
    // LVGL 9.5 renders a transformed object together with its children into a layer.
    // Central layer only; outer ring stays fixed. Approx 550 KiB ARGB at 380x360.
    content=lv_obj_create(root); clean(content);
    lv_obj_set_size(content,380,360); lv_obj_center(content);
    lv_obj_set_style_transform_pivot_x(content,190,0);
    lv_obj_set_style_transform_pivot_y(content,180,0);
    percentageLayer=lv_obj_create(content); clean(percentageLayer);
    lv_obj_set_size(percentageLayer,380,78); lv_obj_set_pos(percentageLayer,0,100);
    percentage=label(percentageLayer,&quota_mono_68);
    lv_obj_set_size(percentage,130,60); lv_label_set_text(percentage,"--");
    percentageSign=label(percentageLayer,&quota_mono_44);
    lv_obj_set_size(percentageSign,30,40); lv_label_set_text(percentageSign,"%");
    softMark=lv_obj_create(content); clean(softMark);
    lv_obj_set_size(softMark,23,2); lv_obj_set_pos(softMark,178,273);
    lv_obj_set_style_radius(softMark,LV_RADIUS_CIRCLE,0);
    lv_obj_set_style_bg_opa(softMark,90,0);
    for (int i=0;i<11;++i) {
        digits[i]=label(content,&quota_mono_68);
        oldDigits[i]=label(content,&quota_mono_68);
        lv_label_set_text(digits[i],""); lv_label_set_text(oldDigits[i],"");
    }
    shown.clear();
}
void WatchFace::destroy() {
    if (root) lv_obj_delete(root);
    root=ring=content=percentage=percentageSign=percentageLayer=softMark=nullptr;
    for (int i=0;i<2;++i) {
        lv_image_cache_drop(&backdropImages[i]);
        if (backdropBuffers[i]) lv_draw_buf_destroy(backdropBuffers[i]);
        backdropBuffers[i]=nullptr;
        backdropImages[i]={};
    }
    shown.clear();
}
void WatchFace::retarget(const Window& win,int nextMode,bool reset,bool switched,int64_t now) {
    mode=nextMode;
    colorMix.target(mode?1.f:0.f,now,switched?300000:550000);
    if (switched) { switchAt=now; shown.clear(); }
    resetAt=reset?now:-2000000;
    resetTarget=win.hasUsage?win.remaining():0;
    progress.target(resetTarget,now,switched?300000:600000);
}
void WatchFace::orient(int degrees,int64_t now) {
    if (degrees==angle) return;
    float current=rotation.value(now);
    float delta=shortestRotationDelta(degrees,current);
    rotation.target(current+delta,now,260000);
    angle=degrees;
}
void WatchFace::update(const Window& win,int currentMode,int64_t now) {
    if (!root) return;
    mode=currentMode;
    dashed=win.source==Source::Estimated;
    dim=win.dim(now) || win.status(now)==Status::ResetPending;
    colorBlend=colorMix.value(now);
    color=mix(green,amber,colorBlend);
    if (dashed) color=0x59635C;
    float previous=sweep;
    sweep=win.hasUsage?progress.value(now):0;
    int64_t resetElapsed=now-resetAt;
    if (resetElapsed>=0 && resetElapsed<1300000) {
        if (resetElapsed<320000) sweep=progress.from;
        else if (resetElapsed<500000) sweep=progress.from*(1.f-float(resetElapsed-320000)/180000);
        else {
            float t=std::min(1.f,float(resetElapsed-500000)/800000);
            sweep=resetTarget*(1-(1-t)*(1-t)*(1-t));
        }
    }
    // Idle frames do not invalidate the display; seconds and active tweens do.
    static uint32_t lastColor=0; static bool lastDash=false,lastDim=false;
    if (std::abs(previous-sweep)>.001f || color!=lastColor || dashed!=lastDash || dim!=lastDim) lv_obj_invalidate(ring);
    lastColor=color; lastDash=dashed; lastDim=dim;
    char pct[12];
    if (win.hasUsage) std::snprintf(pct,sizeof(pct),"%d",int(std::round(sweep)));
    else std::snprintf(pct,sizeof(pct),"--");
    if (strcmp(lv_label_get_text(percentage),pct)) lv_label_set_text(percentage,pct);
    auto accent=lv_color_hex(color);
    auto pctColor=resetElapsed>=200000 && resetElapsed<320000 ? lv_color_hex(0xA3AA9F) : accent;
    textColor(percentage,pctColor);
    textColor(percentageSign,accent);
    backgroundColor(softMark,accent);
    // The smaller percent sign shares the number's baseline.
    int numberWidth=int(strlen(pct))*41;
    int pctLeft=(380-numberWidth-30)/2;
    lv_obj_set_size(percentage,numberWidth,60);
    lv_obj_set_pos(percentage,pctLeft,0);
    lv_obj_set_pos(percentageSign,pctLeft+numberWidth+2,17);
    const auto refreshElapsed = now-refreshAt;
    const float pulse = refreshElapsed >= 0 && refreshElapsed < 400000 ?
        std::sin(float(refreshElapsed)/400000*3.14159265f) : 0.f;
    opacity(percentageLayer,int(255-150*pulse));
    int64_t remaining=win.clock.remaining(now);
    if (resetElapsed>=0 && resetElapsed<500000) remaining=0;
    int64_t seconds=remaining/1000;
    char timeText[32];
    if (!win.clock.valid) {
        std::snprintf(timeText,sizeof(timeText),mode?"--:--:--:--":"--:--:--");
    } else {
        if (mode) std::snprintf(timeText,sizeof(timeText),"%02lld:%02lld:%02lld:%02lld",
            (long long)(seconds/86400),(long long)(seconds/3600%24),(long long)(seconds/60%60),(long long)(seconds%60));
        else std::snprintf(timeText,sizeof(timeText),"%02lld:%02lld:%02lld",
            (long long)(seconds/3600),(long long)(seconds/60%60),(long long)(seconds%60));
    }
    const lv_font_t* font=mode?&quota_mono_49:&quota_mono_68;
    int count=strlen(timeText);
    // Fixed-width cells include colons. Fit to the inscribed central area.
    int cell=std::min(mode?28:39,356/count);
    int left=(380-cell*count)/2;
    for (int i=0;i<11;++i) {
        if (i>=count) { lv_obj_add_flag(digits[i],LV_OBJ_FLAG_HIDDEN); lv_obj_add_flag(oldDigits[i],LV_OBJ_FLAG_HIDDEN); continue; }
        lv_obj_remove_flag(digits[i],LV_OBJ_FLAG_HIDDEN);
        fontStyle(digits[i],font); fontStyle(oldDigits[i],font);
        lv_obj_set_size(digits[i],cell,90); lv_obj_set_size(oldDigits[i],cell,90);
        char next[2]={timeText[i],0};
        if (i>=int(shown.size()) || shown[i]!=timeText[i]) {
            bool animate=i<int(shown.size()) && shown[i]>='0' && shown[i]<='9';
            char old[2]={animate?shown[i]:' ',0};
            lv_label_set_text(oldDigits[i],old); lv_label_set_text(digits[i],next);
            digitAt[i]=animate?now:now-180000;
        }
        float t=std::min(1.f,float(now-digitAt[i])/160000);
        int y=(mode?211:204);
        lv_obj_set_pos(digits[i],left+cell*i,y+int(9*(1-t)));
        opacity(digits[i],int(255*t));
        if (t<1) {
            lv_obj_remove_flag(oldDigits[i],LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_pos(oldDigits[i],left+cell*i,y-int(9*t));
            opacity(oldDigits[i],int(255*(1-t)));
        } else lv_obj_add_flag(oldDigits[i],LV_OBJ_FLAG_HIDDEN);
    }
    shown=timeText;
    int64_t switchElapsed=now-switchAt;
    float opacity=1;
    int slide=0;
    if (switchElapsed>=0 && switchElapsed<300000) {
        float t=float(switchElapsed)/300000;
        opacity=std::abs(2*t-1);
        slide=int(7*std::sin(t*3.14159265f));
    }
    if (lv_obj_get_style_opa(content,LV_PART_MAIN)!=int(255*opacity))
        lv_obj_set_style_opa(content,int(255*opacity),0);
    lv_obj_set_pos(content,0,slide); // Already center-aligned at construction.
    int rotate=int(std::round(rotation.value(now)*10));
    if (lv_obj_get_style_transform_rotation(content,LV_PART_MAIN)!=rotate)
        lv_obj_set_style_transform_rotation(content,rotate,0);
}
void WatchFace::buildBackdrop(int width,int height) {
    for (int i=0;i<2;++i) {
        backdropBuffers[i]=lv_draw_buf_create(width,height,LV_COLOR_FORMAT_RGB565,LV_STRIDE_AUTO);
        if (!backdropBuffers[i]) continue;
        uint32_t base=i?0x0B0907:0x070A08, center=i?0x1A1610:0x0F1813;
        for(int y=0;y<height;++y) {
            auto row=reinterpret_cast<uint16_t*>(backdropBuffers[i]->data+y*backdropBuffers[i]->header.stride);
            for(int x=0;x<width;++x) {
                float radius=std::hypot(float(x-width/2),float(y-height/2+85));
                float fade=1.f-std::min(340.f,std::max(0.f,radius))/340.f;
                uint32_t rgb=mix(base,center,fade*fade);
                // Static ordered dithering preserves subtle dark gradients in RGB565.
                static constexpr unsigned bayer[16]={0,8,2,10,12,4,14,6,3,11,1,9,15,7,13,5};
                unsigned bias=(2*bayer[(y&3)*4+(x&3)]+1)*255;
                unsigned r=(((rgb>>16)&255)*31*32+bias)/(255*32);
                unsigned g=(((rgb>>8)&255)*63*32+bias)/(255*32);
                unsigned b=((rgb&255)*31*32+bias)/(255*32);
                row[x]=uint16_t(r<<11 | g<<5 | b);
            }
        }
        lv_draw_buf_to_image(backdropBuffers[i],&backdropImages[i]);
    }
}
void WatchFace::drawBackdrop(lv_event_t* event) {
    auto self=static_cast<WatchFace*>(lv_event_get_user_data(event));
    auto layer=lv_event_get_layer(event);
    lv_area_t area; lv_obj_get_coords(self->root,&area);
    lv_draw_image_dsc_t image; lv_draw_image_dsc_init(&image);
    if (self->backdropBuffers[0]) {
        image.src=&self->backdropImages[0];
        lv_draw_image(layer,&image,&area);
    }
    if (self->backdropBuffers[1] && self->colorBlend>0) {
        image.src=&self->backdropImages[1];
        image.opa=int(255*self->colorBlend);
        lv_draw_image(layer,&image,&area);
    }
}
void WatchFace::drawRing(lv_event_t* event) {
    auto self=static_cast<WatchFace*>(lv_event_get_user_data(event));
    auto layer=lv_event_get_layer(event);
    lv_area_t area; lv_obj_get_coords(self->ring,&area);
    lv_draw_arc_dsc_t arc; lv_draw_arc_dsc_init(&arc);
    arc.center.x=(area.x1+area.x2)/2;
    arc.center.y=(area.y1+area.y2)/2;
    arc.radius=206; arc.width=5; arc.rounded=1;
    arc.color=lv_color_hex(mix(0x1B251F,0x282219,self->colorBlend)); arc.start_angle=0; arc.end_angle=360;
    lv_draw_arc(layer,&arc);
    arc.color=lv_color_hex(self->color); arc.opa=self->dim?90:255;
    if (self->dashed) {
        arc.width=4; arc.rounded=0;
        for (int i=0;i<360;i+=15) { arc.start_angle=i; arc.end_angle=i+7; lv_draw_arc(layer,&arc); }
    } else if (self->sweep>0) {
        // Exactly one continuous clockwise arc, anchored at 12 o'clock.
        arc.start_angle=270; arc.end_angle=270+360.f*self->sweep/100.f;
        // A faint halo follows the same arc, without another bright segment.
        lv_draw_arc_dsc_t halo=arc;
        halo.width=13; halo.opa=self->dim?4:9; lv_draw_arc(layer,&halo);
        halo.width=9; halo.opa=self->dim?6:14; lv_draw_arc(layer,&halo);
        lv_draw_arc(layer,&arc);
    }
}
}
