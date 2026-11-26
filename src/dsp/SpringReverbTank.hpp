#pragma once

#include "Biquad.hpp"
#include "ET5MParams.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace et5m {

class SpringReverbTank {
public:
    static constexpr std::size_t kNumDispStages = 6;
    static constexpr std::size_t kMaxDispLen = 64;
    static constexpr std::size_t kMaxSpringLen = 4096;
    static constexpr std::size_t kMaxOutApLen = 1024;

    void prepare(double sampleRate) noexcept {
        fs_ = sampleRate;
        sendHp_.setCoeffs(designOnePoleHP(kReverbSendHpFcHz, fs_));
        sendLp_.setCoeffs(designOnePoleLP(kReverbSendLpFcHz, fs_));
        damp1Lp_.setCoeffs(designOnePoleLP(kReverbSpring1DampHz, fs_));
        damp2Lp_.setCoeffs(designOnePoleLP(kReverbSpring2DampHz, fs_));
        dc1Hp_.setCoeffs(designOnePoleHP(kReverbSpringDcHpHz, fs_));
        dc2Hp_.setCoeffs(designOnePoleHP(kReverbSpringDcHpHz, fs_));
        recHp_.setCoeffs(designOnePoleHP(kReverbRecoveryHpFcHz, fs_));
        recLp_.setCoeffs(designOnePoleLP(kReverbRecoveryLpFcHz, fs_));

        dispLen_ = msToSamples(kReverbDispDelayMs, fs_, 1, kMaxDispLen);
        s1Len_ = msToSamples(kReverbSpring1Ms, fs_, 16, kMaxSpringLen);
        s2Len_ = msToSamples(kReverbSpring2Ms, fs_, 16, kMaxSpringLen);
        o1Len_ = msToSamples(kReverbOutAp1Ms, fs_, 4, kMaxOutApLen);
        o2Len_ = msToSamples(kReverbOutAp2Ms, fs_, 4, kMaxOutApLen);
        reset();
    }

    void reset() noexcept {
        sendHp_.reset();
        sendLp_.reset();
        damp1Lp_.reset();
        damp2Lp_.reset();
        dc1Hp_.reset();
        dc2Hp_.reset();
        recHp_.reset();
        recLp_.reset();
        for (auto& b : dispBufs_) {
            b.fill(0.0);
        }
        dispIdx_.fill(0);
        s1Buf_.fill(0.0);
        s2Buf_.fill(0.0);
        s1Idx_ = 0;
        s2Idx_ = 0;
        o1Buf_.fill(0.0);
        o2Buf_.fill(0.0);
        o1Idx_ = 0;
        o2Idx_ = 0;
        prevU_ = 0.0;
        prevF_ = adaaF(0.0);
    }

    [[nodiscard]] double processSample(double x) noexcept {
        // 1. 6BM8 send conditioning + pentode ADAA1 saturation
        const double s = sendLp_.processSample(sendHp_.processSample(x));
        const double d = kReverbDriverDrive;
        const double u = s * d;
        const double fu = adaaF(u);
        const double du = u - prevU_;
        const double th = (std::abs(du) > 1e-5)
            ? ((fu - prevF_) / du)
            : std::tanh(0.5 * (u + prevU_));
        double y = th / d;
        prevU_ = u;
        prevF_ = fu;

        // 2. 6-stage input dispersion allpass cascade
        const double aD = kReverbDispAllpassG;
        for (std::size_t k = 0; k < kNumDispStages; ++k) {
            std::size_t idx = dispIdx_[k];
            const double dOut = dispBufs_[k][idx];
            const double v = y - aD * dOut;
            y = aD * v + dOut;
            dispBufs_[k][idx] = v;
            ++idx;
            if (idx >= dispLen_) idx = 0;
            dispIdx_[k] = idx;
        }

        // 3. Dual cross-coupled spring tanks
        const double s1Raw = s1Buf_[s1Idx_];
        const double s2Raw = s2Buf_[s2Idx_];
        const double s1Out = damp1Lp_.processSample(dc1Hp_.processSample(s1Raw));
        const double s2Out = damp2Lp_.processSample(dc2Hp_.processSample(s2Raw));

        const double cm = kReverbCrossMix;
        const double fb1 = (1.0 - cm) * s1Out + cm * s2Out;
        const double fb2 = (1.0 - cm) * s2Out - cm * s1Out;
        s1Buf_[s1Idx_] = y + kReverbFeedbackGain * fb1;
        s2Buf_[s2Idx_] = y + kReverbFeedbackGain * fb2;
        ++s1Idx_;
        if (s1Idx_ >= s1Len_) s1Idx_ = 0;
        ++s2Idx_;
        if (s2Idx_ >= s2Len_) s2Idx_ = 0;

        const double tankOut = 0.5 * (s1Out + s2Out);

        // 4. 2-stage output diffusion allpasses
        const double d1 = o1Buf_[o1Idx_];
        const double v1 = tankOut - kReverbOutAp1G * d1;
        const double ap1 = kReverbOutAp1G * v1 + d1;
        o1Buf_[o1Idx_] = v1;
        ++o1Idx_;
        if (o1Idx_ >= o1Len_) o1Idx_ = 0;

        const double d2 = o2Buf_[o2Idx_];
        const double v2 = ap1 - kReverbOutAp2G * d2;
        const double ap2 = kReverbOutAp2G * v2 + d2;
        o2Buf_[o2Idx_] = v2;
        ++o2Idx_;
        if (o2Idx_ >= o2Len_) o2Idx_ = 0;

        // 5. 12AX7 recovery coupling
        return recLp_.processSample(recHp_.processSample(ap2));
    }

private:
    [[nodiscard]] static double adaaF(double u) noexcept {
        const double au = std::abs(u);
        return au + std::log1p(std::exp(-2.0 * au));
    }

    [[nodiscard]] static std::size_t msToSamples(double ms, double fs, std::size_t minVal, std::size_t maxVal) noexcept {
        const double raw = std::floor(ms * 1e-3 * fs + 0.5);
        const std::size_t s = (raw > 0.0) ? static_cast<std::size_t>(raw) : minVal;
        return std::clamp(s, minVal, maxVal);
    }

    double fs_{48000.0};
    OnePoleFilter sendHp_{};
    OnePoleFilter sendLp_{};
    OnePoleFilter damp1Lp_{};
    OnePoleFilter damp2Lp_{};
    OnePoleFilter dc1Hp_{};
    OnePoleFilter dc2Hp_{};
    OnePoleFilter recHp_{};
    OnePoleFilter recLp_{};

    std::size_t dispLen_{22};
    std::size_t s1Len_{1426};
    std::size_t s2Len_{1781};
    std::size_t o1Len_{254};
    std::size_t o2Len_{379};

    std::array<std::array<double, kMaxDispLen>, kNumDispStages> dispBufs_{};
    std::array<std::size_t, kNumDispStages> dispIdx_{};
    std::array<double, kMaxSpringLen> s1Buf_{};
    std::array<double, kMaxSpringLen> s2Buf_{};
    std::size_t s1Idx_{0};
    std::size_t s2Idx_{0};
    std::array<double, kMaxOutApLen> o1Buf_{};
    std::array<double, kMaxOutApLen> o2Buf_{};
    std::size_t o1Idx_{0};
    std::size_t o2Idx_{0};
    double prevU_{0.0};
    double prevF_{0.0};

public:
    SpringReverbTank() noexcept {
        reset();
    }
};

} // namespace et5m
