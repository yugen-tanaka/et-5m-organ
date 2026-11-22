#pragma once

#include "ET5MParams.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstddef>

namespace et5m {

inline constexpr double kPi = 3.14159265358979323846;

struct OnePoleCoeffs {
    double b0{1.0};
    double b1{0.0};
    double a1{0.0};

    [[nodiscard]] double maxPoleRadius() const noexcept {
        return std::abs(a1);
    }
};

struct BiquadCoeffs {
    double b0{1.0};
    double b1{0.0};
    double b2{0.0};
    double a1{0.0};
    double a2{0.0};

    [[nodiscard]] double maxPoleRadius() const noexcept {
        // Roots of z^2 + a1*z + a2 = 0
        const double disc = a1 * a1 - 4.0 * a2;
        if (disc < 0.0) {
            return std::sqrt(std::max(0.0, a2));
        }
        const double s = std::sqrt(disc);
        const double r1 = std::abs(0.5 * (-a1 + s));
        const double r2 = std::abs(0.5 * (-a1 - s));
        return std::max(r1, r2);
    }
};

[[nodiscard]] inline OnePoleCoeffs designOnePoleHP(double fc, double fs) noexcept {
    const double fcClamped = std::max(1.0, std::min(fc, 0.45 * fs));
    const double k = std::tan(kPi * fcClamped / fs);
    const double inv = 1.0 / (1.0 + k);
    return OnePoleCoeffs{inv, -inv, (k - 1.0) * inv};
}

[[nodiscard]] inline OnePoleCoeffs designOnePoleLP(double fc, double fs) noexcept {
    const double fcClamped = std::max(1.0, std::min(fc, 0.45 * fs));
    const double k = std::tan(kPi * fcClamped / fs);
    const double inv = 1.0 / (1.0 + k);
    const double b0 = k * inv;
    return OnePoleCoeffs{b0, b0, (k - 1.0) * inv};
}

[[nodiscard]] inline BiquadCoeffs designRbjBiquad(
    FilterStageType ftype,
    double fc,
    double q,
    double gainDb,
    double fs
) noexcept {
    const double fcClamped = std::max(5.0, std::min(fc, 0.47 * fs));
    const double qClamped = std::max(0.2, std::min(q, 25.0));
    const double w0 = 2.0 * kPi * fcClamped / fs;
    const double cosW0 = std::cos(w0);
    const double sinW0 = std::sin(w0);
    const double alpha = sinW0 / (2.0 * qClamped);

    double b0 = 1.0, b1 = 0.0, b2 = 0.0;
    double a0 = 1.0, a1 = 0.0, a2 = 0.0;

    switch (ftype) {
        case FilterStageType::LP2:
            b0 = (1.0 - cosW0) * 0.5;
            b1 = 1.0 - cosW0;
            b2 = b0;
            a0 = 1.0 + alpha;
            a1 = -2.0 * cosW0;
            a2 = 1.0 - alpha;
            break;
        case FilterStageType::HP2:
            b0 = (1.0 + cosW0) * 0.5;
            b1 = -(1.0 + cosW0);
            b2 = b0;
            a0 = 1.0 + alpha;
            a1 = -2.0 * cosW0;
            a2 = 1.0 - alpha;
            break;
        case FilterStageType::BP:
            b0 = alpha;
            b1 = 0.0;
            b2 = -alpha;
            a0 = 1.0 + alpha;
            a1 = -2.0 * cosW0;
            a2 = 1.0 - alpha;
            break;
        case FilterStageType::Peak: {
            const double A = std::pow(10.0, gainDb / 40.0);
            b0 = 1.0 + alpha * A;
            b1 = -2.0 * cosW0;
            b2 = 1.0 - alpha * A;
            a0 = 1.0 + alpha / A;
            a1 = -2.0 * cosW0;
            a2 = 1.0 - alpha / A;
            break;
        }
        case FilterStageType::LowShelf: {
            const double A = std::pow(10.0, gainDb / 40.0);
            const double sq = 2.0 * std::sqrt(A) * alpha;
            b0 = A * ((A + 1.0) - (A - 1.0) * cosW0 + sq);
            b1 = 2.0 * A * ((A - 1.0) - (A + 1.0) * cosW0);
            b2 = A * ((A + 1.0) - (A - 1.0) * cosW0 - sq);
            a0 = (A + 1.0) + (A - 1.0) * cosW0 + sq;
            a1 = -2.0 * ((A - 1.0) + (A + 1.0) * cosW0);
            a2 = (A + 1.0) + (A - 1.0) * cosW0 - sq;
            break;
        }
        case FilterStageType::HighShelf: {
            const double A = std::pow(10.0, gainDb / 40.0);
            const double sq = 2.0 * std::sqrt(A) * alpha;
            b0 = A * ((A + 1.0) + (A - 1.0) * cosW0 + sq);
            b1 = -2.0 * A * ((A - 1.0) + (A + 1.0) * cosW0);
            b2 = A * ((A + 1.0) + (A - 1.0) * cosW0 - sq);
            a0 = (A + 1.0) - (A - 1.0) * cosW0 + sq;
            a1 = 2.0 * ((A - 1.0) - (A + 1.0) * cosW0);
            a2 = (A + 1.0) - (A - 1.0) * cosW0 - sq;
            break;
        }
        default:
            break;
    }

    const double invA0 = 1.0 / a0;
    return BiquadCoeffs{
        b0 * invA0,
        b1 * invA0,
        b2 * invA0,
        a1 * invA0,
        a2 * invA0
    };
}

class OnePoleFilter {
public:
    void setCoeffs(const OnePoleCoeffs& coeffs) noexcept {
        c_ = coeffs;
    }

