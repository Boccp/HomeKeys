// Copyright (C) 2026 Boccp
// SPDX-License-Identifier: AGPL-3.0-only

#include "Core.h"
#include <cstdlib>
#include <iostream>
#include <thread>
#include <vector>
#include <chrono>
using namespace homekeys;
void check(bool value, const char* message) {
    if (!value) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
int main() {
    std::array<bool,128> pianoNotes{};
    std::array<bool,256> pianoKeys{};
    for(auto b:pianoLayout) {
        check(!pianoKeys[b.key],"two-deck physical keys unique");
        pianoKeys[b.key]=true; pianoNotes[b.note]=true;
    }
    for(int n=48;n<=79;++n) check(pianoNotes[n],"two-deck range has no semitone gap");
    auto pianoNote=[](int k){for(auto b:pianoLayout)if(b.key==k)return b.note;return -1;};
    check(pianoNote('Z')==48 && pianoNote('S')==49 && pianoNote('X')==50,"lower piano fingering");
    check(pianoNote('Q')==60 && pianoNote('2')==61 && pianoNote('W')==62,"upper piano fingering");
    check(pianoNote(']')==79 && pianoNote('=')==78 && pianoNote('/')==64,"extended punctuation keys");
    std::array<bool,128> covered{};
    for(auto b:maxLayout) { covered[b.note]=true; if(blackAbove(b.note)>=0) covered[blackAbove(b.note)]=true; }
    int coverage=0; for(bool b:covered) coverage+=b;
    check(coverage==61 && maxLayout.front().note==36 && maxLayout.back().note==96,"MAX layout spans all 61 notes C2-C7");
    for(int n=36;n<=96;++n) check(covered[n],"MAX has no missing semitone");
    check(blackAbove(40)==-1 && blackAbove(47)==-1,"no imaginary black keys above E and B");
    SpscQueue<int, 4> queue;
    int result=0;
    check(queue.push(1) && queue.push(2) && queue.push(3) && !queue.push(4), "bounded queue");
    check(queue.pop(result) && result == 1 && queue.push(4), "queue wrap");
    for (int expected=2; expected<=4; ++expected) check(queue.pop(result) && result==expected, "FIFO");
    check(!queue.pop(result), "empty queue");
    SpscQueue<int, 128> concurrent;
    std::thread producer([&] { for (int i=0; i<100000; ++i) while (!concurrent.push(i)) std::this_thread::yield(); });
    for (int i=0; i<100000; ++i) { while (!concurrent.pop(result)) std::this_thread::yield(); check(result==i, "concurrent FIFO"); }
    producer.join();
    InputState input;
    std::vector<Event> recorded;
    auto emit = [&](Event e) { recorded.push_back(e); };
    input.down('L', 64, .7f, emit);
    input.down('L', 76, .7f, emit);
    input.up('L', emit);
    input.up('L', emit);
    check(recorded.size()==2 && recorded[1].note==64, "repeat suppression and original note-off");
    for (size_t i=0; i<homeLayout.size(); ++i)
        for (size_t j=i+1; j<homeLayout.size(); ++j)
            check(homeLayout[i].key!=homeLayout[j].key, "unique home bindings");
    auto mapped = [](int key) { for (auto b : homeLayout) if (b.key == key) return b.note; return -1; };
    for (const char* row : {"QWERTYUIOP[", "ASDFGHJKL;", "ZXCVBNM,."}) {
        for (int i=0; row[i]; ++i) {
            check(mapped(row[i]) >= 0, "full grid has no unmapped key");
            if (i) check(mapped(row[i])-mapped(row[i-1])==1, "horizontal semitone");
        }
    }
    const char* bottom = "ZXCVBNM,.";
    const char* middle = "ASDFGHJKL;";
    const char* top = "QWERTYUIOP[";
    for (int i=0; i<9; ++i) check(mapped(middle[i])-mapped(bottom[i])==5, "lower fourth");
    for (int i=0; i<10; ++i) check(mapped(top[i])-mapped(middle[i])==5, "upper fourth");
    Engine engine;
    engine.prepare(48000);
    std::array<float, 256> samples{};
    float* output = samples.data();
    engine.event({EventType::on, 1, 60, .8f});
    engine.render(&output, 1, 256);
    bool audible=false;
    for (float sample : samples) { check(std::isfinite(sample) && std::abs(sample)<1, "finite bounded audio"); audible |= std::abs(sample)>.001f; }
    check(audible, "note generates audio");
    engine.event({EventType::pedal, 0, 0, 1});
    engine.event({EventType::off, 1, 60, 0});
    engine.render(&output, 1, 256);
    check(engine.voiceCount()==1, "pedal holds released key");
    engine.event({EventType::pedal, 0, 0, 0});
    for (int i=0; i<300; ++i) { samples.fill(0); engine.render(&output, 1, 256); }
    check(engine.voiceCount()==0, "pedal release decays to silence");
    for (int i=0; i<100; ++i) engine.event({EventType::on, i, 48+i%36, .7f});
    check(engine.voiceCount()==64, "voice capacity");
    const auto start = std::chrono::steady_clock::now();
    for (int i=0; i<1000; ++i) { samples.fill(0); engine.render(&output, 1, 256); }
    const auto ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now()-start).count();
    engine.panic();
    samples.fill(0);
    engine.render(&output, 1, 256);
    for (float sample : samples) check(sample==0, "panic silence");
    check(engine.voiceCount()==0, "panic clears voices");
    std::cout << "Core checks passed. Offline workload: " << ms << " ms / 5333 ms audio. This is not device latency.\n";
}
