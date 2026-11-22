#pragma once

#include "Biquad.hpp"
#include "BlockingOscillatorBank.hpp"
#include "ET5MParams.hpp"
#include "MidiRouter.hpp"
#include "TubeCabinetStage.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace et5m {

struct EngineState {
    std::array<bool, NUM_STOPS> activeStops{
        false, true, false, false, false, // Upper: FLUTE default
        false, false, false,              // Lower
        false, false                      // Pedal
    };
    bool upperSustain{false};
    bool kneeSustain{false};
    bool pedalSustain{false};
    double sustainTimeMs{kUpperSustainNominalTauMs};
    bool vibratoOn{false};
    bool tremoloOn{false};
    double repeatSpeed{0.98};   // 0..1 -> kRepeatSpeedMinHz..kRepeatSpeedMaxHz
    double manualBalance{0.5};  // 0..1 -> 0.5 center
    double pedalVolume{0.85};   // 0..1
    double expression{1.0};     // 0..1
    double masterVolume{1.0};   // 0..1

    // Circuit Authenticity Calibration
    bool authentic13Tuning{true};
    double syncBleed{kDefaultSyncBleed};
    double cabinetMix{1.0};     // 0 = Direct Circuit Out, 1 = 1965 Tube Amp & Cabinet
    double tubeAmount{1.0};     // 0..1.5
    double humAmount{0.0};      // 0..1.0
    double stereoWidth{0.35};   // 0..1.0
    bool polyPedal{false};
};

class ET5MEngine {
public:
    static constexpr std::size_t kMaxSubBlock = 128;
    static constexpr int kMinNote = 36;
    static constexpr int kMaxNote = 96;
    static constexpr std::size_t kNumNotes = static_cast<std::size_t>(kMaxNote - kMinNote + 1);

    void prepare(double sampleRate) noexcept {
        fs_ = sampleRate;
        oscBank_.prepare(fs_);
        for (std::size_t s = 0; s < NUM_STOPS; ++s) {
            stopFilters_[s].configure(kStopSpecs[s].stages, kStopSpecs[s].gainDb, fs_);
        }
        tubeCabinet_.prepare(fs_);
        tremEnvLp_.setCoeffs(designOnePoleLP(28.0, fs_));
        reset();
    }

    void reset() noexcept {
        oscBank_.resetPhases();
        for (auto& f : stopFilters_) {
            f.reset();
        }
        tubeCabinet_.reset();
        tremEnvLp_.reset();
        upperHeldCount_.fill(0);
        upperEnv_.fill(0.0);
        lowerHeldCount_.fill(0);
        lowerEnv_.fill(0.0);
        pedalHeldCount_.fill(0);
        pedalActiveNote_ = -1;
        pedalLatchedNote_ = -1;
        pedalMonoEnv_ = 0.0;
        lfoPhase_ = 0.0;
        midiRouter_.reset();
    }

    void setState(const EngineState& st) noexcept {
        state_ = st;
    }

    [[nodiscard]] const EngineState& state() const noexcept {
        return state_;
    }

    [[nodiscard]] EngineState& mutableState() noexcept {
        return state_;
    }

    void setRefA4Hz(double refA4Hz) noexcept {
        oscBank_.setRefA4Hz(refA4Hz);
    }

    [[nodiscard]] const BlockingOscillatorBank& oscillatorBank() const noexcept {
        return oscBank_;
    }

    [[nodiscard]] const TubeCabinetStage& tubeCabinet() const noexcept {
        return tubeCabinet_;
    }

    [[nodiscard]] double stopMaxPoleRadius(std::size_t stopIdx) const noexcept {
        return (stopIdx < NUM_STOPS) ? stopFilters_[stopIdx].maxPoleRadius() : 0.0;
    }

    void noteOn(ManualId manual, int midiNote) noexcept {
        if (midiNote < kMinNote || midiNote > kMaxNote) return;
        const std::size_t idx = static_cast<std::size_t>(midiNote - kMinNote);
        if (manual == ManualId::Upper && midiNote >= MidiRouter::kUpperMin && midiNote <= MidiRouter::kUpperMax) {
            ++upperHeldCount_[idx];
        } else if (manual == ManualId::Lower && midiNote >= MidiRouter::kLowerMin && midiNote <= MidiRouter::kLowerMax) {
            ++lowerHeldCount_[idx];
        } else if (manual == ManualId::Pedal && midiNote >= MidiRouter::kPedalMin && midiNote <= MidiRouter::kPedalMax) {
            ++pedalHeldCount_[idx];
            updateLowNotePedal();
        }
    }