    void reset() noexcept {
        z1_ = 0.0;
    }

    [[nodiscard]] double processSample(double x) noexcept {
        const double y = c_.b0 * x + z1_;
        z1_ = c_.b1 * x - c_.a1 * y;
        return y;
    }

    void processBlock(double* buf, std::size_t n) noexcept {
        for (std::size_t i = 0; i < n; ++i) {
            buf[i] = processSample(buf[i]);
        }
    }

    [[nodiscard]] double maxPoleRadius() const noexcept {
        return c_.maxPoleRadius();
    }

private:
    OnePoleCoeffs c_{};
    double z1_{0.0};
};

class BiquadFilter {
public:
    void setCoeffs(const BiquadCoeffs& coeffs) noexcept {
        c_ = coeffs;
    }

    void reset() noexcept {
        z1_ = 0.0;
        z2_ = 0.0;
    }

    [[nodiscard]] double processSample(double x) noexcept {
        const double y = c_.b0 * x + z1_;
        z1_ = c_.b1 * x - c_.a1 * y + z2_;
        z2_ = c_.b2 * x - c_.a2 * y;
        return y;
    }

    void processBlock(double* buf, std::size_t n) noexcept {
        for (std::size_t i = 0; i < n; ++i) {
            buf[i] = processSample(buf[i]);
        }
    }

    [[nodiscard]] double maxPoleRadius() const noexcept {
        return c_.maxPoleRadius();
    }

private:
    BiquadCoeffs c_{};
    double z1_{0.0};
    double z2_{0.0};
};

template <std::size_t MaxStages>
class CascadeFilter {
public:
    template <std::size_t N>
    void configure(const std::array<FilterStageSpec, N>& specs, double gainDb, double fs) noexcept {
        static_assert(N <= MaxStages, "Exceeded MaxStages");
        numStages_ = N;
        linearGain_ = std::pow(10.0, gainDb / 20.0);
        for (std::size_t i = 0; i < N; ++i) {
            const auto& st = specs[i];
            if (st.type == FilterStageType::HP1) {
                isOnePole_[i] = true;
                onePoles_[i].setCoeffs(designOnePoleHP(st.fc, fs));
            } else if (st.type == FilterStageType::LP1) {
                isOnePole_[i] = true;
                onePoles_[i].setCoeffs(designOnePoleLP(st.fc, fs));
            } else {
                isOnePole_[i] = false;
                biquads_[i].setCoeffs(designRbjBiquad(st.type, st.fc, st.q, st.gainDb, fs));
            }
        }
    }

    void reset() noexcept {
        for (std::size_t i = 0; i < numStages_; ++i) {
            onePoles_[i].reset();
            biquads_[i].reset();
        }
    }

    [[nodiscard]] double processSample(double x) noexcept {
        double y = x * linearGain_;
        for (std::size_t i = 0; i < numStages_; ++i) {
            y = isOnePole_[i] ? onePoles_[i].processSample(y) : biquads_[i].processSample(y);
        }
        return y;
    }

    void processBlock(const double* src, double* dst, std::size_t n) noexcept {
        for (std::size_t i = 0; i < n; ++i) {
            dst[i] = processSample(src[i]);
        }
    }

    [[nodiscard]] double maxPoleRadius() const noexcept {
        double maxR = 0.0;
        for (std::size_t i = 0; i < numStages_; ++i) {
            const double r = isOnePole_[i] ? onePoles_[i].maxPoleRadius() : biquads_[i].maxPoleRadius();
            maxR = std::max(maxR, r);
        }
        return maxR;
    }

private:
    std::size_t numStages_{0};
    double linearGain_{1.0};
    std::array<bool, MaxStages> isOnePole_{};
    std::array<OnePoleFilter, MaxStages> onePoles_{};
    std::array<BiquadFilter, MaxStages> biquads_{};
};

} // namespace et5m
