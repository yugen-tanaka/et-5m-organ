#pragma once

#include "Biquad.hpp"
#include "ET5MParams.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <vector>

namespace et5m {

// 7 masters: C(0), F#(1), G(2), G#(3), A(4), A#(5), B(6)
// Wrap modulus divisible by 3 * 2^16 = 196608 so 1/2^k and 1/(3*2^k) dividers wrap seamlessly.
inline constexpr double kMasterPhaseMod = static_cast<double>(3 * (1 << 16));

struct PitchClassSource {
    bool isMasterChain;
    std::size_t masterIndex;
};

inline constexpr std::array<PitchClassSource, 12> kPitchClassSources = {{
    {true,  0}, // 0: C  <- Master 0 (C7)
    {false, 3}, // 1: C# <- Master 3 (G#6) / 3
    {false, 4}, // 2: D  <- Master 4 (A6)  / 3
    {false, 5}, // 3: D# <- Master 5 (A#6) / 3
    {false, 6}, // 4: E  <- Master 6 (B6)  / 3
    {false, 0}, // 5: F  <- Master 0 (C7)  / 3
    {true,  1}, // 6: F# <- Master 1 (F#6)
    {true,  2}, // 7: G  <- Master 2 (G6)
    {true,  3}, // 8: G# <- Master 3 (G#6)
    {true,  4}, // 9: A  <- Master 4 (A6)
    {true,  5}, // 10: A# <- Master 5 (A#6)
    {true,  6}  // 11: B  <- Master 6 (B6)
}};

inline constexpr std::array<int, 7> kMasterTopMidi = {96, 90, 91, 92, 93, 94, 95};

[[nodiscard]] inline double polyBlep(double t, double dt) noexcept {
    if (t < dt) {
        const double x = t / dt;
        return x + x - x * x - 1.0;
    }
    if (t > 1.0 - dt) {
        const double x = (t - 1.0) / dt;
        return x * x + x + x + 1.0;
    }
    return 0.0;
}

[[nodiscard]] inline double stageCoutHpHz(int midiNote) noexcept {
    if (midiNote >= 67) return 388.2;
    if (midiNote >= 55) return 194.1;
    if (midiNote >= 43) return 77.6;
    return 38.8;
}

[[nodiscard]] inline double renderBlockingOscSample(
    double phase,
    double dt,
    bool isOneThird,
    double syncBleed = 1.0,
    double expKappa = kExpKappa
) noexcept {
    const double expNegK = std::exp(-expKappa);
    const double norm = 1.0 / (1.0 - expNegK);
    const double ramp01 = (std::exp(-expKappa * phase) - expNegK) * norm;
    double wave = 2.0 * ramp01 - 1.0;

    const double dtClamped = std::min(dt, 0.25);
    wave += polyBlep(phase, dtClamped);

    if (syncBleed > 0.0) {
        const double sigma = std::max(0.018, 2.6 * dt);
        const double invSigma = 1.0 / sigma;
        if (isOneThird) {
            const double diff1 = std::abs(phase - (1.0 / 3.0));
            const double d1 = std::min(diff1, 1.0 - diff1) * invSigma;
            const double diff2 = std::abs(phase - (2.0 / 3.0));
            const double d2 = std::min(diff2, 1.0 - diff2) * invSigma;
            wave -= syncBleed * 0.105 * (std::exp(-0.5 * d1 * d1) + std::exp(-0.5 * d2 * d2));
        } else {
            const double d5 = (phase - 0.5) * invSigma;
            wave -= syncBleed * 0.085 * std::exp(-0.5 * d5 * d5);
        }
    }
    return wave;
}

class BlockingOscillatorBank {
public:
    static constexpr int kMinMidi = 36;
    static constexpr int kMaxMidi = 96;
    static constexpr std::size_t kNumNotes = static_cast<std::size_t>(kMaxMidi - kMinMidi + 1);
    static constexpr std::size_t kTableSize = 2048;
    static constexpr std::size_t kTableMask = kTableSize - 1;

    void prepare(double sampleRate) noexcept {
        fs_ = sampleRate;
        vibLpAlpha_ = 1.0 - std::exp(-2.0 * kPi * kVibratoRcFcHz / fs_);
        buildBandlimitedTables();
        for (int m = kMinMidi; m <= kMaxMidi; ++m) {
            const std::size_t idx = static_cast<std::size_t>(m - kMinMidi);
            noteHpFilters_[idx].setCoeffs(designOnePoleHP(stageCoutHpHz(m), fs_));
            noteHpFilters_[idx].reset();
        }
    }