    void noteOff(ManualId manual, int midiNote) noexcept {
        if (midiNote < kMinNote || midiNote > kMaxNote) return;
        const std::size_t idx = static_cast<std::size_t>(midiNote - kMinNote);
        if (manual == ManualId::Upper) {
            if (upperHeldCount_[idx] > 0) --upperHeldCount_[idx];
        } else if (manual == ManualId::Lower) {
            if (lowerHeldCount_[idx] > 0) --lowerHeldCount_[idx];
        } else if (manual == ManualId::Pedal) {
            if (pedalHeldCount_[idx] > 0) --pedalHeldCount_[idx];
            updateLowNotePedal();
        }
    }

    void handleMidiNoteOn(int channel1to16, int rawMidiNote, const MidiRoutingConfig& cfg) noexcept {
        midiRouter_.handleNoteOn(channel1to16, rawMidiNote, cfg, [this](const RoutedNoteEvent& ev) {
            noteOn(ev.manual, ev.midiNote);
        });
    }

    void handleMidiNoteOff(int channel1to16, int rawMidiNote) noexcept {
        midiRouter_.handleNoteOff(channel1to16, rawMidiNote, [this](const RoutedNoteEvent& ev) {
            noteOff(ev.manual, ev.midiNote);
        });
    }

    void allNotesOff() noexcept {
        midiRouter_.allNotesOff([this](const RoutedNoteEvent& ev) {
            noteOff(ev.manual, ev.midiNote);
        });
        upperHeldCount_.fill(0);
        lowerHeldCount_.fill(0);
        pedalHeldCount_.fill(0);
        pedalActiveNote_ = -1;
    }

    void manualNotesOff(ManualId manual) noexcept {
        if (manual == ManualId::Upper) {
            upperHeldCount_.fill(0);
        } else if (manual == ManualId::Lower) {
            lowerHeldCount_.fill(0);
        } else {
            pedalHeldCount_.fill(0);
            pedalActiveNote_ = -1;
        }
    }

    [[nodiscard]] int activeUpperNoteCount() const noexcept {
        int c = 0;
        for (std::size_t i = 0; i < kNumNotes; ++i) {
            if (upperHeldCount_[i] > 0 || upperEnv_[i] > 1e-4) ++c;
        }
        return c;
    }

    [[nodiscard]] int activeLowerNoteCount() const noexcept {
        int c = 0;
        for (std::size_t i = 0; i < kNumNotes; ++i) {
            if (lowerHeldCount_[i] > 0 || lowerEnv_[i] > 1e-4) ++c;
        }
        return c;
    }

    [[nodiscard]] int activePedalMidiNote() const noexcept {
        return pedalActiveNote_;
    }

    [[nodiscard]] double lfoRateHz() const noexcept {
        const double rs = std::clamp(state_.repeatSpeed, 0.0, 1.0);
        return kRepeatSpeedMinHz + rs * (kRepeatSpeedMaxHz - kRepeatSpeedMinHz);
    }

    void processBlock(
        double* mixL,
        double* mixR,
        double* dirL,
        double* dirR,
        double* cabL,
        double* cabR,
        std::size_t numSamples
    ) noexcept {
        std::size_t offset = 0;
        while (offset < numSamples) {
            const std::size_t n = std::min(kMaxSubBlock, numSamples - offset);
            processSubBlock(
                mixL + offset, mixR + offset,
                dirL ? (dirL + offset) : nullptr, dirR ? (dirR + offset) : nullptr,
                cabL ? (cabL + offset) : nullptr, cabR ? (cabR + offset) : nullptr,
                n
            );
            offset += n;
        }
    }

private:
    void updateLowNotePedal() noexcept {
        pedalActiveNote_ = -1;
        for (int m = MidiRouter::kPedalMin; m <= MidiRouter::kPedalMax; ++m) {
            if (pedalHeldCount_[static_cast<std::size_t>(m - kMinNote)] > 0) {
                pedalActiveNote_ = m;
                pedalLatchedNote_ = m;
                break;
            }
        }
    }

