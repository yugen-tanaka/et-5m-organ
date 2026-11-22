#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "dsp/ET5MEngine.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <vector>

using namespace et5m;

namespace {

double computeRmsDb(const std::vector<double>& buf, std::size_t startIdx = 0) {
    double sumSq = 0.0;
    const std::size_t n = buf.size() - startIdx;
    for (std::size_t i = startIdx; i < buf.size(); ++i) {
        sumSq += buf[i] * buf[i];
    }
    const double rms = std::sqrt(sumSq / static_cast<double>(std::max<std::size_t>(1, n)));
    return 20.0 * std::log10(std::max(rms, 1e-12));
}

void renderEngine(
    ET5MEngine& eng,
    std::size_t nSamples,
    std::vector<double>& mixL,
    std::vector<double>& mixR,
    std::vector<double>& dirL,
    std::vector<double>& dirR,
    std::vector<double>& cabL,
    std::vector<double>& cabR,
    std::size_t blockSize = 128
) {
    mixL.resize(nSamples); mixR.resize(nSamples);
    dirL.resize(nSamples); dirR.resize(nSamples);
    cabL.resize(nSamples); cabR.resize(nSamples);
    std::size_t pos = 0;
    while (pos < nSamples) {
        const std::size_t n = std::min(blockSize, nSamples - pos);
        eng.processBlock(
            mixL.data() + pos, mixR.data() + pos,
            dirL.data() + pos, dirR.data() + pos,
            cabL.data() + pos, cabR.data() + pos,
            n
        );
        pos += n;
    }
}

} // namespace

TEST_CASE("1. Multirate Filter Stability & Finite Output", "[dsp][multirate]") {
    for (double fs : {44100.0, 48000.0, 88200.0, 96000.0}) {
        ET5MEngine eng;
        eng.prepare(fs);
        REQUIRE(eng.tubeCabinet().maxPoleRadius() <= 0.9985);
        for (std::size_t s = 0; s < NUM_STOPS; ++s) {
            REQUIRE(eng.stopMaxPoleRadius(s) <= 0.9985);
        }

        EngineState st;
        st.activeStops.fill(false);
        st.activeStops[STOP_UPPER_DIAPASON] = true;
        st.activeStops[STOP_UPPER_OBOE] = true;
        st.activeStops[STOP_LOWER_HORN] = true;
        st.activeStops[STOP_PEDAL_BOURDON] = true;
        st.vibratoOn = true;
        st.tremoloOn = true;
        eng.setState(st);

        eng.noteOn(ManualId::Upper, 67);
        eng.noteOn(ManualId::Upper, 72);
        eng.noteOn(ManualId::Lower, 52);
        eng.noteOn(ManualId::Pedal, 36);

        const std::size_t n = static_cast<std::size_t>(0.35 * fs);
        std::vector<double> mixL, mixR, dirL, dirR, cabL, cabR;
        renderEngine(eng, n, mixL, mixR, dirL, dirR, cabL, cabR);

        double maxAbs = 0.0;
        for (std::size_t i = 0; i < n; ++i) {
            REQUIRE(std::isfinite(mixL[i]));
            REQUIRE(std::isfinite(mixR[i]));
            maxAbs = std::max({maxAbs, std::abs(mixL[i]), std::abs(mixR[i])});
        }
        REQUIRE(maxAbs > 1e-4);
    }
}

