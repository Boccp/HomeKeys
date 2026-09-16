// Copyright (C) 2026 Boccp
// SPDX-License-Identifier: AGPL-3.0-only

#include "Travel.h"
#include "AutoKeyLearner.h"
#include <cstdlib>
#include <iostream>
using namespace homekeys;
void require(bool ok, const char* label) {
    if (!ok) { std::cerr << "FAIL: " << label << '\n'; std::exit(1); }
}
int main() {
    AutoKeyLearner learner;
    require(!learner.observe({3,5,10},'F'),"learning waits for release");
    learner.observe({3,5,38},'F');
    auto learned=learner.observe({3,5,0},-1);
    require(learned && learned->key=='F' && learned->range.peak==38,"automatic mapping from single key");
    learner.observe({3,5,20},'F'); learner.observe({3,6,30},-2);
    require(!learner.observe({3,5,0},-1),"ambiguous chord cannot teach mapping");
    learner.observe({3,5,20},'F'); learner.observe({3,6,30},'F');
    require(!learner.observe({3,5,0},-1),"unrelated matrix activity cancels learning");
    std::array<std::uint8_t,64> packet{};
    packet[0]=1; packet[1]=0x21; packet[5]=3; packet[7]=2; packet[8]=7; packet[9]=20;
    require(parseTravel(packet).has_value(), "travel packet");
    for (unsigned n=0;n<64;++n) require(!parseTravel({packet.data(),n}), "truncated packets");
    packet[9]=255; require(!parseTravel(packet), "out of range depth");
    packet[9]=20; packet[5]=1; require(!parseTravel(packet), "ignore acknowledgement");
    TravelCalibrator cal;
    require(cal.begin({2,7,1}), "rest reference");
    cal.observe({2,7,39}); require(!cal.finish(), "release required");
    cal.observe({2,7,1}); auto c=cal.finish();
    require(c && c->normalise(39)==1 && c->normalise(0)==0, "calibrated range");
    cal.observe({3,4,20}); require(!cal.finish(), "reject ambiguous two-key calibration");
    require(!cal.begin({2,7,20}), "reject depressed rest reference");
    const TravelCalibration standard{2,7,0,40};
    auto strike = [&](std::uint64_t duration) {
        TravelTracker t(standard); t.observe({2,7,0},1000);
        auto on=t.observe({2,7,24},1000+duration);
        require(on && on->noteOn && on->estimatedVelocity, "interpolated strike");
        return *on->estimatedVelocity;
    };
    require(strike(10000)>strike(50000), "fast strike louder than slow strike");
    TravelTracker t(standard);
    auto first=t.observe({2,7,30},1000);
    require(first && first->noteOn && !first->estimatedVelocity, "no invented velocity without history");
    auto held=t.observe({2,7,29},2000000);
    require(held && !held->noteOn && !held->noteOff, "event silence preserves held state");
    require(!t.observe({2,7,0},1999999), "ignore old timestamp");
    require(!t.observe({2,8,0},2000001), "ignore other key");
    require(t.observe({2,7,4},2000002)->noteOff, "release hysteresis");
    t.observe({2,7,35},2001000);
    require(t.reset() && !t.reset(), "disconnect releases once");
    TravelTracker gap(standard); gap.observe({2,7,0},1000);
    require(!gap.observe({2,7,30},1000000)->estimatedVelocity, "gap invalidates speed");
    TravelTracker resting(standard); resting.observe({2,7,0},1000);
    resting.observe({2,7,10},1000000);
    auto afterRest=resting.observe({2,7,24},1010000);
    require(afterRest->estimatedVelocity.has_value(), "moving segment after long idle");
    std::cout << "Travel parser, calibration and estimated velocity checks passed (synthetic data only).\n";
}
