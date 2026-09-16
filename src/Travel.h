// Copyright (C) 2026 Boccp
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <span>

namespace homekeys {
// Experimental wire format documented at:
// https://github.com/RigZeeel/KeyAxis/blob/main/PROTOCOL.md
// This parser does not send commands or imply that a device is compatible.
struct TravelFrame { std::uint8_t row, column, depth; };
inline std::optional<TravelFrame> parseTravel(std::span<const std::uint8_t> d) noexcept {
    if (d.size() != 64 || d[0] != 1 || d[1] != 0x21 || d[5] != 3 || d[9] > 40)
        return std::nullopt;
    return TravelFrame{d[7], d[8], d[9]};
}
struct TravelCalibration {
    std::uint8_t row{}, column{}, rest{}, peak{};
    bool valid() const noexcept { return peak <= 40 && peak >= rest + 10; }
    float normalise(unsigned depth) const noexcept {
        if (!valid()) return 0;
        return std::clamp((float(depth) - rest) / (peak - rest), 0.0f, 1.0f);
    }
};
// A software-only calibration exercise: explicit rest reference, one physical
// switch per exercise, full press then release. It never calibrates firmware.
class TravelCalibrator {
    TravelCalibration candidate{};
    bool started{}, ambiguous{}, returned{};
public:
    bool begin(TravelFrame resting) noexcept {
        candidate = {resting.row, resting.column, resting.depth, resting.depth};
        started = resting.depth <= 5;
        ambiguous = returned = false;
        return started;
    }
    void observe(TravelFrame f) noexcept {
        if (!started || f.depth > 40) return;
        if (f.row != candidate.row || f.column != candidate.column) {
            if (f.depth > 5) ambiguous = true;
            return;
        }
        candidate.peak = std::max(candidate.peak, f.depth);
        returned = candidate.valid() && f.depth <= candidate.rest + 2;
    }
    std::optional<TravelCalibration> finish() const noexcept {
        if (!started || ambiguous || !returned || !candidate.valid()) return std::nullopt;
        return candidate;
    }
};
struct TravelExpression {
    float travel{};
    bool noteOn{}, noteOff{};
    // Estimated strike velocity, not force or measured aftertouch.
    std::optional<float> estimatedVelocity;
};
// One instance per calibrated physical key. Call from a single input worker.
// Time is monotonic microseconds. No allocation, polling, or audio-thread work.
class TravelTracker {
    TravelCalibration calibration;
    bool held{}, haveSample{}, measuring{};
    float previous{};
    std::uint64_t previousTime{}, startTime{};
public:
    explicit TravelTracker(TravelCalibration c) noexcept : calibration(c) {}
    std::optional<TravelExpression> observe(TravelFrame f, std::uint64_t time) noexcept {
        if (!calibration.valid() || f.depth > 40 || f.row != calibration.row || f.column != calibration.column)
            return std::nullopt;
        if (haveSample && time <= previousTime) return std::nullopt;
        const float depth = calibration.normalise(f.depth);
        TravelExpression out{depth};
        // A gap invalidates velocity timing, but silence never releases a held key:
        // this protocol emits only changes. Disconnect/stop must call reset().
        if (haveSample && time - previousTime > 100000) measuring = false;
        if (!held && haveSample && previous < .15f && depth >= .15f) {
            startTime = previousTime + static_cast<std::uint64_t>(
                (time - previousTime) * (.15f - previous) / (depth - previous));
            measuring = time - previousTime <= 100000;
        }
        if (!held && depth >= .55f) {
            held = true; out.noteOn = true;
            if (measuring && depth > previous) {
                const auto strikeTime = previousTime + static_cast<std::uint64_t>(
                    (time - previousTime) * (.55f - previous) / (depth - previous));
                if (strikeTime > startTime) {
                    const float speed = .4f * 1000000.0f / float(strikeTime - startTime);
                    // Initial tunable curve; needs player/device validation.
                    out.estimatedVelocity = std::clamp(std::sqrt(speed / 100.0f), .05f, 1.0f);
                }
            }
            // An event-driven keyboard may skip the early threshold after a
            // long rest. Use the final observed moving segment when available.
            if (!out.estimatedVelocity && haveSample && depth-previous >= .05f
                && time-previousTime >= 500 && time-previousTime <= 100000) {
                const float speed=(depth-previous)*1000000.0f/float(time-previousTime);
                out.estimatedVelocity=std::clamp(std::sqrt(speed/100.0f),.05f,1.0f);
            }
            measuring = false;
        }
        if (held && depth <= .10f) { held = false; out.noteOff = true; }
        if (depth < .10f) measuring = false;
        previous = depth; previousTime = time; haveSample = true;
        return out;
    }
    bool reset() noexcept {
        const bool releaseRequired = held;
        held = haveSample = measuring = false;
        previous = 0; previousTime = startTime = 0;
        return releaseRequired;
    }
};
}