TEST_CASE("2. 7-Master + 1/3-Divider Exact Frequency Ratios & Phase Lock", "[dsp][tuning]") {
    ET5MEngine eng;
    eng.prepare(48000.0);
    const auto& osc = eng.oscillatorBank();

    // C6(84) / F4(65) == 3.0; B5(83) / E4(64) == 3.0
    const double fC6 = osc.noteFrequency(84, true);
    const double fF4 = osc.noteFrequency(65, true);
    REQUIRE_THAT(fC6 / fF4, Catch::Matchers::WithinAbs(3.0, 1e-9));

    const double fB5 = osc.noteFrequency(83, true);
    const double fE4 = osc.noteFrequency(64, true);
    REQUIRE_THAT(fB5 / fE4, Catch::Matchers::WithinAbs(3.0, 1e-9));

    const double fE4Et = osc.noteFrequency(64, false);
    const double centsDiff = 1200.0 * std::log2(fE4 / fE4Et);
    REQUIRE_THAT(centsDiff, Catch::Matchers::WithinAbs(-1.9550, 1e-3));

    // Phase-locked octaves C4 + C5 should exhibit zero beating
    EngineState st;
    st.activeStops.fill(false);
    st.activeStops[STOP_UPPER_DIAPASON] = true;
    st.cabinetMix = 0.0;
    eng.setState(st);
    eng.noteOn(ManualId::Upper, 60);
    eng.noteOn(ManualId::Upper, 72);
    std::vector<double> mixL, mixR, dirL, dirR, cabL, cabR;
    renderEngine(eng, 48000, mixL, mixR, dirL, dirR, cabL, cabR);
    std::vector<double> rmsChunks;
    for (int c = 0; c < 4; ++c) {
        const std::size_t s0 = 4800 + static_cast<std::size_t>(c) * 8000;
        double sum = 0.0;
        for (std::size_t i = s0; i < s0 + 8000; ++i) sum += dirL[i] * dirL[i];
        rmsChunks.push_back(std::sqrt(sum / 8000.0));
    }
    const auto [mn, mx] = std::minmax_element(rmsChunks.begin(), rmsChunks.end());
    REQUIRE((*mx - *mn) / *mx < 0.01);
}

TEST_CASE("3. Manual Ranges & Pedal Low-Note Priority", "[dsp][manuals]") {
    ET5MEngine eng;
    eng.prepare(48000.0);

    // Out-of-range notes ignored
    eng.noteOn(ManualId::Upper, 52);
    eng.noteOn(ManualId::Upper, 97);
    eng.noteOn(ManualId::Lower, 35);
    eng.noteOn(ManualId::Lower, 80);
    eng.noteOn(ManualId::Pedal, 49);
    REQUIRE(eng.activeUpperNoteCount() == 0);
    REQUIRE(eng.activeLowerNoteCount() == 0);
    REQUIRE(eng.activePedalMidiNote() == -1);

    // Low-note priority on Pedal
    eng.noteOn(ManualId::Pedal, 45);
    REQUIRE(eng.activePedalMidiNote() == 45);
    eng.noteOn(ManualId::Pedal, 38);
    REQUIRE(eng.activePedalMidiNote() == 38);
    eng.noteOff(ManualId::Pedal, 38);
    REQUIRE(eng.activePedalMidiNote() == 45);
}

TEST_CASE("4. Upper & Knee Sustain Exponential Decay", "[dsp][sustain]") {
    auto measureTau = [](bool upSust, bool kneeSust, double sustTimeMs) {
        ET5MEngine eng;
        eng.prepare(48000.0);
        EngineState st;
        st.activeStops.fill(false);
        st.activeStops[STOP_UPPER_FLUTE] = true;
        st.upperSustain = upSust;
        st.kneeSustain = kneeSust;
        st.sustainTimeMs = sustTimeMs;
        eng.setState(st);
        eng.noteOn(ManualId::Upper, 67);
        std::vector<double> mixL, mixR, dirL, dirR, cabL, cabR;
        renderEngine(eng, 9600, mixL, mixR, dirL, dirR, cabL, cabR);
        eng.noteOff(ManualId::Upper, 67);
        renderEngine(eng, 28800, mixL, mixR, dirL, dirR, cabL, cabR);

        auto rmsRange = [&](std::size_t a, std::size_t b) {
            double s = 0.0;
            for (std::size_t i = a; i < b; ++i) s += cabL[i] * cabL[i];
            return std::sqrt(s / static_cast<double>(b - a));
        };
        const double r1 = rmsRange(1440, 2880);   // 30..60ms
        const double r2 = rmsRange(15840, 17280); // 330..360ms
        const double dbDrop = 20.0 * std::log10((r1 + 1e-12) / (r2 + 1e-12));
        return (0.30 * 8.6858896) / std::max(dbDrop, 1e-6) * 1e3;
    };

    const double tauOff = measureTau(false, false, 257.0);
    const double tauTab = measureTau(true, false, 257.0);
    const double tauKnee = measureTau(false, true, 257.0);
    const double tauLong = measureTau(true, false, 650.0);

    REQUIRE(tauOff < 40.0);
    REQUIRE(tauTab > 220.0);
    REQUIRE(tauTab < 295.0);
    REQUIRE(std::abs(tauTab - tauKnee) < 5.0);
    REQUIRE(tauLong > 550.0);
}

