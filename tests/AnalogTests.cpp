// Copyright (C) 2026 Boccp
// SPDX-License-Identifier: AGPL-3.0-only

#include "AnalogInput.h"
#include <iostream>
#include <cstdlib>
namespace homekeys {
struct AnalogInputTest {
    static void check(bool b) { if(!b) std::exit(1); }
    static void run() {
        {
            AnalogInput uncalibrated;
            unsigned gen=0;
            uncalibrated.process({1,1,0},1000,gen);
            uncalibrated.process({1,1,10},5000,gen);
            uncalibrated.process({1,1,38},25000,gen);
            check(uncalibrated.monitorVelocity>0 && uncalibrated.monitorEstimated);
            AnalogEvent unused;
            check(!uncalibrated.audio.pop(unused) && uncalibrated.lastVelocity==0);
        }
        AnalogInput a;
        // Use the user's observed full travel (38), without touching the HID.
        auto strike38=[](std::uint64_t duration) {
            AnalogInput measured; measured.bind('F',{1,2,0,38});
            measured.notes['F']=60; measured.enabled=true; unsigned gen=0;
            measured.process({1,2,0},1000,gen);
            measured.process({1,2,8},2000,gen);
            measured.process({1,2,24},2000+duration,gen);
            AnalogEvent event;
            check(measured.audio.pop(event) && event.event.type==EventType::on);
            check(measured.lastEstimated && measured.lastVelocity>0);
            Engine synth; synth.prepare(48000); synth.event(event.event);
            std::array<float,256> samples{}; float* output=samples.data();
            double energy=0;
            for(int block=0;block<100;++block) {
                samples.fill(0); synth.render(&output,1,256);
                for(float sample:samples) energy+=double(sample)*sample;
            }
            return std::sqrt(energy/25600);
        };
        const double fastRms=strike38(5000), slowRms=strike38(50000);
        check(fastRms>2*slowRms);
        std::cout<<"Synthetic travel -> audio RMS: slow="<<slowRms<<", fast="<<fastRms
                 <<", difference="<<20*std::log10(fastRms/slowRms)<<" dB\n";
        check(a.bind('Q',{2,7,0,40}));
        check(!a.bind('W',{2,7,0,40}));
        a.notes['Q']=60; a.enabled=true;
        unsigned generation=a.generation;
        a.process({2,7,0},1000,generation);
        a.process({2,7,10},2000,generation);
        a.process({2,7,24},12000,generation);
        AnalogEvent e;
        check(a.audio.pop(e) && e.event.type==EventType::on && e.event.note==60 && e.event.value>0);
        a.notes['Q']=72;
        a.process({2,7,0},15000,generation);
        check(a.audio.pop(e) && e.event.type==EventType::off && e.event.note==60);
        a.mute(); a.enabled=false;
        a.process({2,7,30},16000,generation);
        check(!a.audio.pop(e));
        check(a.panic.load());
        a.enabled=true; a.process({2,7,0},18000,generation);
        a.process({2,7,10},19000,generation); a.process({2,7,24},25000,generation);
        check(a.audio.pop(e) && e.event.note==72 && e.generation==a.generation.load());
        check(a.reports==8);
        std::cout<<"Analog routing, calibration collision, original release, focus suppression and generation checks passed; no hardware writes.\n";
    }
};
}
int main(){homekeys::AnalogInputTest::run();}