    void computeManualBalanceGains(double& outUpper, double& outLower) const noexcept {
        const double b = std::clamp(state_.manualBalance, 0.0, 1.0);
        const double delta = (b - 0.5) * 2.0;
        const double uDb = (delta >= 0.0) ? (3.2 * delta) : (7.2 * delta);
        const double lDb = (delta >= 0.0) ? (-7.2 * delta) : (-3.2 * delta);
        outUpper = std::pow(10.0, uDb / 20.0);
        outLower = std::pow(10.0, lDb / 20.0);
    }

    [[nodiscard]] double computePedalVolumeGain() const noexcept {
        const double pv = std::clamp(state_.pedalVolume, 0.0, 1.0);
        if (pv <= 1e-4) return 0.0;
        return std::pow(pv / 0.85, 1.35);
    }

    void processSubBlock(
        double* mixL,
        double* mixR,
        double* dirL,
        double* dirR,
        double* cabL,
        double* cabR,
        std::size_t n
    ) noexcept {
        // 1. Shared REPEAT SPEED LFO
        const double rateHz = lfoRateHz();
        const double phInc = rateHz / fs_;
        for (std::size_t i = 0; i < n; ++i) {
            lfoSin_[i] = std::sin(2.0 * kPi * lfoPhase_);
            lfoPhase_ += phInc;
            if (lfoPhase_ >= 1.0) lfoPhase_ -= 1.0;
        }

        // 2. Advance 7 masters
        std::array<double*, 7> masterPtrs{};
        std::array<const double*, 7> masterConstPtrs{};
        for (std::size_t m = 0; m < 7; ++m) {
            masterPtrs[m] = masterPhaseBuf_[m].data();
            masterConstPtrs[m] = masterPhaseBuf_[m].data();
        }
        oscBank_.advanceMasters(
            n,
            lfoSin_.data(),
            kVibratoHalfDepthCents,
            kVibratoDcShiftCents,
            state_.vibratoOn,
            masterPtrs
        );

        // 3. Envelopes & Oscillator excitation per manual
        const double tauAtk = kAttackTauMs * 1e-3;
        const double tauFastRel = kFastReleaseTauMs * 1e-3;
        const double upperRel = (state_.upperSustain || state_.kneeSustain)
            ? (state_.sustainTimeMs * 1e-3)
            : tauFastRel;
        const double pedalRel = state_.pedalSustain
            ? (state_.sustainTimeMs * 1.15 * 1e-3)
            : (kPedalFastReleaseTauMs * 1e-3);

        const double aAtk = 1.0 - std::exp(-1.0 / std::max(1e-5, tauAtk * fs_));
        const double aPedAtk = 1.0 - std::exp(-1.0 / std::max(1e-5, tauAtk * 1.5 * fs_));
        const double aUpRel = std::exp(-1.0 / std::max(1e-5, upperRel * fs_));
        const double aLoRel = std::exp(-1.0 / std::max(1e-5, tauFastRel * fs_));
        const double aPedRel = std::exp(-1.0 / std::max(1e-5, pedalRel * fs_));

        std::fill_n(uBus_.data(), n, 0.0);
        std::fill_n(lBus_.data(), n, 0.0);
        std::fill_n(pBus_.data(), n, 0.0);
        bool uHasSignal = false;
        bool lHasSignal = false;
        bool pHasSignal = false;

        for (int note = kMinNote; note <= kMaxNote; ++note) {
            const std::size_t idx = static_cast<std::size_t>(note - kMinNote);
            const bool upperHeld = (upperHeldCount_[idx] > 0);
            const bool lowerHeld = (lowerHeldCount_[idx] > 0);
            const bool needUpper = (note >= MidiRouter::kUpperMin && note <= MidiRouter::kUpperMax)
                && (upperHeld || upperEnv_[idx] >= 1e-6);
            const bool needLower = (note >= MidiRouter::kLowerMin && note <= MidiRouter::kLowerMax)
                && (lowerHeld || lowerEnv_[idx] >= 1e-6);

            if (!needUpper && !needLower) {
                upperEnv_[idx] = 0.0;
                lowerEnv_[idx] = 0.0;
                continue;
            }

            oscBank_.renderNote(note, masterConstPtrs, n, state_.authentic13Tuning, state_.syncBleed, noteBuf_.data());

            if (needUpper) {
                uHasSignal = true;
                double e = upperEnv_[idx];
                if (upperHeld) {
                    for (std::size_t i = 0; i < n; ++i) {
                        e += aAtk * (1.0 - e);
                        uBus_[i] += e * noteBuf_[i];
                    }
                } else {
                    for (std::size_t i = 0; i < n; ++i) {
                        e *= aUpRel;
                        uBus_[i] += e * noteBuf_[i];
                    }
                }
                upperEnv_[idx] = (e < 1e-6 && !upperHeld) ? 0.0 : e;
            }

            if (needLower) {
                lHasSignal = true;
                double e = lowerEnv_[idx];
                if (lowerHeld) {
                    for (std::size_t i = 0; i < n; ++i) {
                        e += aAtk * (1.0 - e);
                        lBus_[i] += e * noteBuf_[i];
                    }
                } else {
                    for (std::size_t i = 0; i < n; ++i) {
                        e *= aLoRel;
                        lBus_[i] += e * noteBuf_[i];
                    }
                }
                lowerEnv_[idx] = (e < 1e-6 && !lowerHeld) ? 0.0 : e;
            }
        }

        // Strict monophonic low-note-priority Pedalboard (always single voice)
        if (pedalActiveNote_ >= 0) {
            pedalLatchedNote_ = pedalActiveNote_;
        }
        if (pedalLatchedNote_ >= MidiRouter::kPedalMin
            && pedalLatchedNote_ <= MidiRouter::kPedalMax
            && (pedalActiveNote_ >= 0 || pedalMonoEnv_ >= 1e-6)) {
            pHasSignal = true;
            oscBank_.renderNote(pedalLatchedNote_, masterConstPtrs, n, state_.authentic13Tuning, state_.syncBleed, noteBuf_.data());
            double e = pedalMonoEnv_;
            if (pedalActiveNote_ >= 0) {
                for (std::size_t i = 0; i < n; ++i) {
                    e += aPedAtk * (1.0 - e);
                    pBus_[i] += e * noteBuf_[i];
                }
            } else {
                for (std::size_t i = 0; i < n; ++i) {
                    e *= aPedRel;
                    pBus_[i] += e * noteBuf_[i];
                }
            }
            if (e < 1e-6 && pedalActiveNote_ < 0) {
                pedalMonoEnv_ = 0.0;
                pedalLatchedNote_ = -1;
            } else {
                pedalMonoEnv_ = e;
            }
        } else {
            pedalMonoEnv_ = 0.0;
            if (pedalActiveNote_ < 0) {
                pedalLatchedNote_ = -1;
            }
        }

        // 4. Route through active stop formant filters
        std::fill_n(uSum_.data(), n, 0.0);
        std::fill_n(lSum_.data(), n, 0.0);
        std::fill_n(pSum_.data(), n, 0.0);

        int nUStops = 0;
        for (std::size_t s = STOP_UPPER_DIAPASON; s <= STOP_UPPER_TRUMPET; ++s) {
            if (state_.activeStops[s]) ++nUStops;
        }
        if (nUStops > 0 && uHasSignal) {
            const double uLoad = 1.0 / std::sqrt(1.0 + 0.14 * static_cast<double>(nUStops - 1));
            for (std::size_t s = STOP_UPPER_DIAPASON; s <= STOP_UPPER_TRUMPET; ++s) {
                if (state_.activeStops[s]) {
                    stopFilters_[s].processBlock(uBus_.data(), filtTmp_.data(), n);
                    for (std::size_t i = 0; i < n; ++i) uSum_[i] += filtTmp_[i] * uLoad;
                }
            }
        }

        int nLStops = 0;
        for (std::size_t s = STOP_LOWER_DIAPASON; s <= STOP_LOWER_HORN; ++s) {
            if (state_.activeStops[s]) ++nLStops;
        }
        if (nLStops > 0 && lHasSignal) {
            const double lLoad = 1.0 / std::sqrt(1.0 + 0.14 * static_cast<double>(nLStops - 1));
            for (std::size_t s = STOP_LOWER_DIAPASON; s <= STOP_LOWER_HORN; ++s) {
                if (state_.activeStops[s]) {
                    stopFilters_[s].processBlock(lBus_.data(), filtTmp_.data(), n);
                    for (std::size_t i = 0; i < n; ++i) lSum_[i] += filtTmp_[i] * lLoad;
                }
            }
        }

        int nPStops = 0;
        for (std::size_t s = STOP_PEDAL_BOURDON; s <= STOP_PEDAL_TROMBONE; ++s) {
            if (state_.activeStops[s]) ++nPStops;
        }
        if (nPStops > 0 && pHasSignal) {
            const double pLoad = 1.0 / std::sqrt(1.0 + 0.14 * static_cast<double>(nPStops - 1));
            for (std::size_t s = STOP_PEDAL_BOURDON; s <= STOP_PEDAL_TROMBONE; ++s) {
                if (state_.activeStops[s]) {
                    stopFilters_[s].processBlock(pBus_.data(), filtTmp_.data(), n);
                    for (std::size_t i = 0; i < n; ++i) pSum_[i] += filtTmp_[i] * pLoad;
                }
            }
        }

        // 5. Manual Balance, Pedal Volume, Expression & Tremolo AM
        double gU = 1.0, gL = 1.0;
        computeManualBalanceGains(gU, gL);
        const double gP = computePedalVolumeGain();
        const double masterMul = state_.expression * state_.masterVolume;

        for (std::size_t i = 0; i < n; ++i) {
            const double tremTarget = state_.tremoloOn ? (1.0 - kTremoloModIndex * lfoSin_[i]) : 1.0;
            const double tremGain = tremEnvLp_.processSample(tremTarget);
            const double mono = (uSum_[i] * gU + lSum_[i] * gL + pSum_[i] * gP) * masterMul * tremGain;
            circuitMono_[i] = mono;
        }

        // 6. Tube Amp & Cloth-Baffle Cabinet Stage + Blend
        tubeCabinet_.processBlock(
            circuitMono_.data(),
            cabTmpL_.data(),
            cabTmpR_.data(),
            n,
            state_.tubeAmount,
            state_.humAmount,
            state_.stereoWidth
        );

        const double cm = std::clamp(state_.cabinetMix, 0.0, 1.0);
        for (std::size_t i = 0; i < n; ++i) {
            const double d = circuitMono_[i];
            const double cl = cabTmpL_[i];
            const double cr = cabTmpR_[i];
            if (dirL) dirL[i] = d;
            if (dirR) dirR[i] = d;
            if (cabL) cabL[i] = cl;
            if (cabR) cabR[i] = cr;
            mixL[i] = (1.0 - cm) * d + cm * cl;
            mixR[i] = (1.0 - cm) * d + cm * cr;
        }
    }