TEST_CASE("5. Shared REPEAT SPEED LFO Driving Vibrato & Tremolo", "[dsp][lfo]") {
    ET5MEngine eng;
    eng.prepare(48000.0);
    EngineState st;
    st.activeStops.fill(false);
    st.activeStops[STOP_UPPER_FLUTE] = true;
    st.repeatSpeed = 0.0;
    eng.setState(st);
    const double rMin = eng.lfoRateHz();
    st.repeatSpeed = 1.0;
    eng.setState(st);
    const double rMax = eng.lfoRateHz();
    REQUIRE(rMin >= 4.8);
    REQUIRE(rMin <= 5.5);
    REQUIRE(rMax >= 8.1);
    REQUIRE(rMax <= 8.8);

    // Measure Tremolo AM swing on Direct output
    st.repeatSpeed = 0.96;
    st.tremoloOn = true;
    st.cabinetMix = 0.0;
    eng.setState(st);
    eng.noteOn(ManualId::Upper, 67);
    std::vector<double> mixL, mixR, dirL, dirR, cabL, cabR;
    renderEngine(eng, 57600, mixL, mixR, dirL, dirR, cabL, cabR);
    double minRms = 100.0, maxRms = -200.0;
    for (std::size_t i = 9600; i + 480 < 57600; i += 120) {
        double s = 0.0;
        for (std::size_t j = 0; j < 480; ++j) s += dirL[i + j] * dirL[i + j];
        const double d = 20.0 * std::log10(std::sqrt(s / 480.0) + 1e-12);
        minRms = std::min(minRms, d);
        maxRms = std::max(maxRms, d);
    }
    const double swing = maxRms - minRms;
    REQUIRE(swing >= 4.4);
    REQUIRE(swing <= 6.2);
}

TEST_CASE("6. Manual Balance & Pedal Volume Controls", "[dsp][controls]") {
    auto renderManualDb = [](ManualId man, int midi, std::size_t stopIdx, double bal, double pedVol) {
        ET5MEngine eng;
        eng.prepare(48000.0);
        EngineState st;
        st.activeStops.fill(false);
        st.activeStops[stopIdx] = true;
        st.manualBalance = bal;
        st.pedalVolume = pedVol;
        eng.setState(st);
        eng.noteOn(man, midi);
        std::vector<double> mixL, mixR, dirL, dirR, cabL, cabR;
        renderEngine(eng, 16800, mixL, mixR, dirL, dirR, cabL, cabR);
        return computeRmsDb(cabL, 4800);
    };

    const double uLow = renderManualDb(ManualId::Upper, 67, STOP_UPPER_FLUTE, 0.0, 0.85);
    const double uHi = renderManualDb(ManualId::Upper, 67, STOP_UPPER_FLUTE, 1.0, 0.85);
    const double lLow = renderManualDb(ManualId::Lower, 64, STOP_LOWER_DIAPASON, 0.0, 0.85);
    const double lHi = renderManualDb(ManualId::Lower, 64, STOP_LOWER_DIAPASON, 1.0, 0.85);
    REQUIRE((uHi - uLow) > 8.0);
    REQUIRE((lLow - lHi) > 8.0);

    const double pHi = renderManualDb(ManualId::Pedal, 36, STOP_PEDAL_BOURDON, 0.5, 0.85);
    const double pMid = renderManualDb(ManualId::Pedal, 36, STOP_PEDAL_BOURDON, 0.5, 0.45);
    REQUIRE((pHi - pMid) > 6.0);
}

