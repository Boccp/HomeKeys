// Copyright (C) 2026 Boccp
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once
#include <array>
#include <atomic>
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace homekeys {
// Exactly one producer (UI) and one consumer (audio callback).
template<class T, unsigned N> class SpscQueue {
    static_assert(N > 1);
    static_assert(std::atomic<unsigned>::is_always_lock_free);
    std::array<T, N> data{};
    alignas(64) std::atomic<unsigned> write{0};
    alignas(64) std::atomic<unsigned> read{0};
public:
    bool push(const T& value) noexcept {
        const auto w = write.load(std::memory_order_relaxed);
        const auto next = (w + 1) % N;
        if (next == read.load(std::memory_order_acquire)) return false;
        data[w] = value;
        write.store(next, std::memory_order_release);
        return true;
    }
    bool pop(T& value) noexcept {
        const auto r = read.load(std::memory_order_relaxed);
        if (r == write.load(std::memory_order_acquire)) return false;
        value = data[r];
        read.store((r + 1) % N, std::memory_order_release);
        return true;
    }
};
enum class EventType { on, off, pedal, panic };
struct Event { EventType type{}; int id{}; int note{}; float value{}; };
struct Binding { int key; int note; };
// Familiar two-deck computer piano ordering, as used by Songtive.
// Lower deck begins C3, upper deck C4; overlapping notes retain familiar fingering.
inline constexpr auto pianoLayout = [] {
    constexpr char lower[]="ZSXDCVGBHNJM,L.;/";
    constexpr char upper[]="Q2W3ER5T6Y7UI9O0P[=]";
    std::array<Binding,37> result{};
    for(int i=0;i<17;++i) result[i]={lower[i],48+i};
    for(int i=0;i<20;++i) result[17+i]={upper[i],60+i};
    return result;
}();
// Conventional virtual-piano MAX layout: 36 white keys C2..C7.
inline constexpr auto maxLayout = [] {
    constexpr char keys[]="1234567890QWERTYUIOPASDFGHJKLZXCVBNM";
    constexpr int scale[]{0,2,4,5,7,9,11};
    std::array<Binding,36> result{};
    for(int i=0;i<36;++i) result[i]={keys[i],36+12*(i/7)+scale[i%7]};
    return result;
}();
inline constexpr int blackAbove(int note) {
    const int pc=note%12;
    return note<96 && (pc==0 || pc==2 || pc==5 || pc==7 || pc==9) ? note+1 : -1;
}
// Chromatic fourths grid inspired by Ableton Push: +1 horizontally, +5 upward.
// Rows are indexed by physical key order; QWERTY rows are staggered, not a square pad grid.
inline constexpr std::array<Binding, 30> homeLayout{{
    {'Q',58},{'W',59},{'E',60},{'R',61},{'T',62},{'Y',63},{'U',64},{'I',65},{'O',66},{'P',67},{'[',68},
    {'A',53},{'S',54},{'D',55},{'F',56},{'G',57},{'H',58},{'J',59},{'K',60},{'L',61},{';',62},
    {'Z',48},{'X',49},{'C',50},{'V',51},{'B',52},{'N',53},{'M',54},{',',55},{'.',56}
}};
inline constexpr std::array<Binding, 17> classicLayout{{
    {'A',60},{'W',61},{'S',62},{'E',63},{'D',64},{'F',65},
    {'T',66},{'G',67},{'Y',68},{'H',69},{'U',70},{'J',71},
    {'K',72},{'O',73},{'L',74},{'P',75},{';',76}
}};

// UI-thread-only key ownership. Note-off retains the note chosen at key-down.
class InputState {
    std::array<int, 256> active;
public:
    InputState() { reset(); }
    void reset() noexcept { active.fill(-1); }
    template<class Emit> void down(int key, int note, float velocity, Emit emit) {
        if (key < 0 || key >= 256 || active[key] >= 0) return;
        active[key] = std::clamp(note, 0, 127);
        emit(Event{EventType::on, key, active[key], velocity});
    }
    template<class Emit> void up(int key, Emit emit) {
        if (key < 0 || key >= 256 || active[key] < 0) return;
        emit(Event{EventType::off, key, active[key], 0});
        active[key] = -1;
    }
    bool held(int key) const noexcept { return key >= 0 && key < 256 && active[key] >= 0; }
};

// Replaceable tone module. This is a synthetic electric-key tone, not a piano sample.
// prepare() is outside the running callback. Tables eliminate per-sample sin().
class ToneBank {
    static constexpr unsigned tableSize = 2048;
    std::array<float, tableSize + 1> table{};
    std::array<double, 128> steps{};
public:
    void prepare(double sampleRate) {
        for (unsigned i = 0; i <= tableSize; ++i) {
            const auto p = 6.283185307179586 * i / tableSize;
            table[i] = static_cast<float>(0.78 * std::sin(p) + 0.16 * std::sin(2*p) + 0.06 * std::sin(3*p));
        }
        for (int n = 0; n < 128; ++n) steps[n] = 440.0 * std::pow(2.0, (n-69)/12.0) / sampleRate;
    }
    double step(int note) const noexcept { return steps[note]; }
    float sample(double phase) const noexcept {
        const auto position = phase * tableSize;
        const auto index = static_cast<unsigned>(position);
        const auto fraction = static_cast<float>(position-index);
        return table[index] + fraction * (table[index+1]-table[index]);
    }
};

class Engine {
    struct Voice {
        int id = -1;
        double phase = 0, step = 0;
        float level = 0, velocity = 0;
        bool down = false, releasing = false, attacking = false;
    };
    std::array<Voice, 64> voices{};
    ToneBank tone;
    float attack = 0, decay = 0, release = 0;
    bool pedal = false;
public:
    void prepare(double rate) {
        tone.prepare(rate);
        attack = static_cast<float>(1.0 / (0.004 * rate));
        decay = static_cast<float>(std::exp(-1.0/(2.5*rate)));
        release = static_cast<float>(std::exp(-1.0/(0.09*rate)));
        panic();
    }
    void panic() noexcept { for (auto& v : voices) v = Voice{}; pedal = false; }
    void event(Event e) noexcept {
        if (e.type == EventType::panic) { panic(); return; }
        if (e.type == EventType::pedal) {
            pedal = e.value > 0;
            if (!pedal) for (auto& v : voices) if (!v.down) v.releasing = true;
            return;
        }
        if (e.type == EventType::off) {
            for (auto& v : voices) if (v.id == e.id && v.down) {
                v.down = false;
                if (!pedal) v.releasing = true;
            }
            return;
        }
        if (e.note < 0 || e.note > 127) return;
        Voice* chosen = nullptr;
        for (auto& v : voices) if (v.id < 0) { chosen = &v; break; }
        if (!chosen) chosen = &*std::min_element(voices.begin(), voices.end(),
            [](const Voice& a, const Voice& b) { return a.level < b.level; });
        *chosen = Voice{};
        chosen->id = e.id;
        chosen->step = tone.step(e.note);
        chosen->velocity = std::clamp(e.value, 0.0f, 1.0f);
        chosen->down = chosen->attacking = true;
    }
    int voiceCount() const noexcept {
        int n=0; for (const auto& v : voices) if (v.id >= 0) ++n; return n;
    }
    // Caller clears output; no allocation, I/O or locks here.
    void render(float* const* channels, int channelCount, int samples) noexcept {
        for (int i=0; i<samples; ++i) {
            float sum = 0;
            for (auto& v : voices) {
                if (v.id < 0) continue;
                if (v.releasing) v.level *= release;
                else if (v.attacking) { v.level += attack; if (v.level >= 1) { v.level = 1; v.attacking = false; } }
                else v.level *= decay;
                if (!v.attacking && v.level < 0.00001f) { v.id = -1; continue; }
                sum += tone.sample(v.phase) * v.level * v.velocity * 0.12f;
                v.phase += v.step;
                v.phase -= std::floor(v.phase);
            }
            // Bounded soft saturation; a dedicated limiter can replace this stage.
            const float output = sum / (1.0f + std::abs(sum));
            for (int c=0; c<channelCount; ++c) channels[c][i] += output;
        }
    }
};
}
