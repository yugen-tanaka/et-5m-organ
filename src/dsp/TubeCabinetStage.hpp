#pragma once

#include "Biquad.hpp"
#include "ET5MParams.hpp"
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
        cabFilterL_.configure(kCabinetStages, kCabinetGainDb, fs_);
        cabFilterR_.configure(kCabinetStages, kCabinetGainDb, fs_);
        rDisp_.setCoeffs(designOnePoleLP(3800.0, fs_));
        noiseLpL_.setCoeffs(designOnePoleLP(1400.0, fs_));
        noiseLpR_.setCoeffs(designOnePoleLP(1400.0, fs_));
        delaySamples_ = std::max<std::size_t>(1, std::min<std::size_t>(14, static_cast<std::size_t>(std::llround(0.00018 * fs_))));
        reset();
    }

    void reset() noexcept {
        cabFilterL_.reset();
        cabFilterR_.reset();
        rDisp_.reset();
        noiseLpL_.reset();
        noiseLpR_.reset();
        humPhase_ = 0.0;
        delayBufR_.fill(0.0);
        delayIdx_ = 0;
        prngState_ = 19650201ULL;
        prevU_ = kTubeBias;
        prevF_ = adaaF(kTubeBias);
    }

    void processBlock(
        const double* monoIn,
        double* outLeft,
        double* outRight,
        std::size_t nSamples,
        double tubeAmount,
        double humAmount,
        double stereoWidth
    ) noexcept {
        const double phInc = 100.0 / fs_;
        const double humAmp = std::pow(10.0, kHum100HzDb / 20.0) * 1.4142135623730951 * humAmount;
        const double nfAmp = std::pow(10.0, kNoiseFloorDb / 20.0) * humAmount * 1.6;
        const double widthWeight = std::clamp(stereoWidth, 0.0, 1.0) * 0.35;
        const bool doTube = tubeAmount > 1e-4;
        const bool doWidth = stereoWidth > 1e-4;
        const bool doHum = humAmount > 1e-4;

        const double d = std::max(1e-4, kTubeDrive * tubeAmount);
        const double b = kTubeBias;
        const double coshB = std::cosh(b);
        const double slope0 = d / (coshB * coshB);
        const double tanhB = std::tanh(b);

        if (!doTube) {
            prevU_ = b;
            prevF_ = adaaF(b);
        }

        for (std::size_t i = 0; i < nSamples; ++i) {
            const double x = monoIn[i];
            double sat = x;
            if (doTube) {
                const double u = x * d + b;
                const double fu = adaaF(u);
                const double du = u - prevU_;
                const double th = (std::abs(du) > 1e-5)
                    ? ((fu - prevF_) / du)
                    : std::tanh(0.5 * (u + prevU_));
                sat = (th - tanhB) / slope0;
                prevU_ = u;
                prevF_ = fu;
            }

            double cabL = cabFilterL_.processSample(sat);
            double cabR = cabFilterR_.processSample(sat);

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

    // Fast deterministic zero-mean unit-variance Irwin-Hall Gaussian approximation
    [[nodiscard]] double nextGaussianApprox() noexcept {
        double sum = 0.0;
        for (int k = 0; k < 6; ++k) {
            prngState_ ^= prngState_ << 13;
            prngState_ ^= prngState_ >> 7;
            prngState_ ^= prngState_ << 17;
            const double u = static_cast<double>(prngState_ & 0xFFFFFFFFULL) / 4294967296.0;
            sum += u;
        }
        return (sum - 3.0) * 1.4142135623730951; // mean 0, std 1
    }

    double fs_{48000.0};
    CascadeFilter<kCabinetNumStages> cabFilterL_{};
    CascadeFilter<kCabinetNumStages> cabFilterR_{};
    OnePoleFilter rDisp_{};
    OnePoleFilter noiseLpL_{};
    OnePoleFilter noiseLpR_{};
    std::array<double, kMaxDelaySamples> delayBufR_{};
    std::size_t delaySamples_{8};
    std::size_t delayIdx_{0};
    double humPhase_{0.0};
    std::uint64_t prngState_{19650201ULL};
    double prevU_{kTubeBias};
    double prevF_{0.0};

public:
    TubeCabinetStage() noexcept {
        reset();
    }
};

} // namespace et5m