TEST_CASE("7. Direct Circuit vs 1965 Tube Amp & Cloth-Baffle Cabinet Separation", "[dsp][cabinet]") {
    ET5MEngine eng;
    eng.prepare(48000.0);
    EngineState st;
    st.activeStops.fill(false);
    st.activeStops[STOP_UPPER_OBOE] = true;
    st.humAmount = 0.0;
    eng.setState(st);
    eng.noteOn(ManualId::Upper, 67);
    std::vector<double> mixL, mixR, dirL, dirR, cabL, cabR;
    renderEngine(eng, 19200, mixL, mixR, dirL, dirR, cabL, cabR);

    double maxDirDiff = 0.0, maxCabDiff = 0.0;
    for (std::size_t i = 0; i < dirL.size(); ++i) {
        maxDirDiff = std::max(maxDirDiff, std::abs(dirL[i] - dirR[i]));
        maxCabDiff = std::max(maxCabDiff, std::abs(cabL[i] - cabR[i]));
    }
    REQUIRE(maxDirDiff == 0.0);
    REQUIRE(maxCabDiff > 1e-5);

    eng.reset();
    renderEngine(eng, 4800, mixL, mixR, dirL, dirR, cabL, cabR);
    for (double v : cabL) REQUIRE(std::abs(v) < 1e-12);

    st.humAmount = 1.0;
    eng.setState(st);
    renderEngine(eng, 12000, mixL, mixR, dirL, dirR, cabL, cabR);
    const double humDb = computeRmsDb(cabL, 0);
    REQUIRE(humDb > -80.0);
    REQUIRE(humDb < -45.0);
}

TEST_CASE("8. MidiRouter Channel Routing, Split & Stuck-Note Protection", "[dsp][midi]") {
    ET5MEngine eng;
    eng.prepare(48000.0);
    MidiRoutingConfig cfg;
    cfg.upperChannel = 1;
    cfg.lowerChannel = 2;
    cfg.pedalChannel = 3;

    // Channel 4 unassigned -> ignored
    eng.handleMidiNoteOn(4, 67, cfg);
    REQUIRE(eng.activeUpperNoteCount() == 0);
    REQUIRE(eng.activeLowerNoteCount() == 0);

    // Channel 1 -> Upper, then channel changed live before NoteOff -> no stuck note!
    eng.handleMidiNoteOn(1, 67, cfg);
    REQUIRE(eng.activeUpperNoteCount() == 1);
    cfg.upperChannel = 5;
    eng.handleMidiNoteOff(1, 67);
    REQUIRE(eng.activeUpperNoteCount() == 0);

    // Single-Keyboard Split mode
    cfg.singleKeyboardSplit = true;
    cfg.splitPointMidi = 60;
    cfg.pedalSplitTopMidi = 47;
    eng.handleMidiNoteOn(1, 40, cfg);
    eng.handleMidiNoteOn(1, 52, cfg);
    eng.handleMidiNoteOn(1, 67, cfg);
    REQUIRE(eng.activePedalMidiNote() == 40);
    REQUIRE(eng.activeLowerNoteCount() == 1);
    REQUIRE(eng.activeUpperNoteCount() == 1);
    eng.allNotesOff();
    REQUIRE(eng.activeUpperNoteCount() == 0);
    REQUIRE(eng.activeLowerNoteCount() == 0);
    REQUIRE(eng.activePedalMidiNote() == -1);
}

