#pragma once

#include "Biquad.hpp"
#include "ET5MParams.hpp"
#include "SpringReverbTank.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace et5m {

class TubeCabinetStage {
public:
    static constexpr std::size_t kMaxDelaySamples = 32;

    void prepare(double sampleRate) noexcept {
        fs_ = sampleRate;
        reverb_.prepare(fs_);
        cabFilterL_.configure(kCabinetStages, kCabinetGainDb, fs_);
        cabFilterR_.configure(kCabinetStages, kCabinetGainDb, fs_);
        otHp_.setCoeffs(designOnePoleHP(kTubeOtHpFcHz, fs_));
        otLp_.setCoeffs(designOnePoleLP(kTubeOtLpFcHz, fs_));
        rDisp_.setCoeffs(designOnePoleLP(3800.0, fs_));
        noiseLpL_.setCoeffs(designOnePoleLP(1400.0, fs_));
        noiseLpR_.setCoeffs(designOnePoleLP(1400.0, fs_));
        delaySamples_ = std::max<std::size_t>(1, std::min<std::size_t>(14, static_cast<std::size_t>(std::llround(0.00018 * fs_))));
        reset();
    }

    void reset() noexcept {
        reverb_.reset();
        cabFilterL_.reset();
        cabFilterR_.reset();
        otHp_.reset();
        otLp_.reset();
        rDisp_.reset();
        noiseLpL_.reset();
        noiseLpR_.reset();
        humPhase_ = 0.0;
        delayBufR_.fill(0.0);
        delayIdx_ = 0;
        prngState_ = 19650201ULL;
        prevU1_ = kTube12ax7Bias;
        prevF1_ = adaaF(kTube12ax7Bias);
        prevU2_ = 0.0;
        prevF2_ = adaaF(0.0);
    }

    void processBlock(
        const double* monoIn,
        double* outLeft,
        double* outRight,
        std::size_t nSamples,
        double tubeAmount,
        double humAmount,
        double stereoWidth,
        double hallTone = 0.0
    ) noexcept {
        const double phInc = 100.0 / fs_;
        const double humAmp = std::pow(10.0, kHum100HzDb / 20.0) * 1.4142135623730951 * humAmount;
        const double nfAmp = std::pow(10.0, kNoiseFloorDb / 20.0) * humAmount * 1.6;
        const double widthWeight = std::clamp(stereoWidth, 0.0, 1.0) * 0.35;
        const double ht = std::clamp(hallTone, 0.0, 1.0);
        const bool doReverb = ht > 1e-4;
        const double wetGain = doReverb ? (kReverbMaxWetGain * std::pow(ht, kReverbTaperExp)) : 0.0;
        const bool doTube = tubeAmount > 1e-4;
        const bool doWidth = stereoWidth > 1e-4;
        const bool doHum = humAmount > 1e-4;

        const double d1 = std::max(1e-4, kTube12ax7Drive * tubeAmount);
        const double b1 = kTube12ax7Bias;
        const double coshB1 = std::cosh(b1);
        const double slope1 = d1 / (coshB1 * coshB1);
        const double tanhB1 = std::tanh(b1);
        const double d2 = std::max(1e-4, kTube6bq5Drive * tubeAmount);

        if (!doTube) {
            prevU1_ = b1;
            prevF1_ = adaaF(b1);
            prevU2_ = 0.0;
            prevF2_ = adaaF(0.0);
        }

        for (std::size_t i = 0; i < nSamples; ++i) {
            const double xi = monoIn[i];
            const double x = doReverb ? (xi + wetGain * reverb_.processSample(xi)) : xi;
            double sat = x;
            if (doTube) {
                const double u1 = x * d1 + b1;
                const double fu1 = adaaF(u1);
                const double du1 = u1 - prevU1_;
                const double th1 = (std::abs(du1) > 1e-5)
                    ? ((fu1 - prevF1_) / du1)
                    : std::tanh(0.5 * (u1 + prevU1_));
                const double y1 = (th1 - tanhB1) / slope1;
                prevU1_ = u1;
                prevF1_ = fu1;

                const double u2 = y1 * d2;
                const double fu2 = adaaF(u2);
                const double du2 = u2 - prevU2_;
                const double th2 = (std::abs(du2) > 1e-5)
                    ? ((fu2 - prevF2_) / du2)
                    : std::tanh(0.5 * (u2 + prevU2_));
                sat = th2 / d2;
                prevU2_ = u2;
                prevF2_ = fu2;
            }

            const double satOt = otLp_.processSample(otHp_.processSample(sat));
            double cabL = cabFilterL_.processSample(satOt);
            double cabR = cabFilterR_.processSample(satOt);

            if (doWidth) {
                const double delayed = delayBufR_[delayIdx_];
                delayBufR_[delayIdx_] = cabR;
                delayIdx_ = (delayIdx_ + 1) % delaySamples_;
                const double rWet = rDisp_.processSample(delayed);
                cabR = (1.0 - widthWeight) * cabR + widthWeight * rWet;
            }

            if (doHum) {
                const double twoPiPh = 2.0 * kPi * humPhase_;
                const double hum = humAmp * (std::sin(twoPiPh) + 0.18 * std::sin(2.0 * twoPiPh + 0.4));
                humPhase_ += phInc;
                if (humPhase_ >= 1.0) humPhase_ -= 1.0;

                const double nL = noiseLpL_.processSample(nextGaussianApprox()) * nfAmp;
                const double nR = noiseLpR_.processSample(nextGaussianApprox()) * nfAmp;
                cabL += hum + nL;
                cabR += hum + nR;
            }

            outLeft[i] = cabL;
            outRight[i] = cabR;
        }
    }

    [[nodiscard]] double maxPoleRadius() const noexcept {
        return cabFilterL_.maxPoleRadius();
    }

private:
    [[nodiscard]] static double adaaF(double u) noexcept {
        const double au = std::abs(u);
        return au + std::log1p(std::exp(-2.0 * au));
    }

    [[nodiscard]] double nextGaussianApprox() noexcept {
        double sum = 0.0;
        for (int k = 0; k < 6; ++k) {
            prngState_ ^= prngState_ << 13;
            prngState_ ^= prngState_ >> 7;
            prngState_ ^= prngState_ << 17;
            const double u = static_cast<double>(prngState_ & 0xFFFFFFFFULL) / 4294967296.0;
            sum += u;
        }
        return (sum - 3.0) * 1.4142135623730951;
    }

    double fs_{48000.0};
    SpringReverbTank reverb_{};
    CascadeFilter<kCabinetNumStages> cabFilterL_{};
    CascadeFilter<kCabinetNumStages> cabFilterR_{};
    OnePoleFilter otHp_{};
    OnePoleFilter otLp_{};
    OnePoleFilter rDisp_{};
    OnePoleFilter noiseLpL_{};
    OnePoleFilter noiseLpR_{};
    std::array<double, kMaxDelaySamples> delayBufR_{};
    std::size_t delaySamples_{8};
    std::size_t delayIdx_{0};
    double humPhase_{0.0};
    std::uint64_t prngState_{19650201ULL};
    double prevU1_{kTube12ax7Bias};
    double prevF1_{0.0};
    double prevU2_{0.0};
    double prevF2_{0.0};

public:
    TubeCabinetStage() noexcept {
        reset();
    }
};

} // namespace et5m