    void resetPhases() noexcept {
        masterPhases_.fill(0.0);
        vibLpState_ = 0.0;
        for (auto& f : noteHpFilters_) {
            f.reset();
        }
    }

    void setRefA4Hz(double refA4Hz) noexcept {
        refA4Hz_ = refA4Hz;
    }

    [[nodiscard]] double refA4Hz() const noexcept {
        return refA4Hz_;
    }

    [[nodiscard]] double masterBaseFreq(std::size_t masterIdx) const noexcept {
        const int mMidi = kMasterTopMidi[masterIdx];
        return refA4Hz_ * std::pow(2.0, static_cast<double>(mMidi - 69) / 12.0);
    }

    [[nodiscard]] double noteFrequency(int midiNote, bool authentic13 = true) const noexcept {
        const std::size_t pc = static_cast<std::size_t>((midiNote % 12 + 12) % 12);
        const auto& src = kPitchClassSources[pc];
        if (!authentic13 || src.isMasterChain) {
            return refA4Hz_ * std::pow(2.0, static_cast<double>(midiNote - 69) / 12.0);
        }
        const double fMaster = masterBaseFreq(src.masterIndex);
        const int f5Midi = kMasterTopMidi[src.masterIndex] - 19;
        const double octDiff = static_cast<double>(midiNote - f5Midi) / 12.0;
        return (fMaster / 3.0) * std::pow(2.0, octDiff);
    }

    void advanceMasters(
        std::size_t nSamples,
        const double* vibLfo,
        double vibDepthCents,
        double vibDcShiftCents,
        bool vibActive,
        std::array<double*, 7>& masterUnwrappedOut
    ) noexcept {
        if (!vibActive || vibLfo == nullptr) {
            // Advance RC filter smoothly toward zero while advancing masters drift-free
            for (std::size_t i = 0; i < nSamples; ++i) {
                vibLpState_ += vibLpAlpha_ * (0.0 - vibLpState_);
                const double pitchMult = std::pow(2.0, vibLpState_ / 1200.0);
                for (std::size_t m = 0; m < 7; ++m) {
                    const double dt0 = masterBaseFreq(m) / fs_;
                    masterUnwrappedOut[m][i] = masterPhases_[m];
                    masterPhases_[m] += dt0 * pitchMult;
                }
            }
        } else {
            for (std::size_t i = 0; i < nSamples; ++i) {
                const double target = vibLfo[i] * vibDepthCents + vibDcShiftCents;
                vibLpState_ += vibLpAlpha_ * (target - vibLpState_);
                const double pitchMult = std::pow(2.0, vibLpState_ / 1200.0);
                for (std::size_t m = 0; m < 7; ++m) {
                    const double dt0 = masterBaseFreq(m) / fs_;
                    masterUnwrappedOut[m][i] = masterPhases_[m];
                    masterPhases_[m] += dt0 * pitchMult;
                }
            }
        }
        for (std::size_t m = 0; m < 7; ++m) {
            masterPhases_[m] = std::fmod(masterPhases_[m], kMasterPhaseMod);
            if (masterPhases_[m] < 0.0) {
                masterPhases_[m] += kMasterPhaseMod;
            }
        }
    }