TEST_CASE("9. Block-Size Invariance (< 1e-12) & Real-Time Performance Benchmark", "[dsp][perf]") {
    auto runWithBlock = [](std::size_t bs) {
        ET5MEngine eng;
        eng.prepare(48000.0);
        EngineState st;
        st.activeStops = {true, true, false, true, false, true, false, true, true, false};
        st.upperSustain = true;
        st.vibratoOn = true;
        st.tremoloOn = true;
        eng.setState(st);
        eng.noteOn(ManualId::Upper, 60);
        eng.noteOn(ManualId::Upper, 64);
        eng.noteOn(ManualId::Upper, 67);
        eng.noteOn(ManualId::Lower, 48);
        eng.noteOn(ManualId::Pedal, 36);
        std::vector<double> mixL, mixR, dirL, dirR, cabL, cabR;
        renderEngine(eng, 24000, mixL, mixR, dirL, dirR, cabL, cabR, bs);
        return mixL;
    };

    const auto ref = runWithBlock(128);
    for (std::size_t bs : {std::size_t{37}, std::size_t{64}, std::size_t{512}}) {
        const auto cur = runWithBlock(bs);
        double maxDiff = 0.0;
        for (std::size_t i = 0; i < ref.size(); ++i) {
            maxDiff = std::max(maxDiff, std::abs(ref[i] - cur[i]));
        }
        REQUIRE(maxDiff < 1e-12);
    }

    // Benchmark 2.0s of 10-voice polyphony + all 10 stops + tube/cabinet
    ET5MEngine eng;
    eng.prepare(48000.0);
    EngineState st;
    st.activeStops.fill(true);
    st.vibratoOn = true;
    st.tremoloOn = true;
    st.upperSustain = true;
    eng.setState(st);
    for (int m : {60, 64, 67, 71, 74, 79}) eng.noteOn(ManualId::Upper, m);
    for (int m : {48, 52, 55}) eng.noteOn(ManualId::Lower, m);
    eng.noteOn(ManualId::Pedal, 36);

    std::vector<double> mixL(96000), mixR(96000);
    const auto t0 = std::chrono::steady_clock::now();
    for (std::size_t pos = 0; pos < 96000; pos += 128) {
        eng.processBlock(mixL.data() + pos, mixR.data() + pos, nullptr, nullptr, nullptr, nullptr, 128);
    }
    const auto t1 = std::chrono::steady_clock::now();
    const double elapsedS = std::chrono::duration<double>(t1 - t0).count();
    const double rtf = elapsedS / 2.0;
    INFO("2.0s 10-voice 10-stop benchmark elapsed=" << elapsedS << "s, RTF=" << rtf);
    REQUIRE(rtf < 0.15);
}

TEST_CASE("10. High-Register Anti-Aliasing (< -90 dB sub-f0 @ 44.1/48kHz)", "[dsp][antialiasing]") {
    auto dftMag = [](const std::vector<double>& sig, std::size_t startIdx, double fs, double freqHz) -> double {
        double re = 0.0;
        double im = 0.0;
        const std::size_t m = sig.size() - startIdx;
        const double invM = 1.0 / static_cast<double>(m);
        for (std::size_t i = 0; i < m; ++i) {
            const double w = 0.5 * (1.0 - std::cos(2.0 * kPi * static_cast<double>(i) * invM));
            const double ang = 2.0 * kPi * freqHz * static_cast<double>(i) / fs;
            const double x = sig[startIdx + i] * w;
            re += x * std::cos(ang);
            im -= x * std::sin(ang);
        }
        return std::sqrt(re * re + im * im);
    };

    for (double fs : {44100.0, 48000.0}) {
        for (int midi : {84, 88, 91, 93, 94, 95, 96}) {
            ET5MEngine eng;
            eng.prepare(fs);
            EngineState st;
            st.activeStops.fill(false);
            st.activeStops[STOP_UPPER_FLUTE] = true;
            st.activeStops[STOP_UPPER_DIAPASON] = true;
            st.activeStops[STOP_UPPER_VIOLIN] = true;
            st.activeStops[STOP_UPPER_TRUMPET] = true;
            st.tubeAmount = 1.0;
            st.humAmount = 0.0;
            st.cabinetMix = 1.0;
            eng.setState(st);
            eng.noteOn(ManualId::Upper, midi);

            const std::size_t n = static_cast<std::size_t>(0.5 * fs);
            const std::size_t start = static_cast<std::size_t>(0.1 * fs);
            std::vector<double> mixL, mixR, dirL, dirR, cabL, cabR;
            renderEngine(eng, n, mixL, mixR, dirL, dirR, cabL, cabR);

            const double f0 = eng.oscillatorBank().noteFrequency(midi, true);
            const double h1Mag = dftMag(mixL, start, fs, f0);
            REQUIRE(h1Mag > 1e-6);

            // Scan 40 Hz .. 0.85 * f0 in 12 Hz steps to catch any aliased difference tones
            double maxSubDb = -200.0;
            for (double fProbe = 40.0; fProbe < f0 * 0.85; fProbe += 12.0) {
                const double mSub = dftMag(mixL, start, fs, fProbe);
                const double dbRel = 20.0 * std::log10((mSub + 1e-15) / (h1Mag + 1e-15));
                maxSubDb = std::max(maxSubDb, dbRel);
            }
            INFO("fs=" << fs << " midi=" << midi << " maxSubDb=" << maxSubDb);
            REQUIRE(maxSubDb < -90.0);
        }
    }
}

