// Copyright (C) 2026 Boccp
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once
#include "Travel.h"
namespace homekeys {
// Conservative correlation: commit only after a single unambiguous press/release.
// -1 means no physical key, -2 means multiple keys. No firmware assumptions.
class AutoKeyLearner {
    int key=-1,cell=-1,peak=0;
public:
    struct Result { int key; TravelCalibration range; };
    void reset() { key=cell=-1; peak=0; }
    std::optional<Result> observe(TravelFrame f,int physicalKey) {
        const int incoming=int(f.row)*256+f.column;
        if(physicalKey==-2) { reset(); return {}; }
        if(key<0) {
            if(physicalKey<0 || f.depth<10) return {};
            key=physicalKey; cell=incoming; peak=f.depth;
        }
        if((physicalKey>=0 && physicalKey!=key) || (incoming!=cell && f.depth>5)) { reset(); return {}; }
        if(incoming!=cell) return {};
        peak=std::max(peak,int(f.depth));
        if(f.depth<=3 && peak>=20 && physicalKey==-1) {
            Result result{key,{f.row,f.column,f.depth,(std::uint8_t)peak}};
            reset(); return result;
        }
        return {};
    }
};
}