    void renderNote(
        int midiNote,
        const std::array<const double*, 7>& masterUnwrapped,
        std::size_t nSamples,
        bool authentic13,
        double syncBleed,
        double* dstOut
    ) noexcept {
        if (midiNote < kMinMidi || midiNote > kMaxMidi) {
            for (std::size_t i = 0; i < nSamples; ++i) dstOut[i] = 0.0;
            return;
        }
        if (rampTables_.size() != kNumNotes * kTableSize) {
            buildBandlimitedTables();
        }
        const std::size_t noteIdx = static_cast<std::size_t>(midiNote - kMinMidi);
        const std::size_t pc = static_cast<std::size_t>((midiNote % 12 + 12) % 12);
        const auto& src = kPitchClassSources[pc];
        const double fMaster = masterBaseFreq(src.masterIndex);
        const double fNote = noteFrequency(midiNote, authentic13);
        const double ratio = fNote / fMaster;
        const double* mPh = masterUnwrapped[src.masterIndex];
        const double* ramp = &rampTables_[noteIdx * kTableSize];
        const double* sync = &syncTables_[noteIdx * kTableSize];
        auto& hp = noteHpFilters_[noteIdx];

        for (std::size_t i = 0; i < nSamples; ++i) {
            double ph = std::fmod(mPh[i] * ratio, 1.0);
            if (ph < 0.0) ph += 1.0;

            const double pos = ph * static_cast<double>(kTableSize);
            const double idxFl = std::floor(pos);
            const double frac = pos - idxFl;
            const std::size_t idx0 = static_cast<std::size_t>(static_cast<int>(idxFl)) & kTableMask;
            const std::size_t jm1 = (idx0 - 1U) & kTableMask;
            const std::size_t j0 = idx0;
            const std::size_t j1 = (idx0 + 1U) & kTableMask;
            const std::size_t j2 = (idx0 + 2U) & kTableMask;

            const double ym1 = ramp[jm1] + syncBleed * sync[jm1];
            const double y0  = ramp[j0]  + syncBleed * sync[j0];
            const double y1  = ramp[j1]  + syncBleed * sync[j1];
            const double y2  = ramp[j2]  + syncBleed * sync[j2];

            const double c0 = y0;
            const double c1 = 0.5 * (y1 - ym1);
            const double c2 = ym1 - 2.5 * y0 + 2.0 * y1 - 0.5 * y2;
            const double c3 = 0.5 * (y2 - ym1) + 1.5 * (y0 - y1);
            const double raw = ((c3 * frac + c2) * frac + c1) * frac + c0;
            dstOut[i] = hp.processSample(raw);
        }
    }

private:
    void buildBandlimitedTables() noexcept {
        rampTables_.assign(kNumNotes * kTableSize, 0.0);
        syncTables_.assign(kNumNotes * kTableSize, 0.0);

        constexpr double sigma = 0.018;
        const double sqrt2Pi = std::sqrt(2.0 * kPi);

        for (std::size_t idx = 0; idx < kNumNotes; ++idx) {
            const int midiNote = kMinMidi + static_cast<int>(idx);
            const std::size_t pc = static_cast<std::size_t>((midiNote % 12 + 12) % 12);
            const bool isOneThird = !kPitchClassSources[pc].isMasterChain;
            const double fMax = 450.0 * std::pow(2.0, static_cast<double>(midiNote - 69 + 0.25) / 12.0);
            const int maxHarm = std::clamp(static_cast<int>((0.44 * fs_) / fMax), 1, 64);
            const int taperStart = std::max(2, static_cast<int>(0.82 * static_cast<double>(maxHarm)));

            double* ramp = &rampTables_[idx * kTableSize];
            double* sync = &syncTables_[idx * kTableSize];

            for (int k = 1; k <= maxHarm; ++k) {
                double taper = 1.0;
                if (maxHarm >= 4 && k > taperStart) {
                    const double u = static_cast<double>(k - taperStart)
                                   / static_cast<double>(maxHarm - taperStart + 1);
                    taper = 0.5 * (1.0 + std::cos(kPi * u));
                }

                const double wk = 2.0 * kPi * static_cast<double>(k);
                const double denom = kExpKappa * kExpKappa + wk * wk;
                const double akR = taper * (4.0 * kExpKappa) / denom;
                const double bkR = taper * (4.0 * wk) / denom;

                const double gEnv = std::exp(-0.5 * (wk * sigma) * (wk * sigma));
                double akS = 0.0;
                if (isOneThird) {
                    akS = taper * (-4.0 * 0.105 * sqrt2Pi * sigma * std::cos(2.0 * kPi * static_cast<double>(k) / 3.0) * gEnv);
                } else {
                    const double sign = ((k % 2) == 1) ? -1.0 : 1.0;
                    akS = taper * (-2.0 * 0.085 * sqrt2Pi * sigma * sign * gEnv);
                }

                for (std::size_t j = 0; j < kTableSize; ++j) {
                    const double phiJ = static_cast<double>(j) / static_cast<double>(kTableSize);
                    const double ang = wk * phiJ;
                    const double c = std::cos(ang);
                    const double s = std::sin(ang);
                    ramp[j] += akR * c + bkR * s;
                    sync[j] += akS * c;
                }
            }
        }
    }

    double fs_{48000.0};
    double refA4Hz_{kRefA4Hz};
    double vibLpState_{0.0};
    double vibLpAlpha_{0.0};
    std::array<double, 7> masterPhases_{};
    std::array<OnePoleFilter, kNumNotes> noteHpFilters_{};
    std::vector<double> rampTables_{};
    std::vector<double> syncTables_{};
};

} // namespace et5m