TEST_CASE("11. Strict Monophonic Low-Note-Priority Pedal (Zero Overlap + Fold Refcount)", "[dsp][pedal]") {
    constexpr double fs = 48000.0;
    ET5MEngine eng;
    eng.prepare(fs);
    EngineState st;
    st.activeStops.fill(false);
    st.activeStops[STOP_PEDAL_BOURDON] = true;
    st.pedalSustain = true;
    st.polyPedal = true; // Must remain strictly monophonic
    st.cabinetMix = 0.0;
    eng.setState(st);

    // Hold B2 (47), switch to lower D2 (38) -> B2 must immediately cut off with zero two-note overlap
    eng.noteOn(ManualId::Pedal, 47);
    std::vector<double> mixL, mixR, dirL, dirR, cabL, cabR;
    renderEngine(eng, 7200, mixL, mixR, dirL, dirR, cabL, cabR);
    eng.noteOff(ManualId::Pedal, 47);
    renderEngine(eng, 1440, mixL, mixR, dirL, dirR, cabL, cabR);

    eng.noteOn(ManualId::Pedal, 38);
    REQUIRE(eng.activePedalMidiNote() == 38);
    renderEngine(eng, 24000, mixL, mixR, dirL, dirR, cabL, cabR);

    const double fD2 = eng.oscillatorBank().noteFrequency(38, true);
    const double fB2 = eng.oscillatorBank().noteFrequency(47, true);
    auto dft = [&](double freq) {
        double re = 0.0, im = 0.0;
        const std::size_t start = 4800;
        const std::size_t m = dirL.size() - start;
        for (std::size_t i = 0; i < m; ++i) {
            const double w = 0.5 * (1.0 - std::cos(2.0 * kPi * static_cast<double>(i) / static_cast<double>(m)));
            const double a = 2.0 * kPi * freq * static_cast<double>(i) / fs;
            re += dirL[start + i] * w * std::cos(a);
            im -= dirL[start + i] * w * std::sin(a);
        }
        return std::sqrt(re * re + im * im);
    };
    const double overlapDb = 20.0 * std::log10((dft(fB2) + 1e-15) / (dft(fD2) + 1e-15));
    REQUIRE(overlapDb < -60.0);

    // Refcount safety with octave-folded keys mapped to same note (B3=59 and B2=47 -> 47)
    eng.reset();
    MidiRoutingConfig cfg;
    cfg.pedalChannel = 3;
    cfg.pedalOctaveFold = true;
    eng.handleMidiNoteOn(3, 59, cfg);
    eng.handleMidiNoteOn(3, 47, cfg);
    REQUIRE(eng.activePedalMidiNote() == 47);
    eng.handleMidiNoteOff(3, 59);
    REQUIRE(eng.activePedalMidiNote() == 47);
    eng.handleMidiNoteOff(3, 47);
    REQUIRE(eng.activePedalMidiNote() == -1);
}
