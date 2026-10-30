#include "controller/countdown_clock.h"
#include "controller/orientation.h"
#include "model/quota_state.h"
#include <cassert>
#include <cstring>
#include <iostream>
using namespace quota;
int main() {
    CountdownClock c;
    assert(!c.valid);
    c.anchor(1800000001,1800000000123LL,5000000);
    assert(c.remaining(5000000)==877);
    assert(c.remaining(5421000)==456);
    assert(c.remaining(99999999)==0);
    c.anchor(1801000000,1800000000000LL,1);
    assert(c.remaining(1)==1000000000LL); // int64, no accumulated frame drift
    Store store; bool reset[2];
    const char* good=R"({"v":1,"q":42,"t":1793360000123,"h":{"u":25,"r":1793368000,"s":"api"},"w":null})";
    assert(store.apply(good,strlen(good),5000000,reset));
    assert(store.windows[0].remaining()==75);
    assert(!store.windows[1].hasUsage);
    assert(!reset[0]);
    // A packet queued while the app is closed retains its BLE receive anchor.
    const auto initial = store.windows[0].clock.remaining(5000000);
    assert(store.windows[0].clock.remaining(8000000)==initial-3000);
    assert(store.windows[0].dim(5000000+901000000));
    assert(!store.apply(good,strlen(good),6000000,reset)); // duplicate
    const char* next=R"({"v":1,"q":43,"t":1793368001123,"h":{"u":30,"r":1793386000,"s":"api"},"w":null})";
    assert(store.apply(next,strlen(next),7000000,reset));
    assert(reset[0] && store.windows[0].remaining()==70); // never assume 100 on reset
    for (const char* bad: {
        R"({"v":2,"q":44,"t":1,"h":null,"w":null})",
        R"({"v":1,"q":44.5,"t":1,"h":null,"w":null})",
        R"({"v":1,"q":44,"t":1,"h":{"u":null,"r":1,"s":"api"},"w":null})",
        R"({"v":1,"q":44,"t":1,"h":{"u":101,"r":1,"s":"api"},"w":null})",
        R"({"v":1,"q":44,"t":1,"h":{"u":0,"r":1.5,"s":"api"},"w":null})",
        R"({"v":1,"q":44,"t":1,"h":null})",
        R"({"v":1,"q":44,"t":1,"h":null,"w":null} garbage)"}) {
        assert(!store.apply(bad,strlen(bad),8000000,reset));
        assert(store.sequence==43);
    }
    const char* estimate=R"({"v":1,"q":44,"t":1793360000123,"h":{"u":null,"r":1793378000,"s":"estimated"},"w":null})";
    assert(store.apply(estimate,strlen(estimate),9000000,reset));
    assert(!reset[0] && !store.windows[0].hasUsage);
    assert(store.windows[0].status(9000000)==Status::Estimated);
    const char* zero=R"({"v":1,"q":45,"t":1793360000123,"h":{"u":0,"r":null,"s":"api"},"w":null})";
    assert(store.apply(zero,strlen(zero),10000000,reset));
    assert(store.windows[0].hasUsage && store.windows[0].remaining()==100);
    assert(!store.windows[0].clock.valid);
    const char* missing=R"({"v":1,"q":46,"t":1793360000123,"h":null,"w":null})";
    assert(store.apply(missing,strlen(missing),11000000,reset));
    assert(!store.windows[0].hasUsage && !store.windows[0].clock.valid);
    const char* recovered=R"({"v":1,"q":47,"t":1793360000123,"h":{"u":28,"r":1793404000,"s":"api"},"w":null})";
    assert(store.apply(recovered,strlen(recovered),12000000,reset));
    assert(reset[0] && store.windows[0].remaining()==72); // Survives missing/stale intervals.
    // Repeated full turns must keep using the shortest path in either direction.
    for (int turns=-10;turns<=10;++turns) {
        for (int target: {0,90,180,270}) {
            float current=turns*360.f+17.f;
            float delta=shortestRotationDelta(target,current);
            assert(std::abs(delta)<=180.f);
            assert(std::abs(std::remainder(current+delta-target,360.f))<.001f);
        }
    }
    assert(shortestRotationDelta(1,359)==2);
    assert(shortestRotationDelta(359,1)==-2);
    Orientation calibrated(180);
    assert(calibrated.angle()==0); // Unknown/flat at boot retains readable default.
    for(int i=0;i<12;++i) calibrated.update(0,-1,0,i*50000);
    assert(calibrated.angle()==0); // Real panel offset: -Y is upright.
    for(int i=0;i<12;++i) calibrated.update(0,1,0,1000000+i*50000);
    assert(calibrated.angle()==180);
    // Horizontal gravity must rotate toward the lower physical edge, not its opposite.
    for(int i=0;i<20;++i) calibrated.update(1,0,0,2000000+i*50000);
    assert(calibrated.angle()==90);
    for(int i=0;i<20;++i) calibrated.update(-1,0,0,4000000+i*50000);
    assert(calibrated.angle()==270);
    Orientation o;
    for(int i=0;i<12;++i) o.update(1,0,0,i*50000);
    assert(o.angle()==270);
    for(int i=0;i<12;++i) o.update(0,0,1,1000000+i*50000);
    assert(o.angle()==270); // flat retains last orientation
    for(int i=0;i<12;++i) o.update(2,0,0,2000000+i*50000);
    assert(o.angle()==270); // shaking retains last orientation
    for(int i=0;i<20;++i) o.update(0,-1,0,3000000+i*50000);
    assert(o.angle()==180);
    for(int i=0;i<20;++i) o.update(-1,0,0,5000000+i*50000);
    assert(o.angle()==90);
    for(int i=0;i<20;++i) o.update(0,1,0,7000000+i*50000);
    assert(o.angle()==0);
    Orientation continuous(180,OrientationTracking::Continuous);
    int64_t sampleUs=100000;
    auto pose = [&](float angle,float planar=1.f,int samples=40) {
        float radians=(angle-180.f)*3.14159265f/180.f;
        float z=std::sqrt(std::max(0.f,1.f-planar*planar));
        for (int i=0;i<samples;++i) {
            sampleUs+=25000;
            continuous.update(-std::sin(radians)*planar,std::cos(radians)*planar,z,sampleUs);
        }
        return continuous.angle();
    };
    for (float angle: {0.f,22.f,47.f,73.f,90.f,130.f,180.f,225.f,270.f,315.f,359.f,1.f})
        assert(std::abs(shortestRotationDelta(angle,pose(angle)))<=3.f);
    int held=continuous.angle();
    for (int i=0;i<12;++i) continuous.update(0,0,1,sampleUs+=25000);
    assert(continuous.angle()==held);
    // Still track when tilted only ~27 degrees above the desk, formerly locked flat.
    assert(std::abs(shortestRotationDelta(73,pose(73,.45f)))<=2.f);
    held=continuous.angle();
    for (int i=0;i<10;++i) continuous.update(2,0,0,sampleUs+=25000);
    assert(continuous.angle()==held);
    // Flat-lock hysteresis: weak planar gravity cannot acquire a new direction.
    pose(130,.2f);
    assert(continuous.angle()==held);
    pose(130,.33f);
    assert(continuous.angle()==held);
    assert(std::abs(shortestRotationDelta(130,pose(130,.45f)))<=2.f);
    // Rapid movement cannot keep retargeting the UI; publish only after settling.
    pose(0);
    for (int angle=10;angle<=160;angle+=10) {
        pose(angle,1.f,1);
        assert(continuous.angle()==0);
    }
    pose(167,1.f,8); // 200ms is still below the stable hold time.
    assert(continuous.angle()==0);
    pose(167,1.f,20);
    assert(std::abs(shortestRotationDelta(167,continuous.angle()))<=3.f);
    held=continuous.angle();
    for (int i=0;i<40;++i) pose(167+(i%2?2.f:-2.f),1.f,1);
    assert(continuous.angle()==held); // Hand jitter does not chase the text.
    // Small smooth changes still track arbitrary angles, at a controlled cadence.
    Orientation paced(180,OrientationTracking::Continuous);
    int64_t lastChange=-450000;
    int previous=0,changes=0;
    for (int i=0;i<240;++i) {
        float degrees=i*.15f;
        float radians=(degrees-180.f)*3.14159265f/180.f;
        int64_t at=25000LL*(i+1);
        int output=paced.update(-std::sin(radians),std::cos(radians),0,at);
        if (output!=previous) {
            assert(at-lastChange>=450000);
            previous=output; lastChange=at; ++changes;
        }
    }
    assert(changes>=2);
    // Realistic sampling must not permanently starve the stability gate.
    auto recovery = [](int gapUs,float magnitude,float jitter) {
        Orientation sensor(180,OrientationTracking::Continuous);
        for (int i=0;i<36;++i) {
            float desired=90.f+(i%2?jitter:-jitter);
            float radians=(desired-180.f)*3.14159265f/180.f;
            sensor.update(-std::sin(radians)*magnitude,std::cos(radians)*magnitude,0,
                          int64_t(i+1)*gapUs);
        }
        assert(std::abs(shortestRotationDelta(90,sensor.angle()))<=3.f);
    };
    recovery(25000,1.f,1.4f); // Raw sample speed looks high despite small stationary jitter.
    recovery(400000,1.f,0.f);
    recovery(200000,1.f,0.f); // GUI scheduling gaps must not reset the gate forever.
    recovery(25000,.83f,0.f); // Use the accepted gravity range, not a second tighter veto.
    std::cout<<"clock, parser, reset and orientation checks passed\n";
}
