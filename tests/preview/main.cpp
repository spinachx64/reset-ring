#include "../../firmware/app_codex_timer/view/watch_face.h"
#include <lvgl.h>
#include <cstdio>
#include <vector>
#include <string>
void flush(lv_display_t* display,const lv_area_t*,uint8_t*) { lv_display_flush_ready(display); }
void save(const char* name) {
    lv_obj_update_layout(lv_screen_active());
    auto image=lv_snapshot_take(lv_screen_active(),LV_COLOR_FORMAT_RGB888);
    if (!image) { std::fprintf(stderr,"snapshot failed\n"); std::exit(1); }
    FILE* out=std::fopen(name,"wb");
    std::fprintf(out,"P6\n%u %u\n255\n",image->header.w,image->header.h);
    for (unsigned y=0;y<image->header.h;y++) {
        auto row=image->data+y*image->header.stride;
        for(unsigned x=0;x<image->header.w;x++) {
            uint8_t rgb[]={row[x*3+2],row[x*3+1],row[x*3]};
            std::fwrite(rgb,1,3,out);
        }
    }
    std::fclose(out); lv_draw_buf_destroy(image);
}
int main(int argc,char** argv) {
    lv_init();
    auto display=lv_display_create(466,466);
    std::vector<uint8_t> buffer(466*466*2);
    lv_display_set_buffers(display,buffer.data(),nullptr,buffer.size(),LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display,flush);
    quota::WatchFace face; face.create(); quota::Window win;
    int64_t start=1000000;
    win.hasUsage=true; win.used=25; win.resetSec=1800000000; win.source=quota::Source::Api; win.updatedUs=start;
    win.clock.anchor(1800000000,1799986934123LL,start);
    face.retarget(win,0,false,false,start);
    face.update(win,0,start+650000);
    save(".state/five-hour.ppm");
    win.used=65;
    win.clock.anchor(1800000000,1799671934123LL,start);
    face.retarget(win,1,false,true,start+1000000);
    face.update(win,1,start+1700000);
    save(".state/weekly.ppm");
    face.orient(90,start+2000000); face.update(win,1,start+2300000);
    save(".state/rotated.ppm");
    win.hasUsage=false; win.source=quota::Source::Estimated;
    face.retarget(win,0,false,true,start+3000000); face.update(win,0,start+3500000);
    save(".state/estimated.ppm");
    face.orient(0,start+4000000);
    win.hasUsage=true; win.source=quota::Source::Api; win.used=0;
    win.clock.anchor(1800000000,1799986934123LL,start);
    face.retarget(win,0,false,true,start+4000000); face.update(win,0,start+4700000);
    save(".state/full.ppm");
    win.used=100;
    face.retarget(win,0,false,false,start+5000000); face.update(win,0,start+5700000);
    save(".state/empty.ppm");
    win=quota::Window{};
    face.retarget(win,0,false,false,start+6000000); face.update(win,0,start+6700000);
    face.update(win,0,start+6900000);
    save(".state/wait-data.ppm");
    win.hasUsage=true; win.used=25; win.source=quota::Source::Api;
    win.clock.anchor(1800000000,1799986934123LL,start);
    face.retarget(win,0,false,false,start+8000000);
    int index=0;
    for (int angle: {22,47,73,130,225,315}) {
        int64_t at=start+9000000+index++*1000000;
        face.orient(angle,at); face.update(win,0,at+250000);
        char path[80]; std::snprintf(path,sizeof(path),".state/rotation-%d.ppm",angle);
        save(path);
    }
    face.destroy();
    return 0;
}