    double fs_{48000.0};
    EngineState state_{};
    BlockingOscillatorBank oscBank_{};
    std::array<CascadeFilter<kStopNumStages>, NUM_STOPS> stopFilters_{};
    TubeCabinetStage tubeCabinet_{};
    OnePoleFilter tremEnvLp_{};
    MidiRouter midiRouter_{};

    std::array<int, kNumNotes> upperHeldCount_{};
    std::array<double, kNumNotes> upperEnv_{};
    std::array<int, kNumNotes> lowerHeldCount_{};
    std::array<double, kNumNotes> lowerEnv_{};
    std::array<int, kNumNotes> pedalHeldCount_{};
    int pedalActiveNote_{-1};
    int pedalLatchedNote_{-1};
    double pedalMonoEnv_{0.0};
    double lfoPhase_{0.0};

    // Pre-allocated real-time scratch buffers
    std::array<double, kMaxSubBlock> lfoSin_{};
    std::array<std::array<double, kMaxSubBlock>, 7> masterPhaseBuf_{};
    std::array<double, kMaxSubBlock> noteBuf_{};
    std::array<double, kMaxSubBlock> uBus_{};
    std::array<double, kMaxSubBlock> lBus_{};
    std::array<double, kMaxSubBlock> pBus_{};
    std::array<double, kMaxSubBlock> uSum_{};
    std::array<double, kMaxSubBlock> lSum_{};
    std::array<double, kMaxSubBlock> pSum_{};
    std::array<double, kMaxSubBlock> filtTmp_{};
    std::array<double, kMaxSubBlock> circuitMono_{};
    std::array<double, kMaxSubBlock> cabTmpL_{};
    std::array<double, kMaxSubBlock> cabTmpR_{};
};

} // namespace et5m
