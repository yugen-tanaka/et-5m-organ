#pragma once

#include "dsp/ET5MEngine.hpp"
#include "dsp/MidiRouter.hpp"

#include <juce_audio_processors/juce_audio_processors.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <memory>

namespace et5m::plugin {

namespace param_ids {
// Upper Manual Tone Tablets (5)
inline constexpr const char* upperDiapason = "upper_diapason";
inline constexpr const char* upperFlute    = "upper_flute";
inline constexpr const char* upperViolin   = "upper_violin";
inline constexpr const char* upperOboe     = "upper_oboe";
inline constexpr const char* upperTrumpet  = "upper_trumpet";

// Lower Manual Tone Tablets (3)
inline constexpr const char* lowerDiapason = "lower_diapason";
inline constexpr const char* lowerCello    = "lower_cello";
inline constexpr const char* lowerHorn     = "lower_horn";

// Pedal Tone Tablets (2)
inline constexpr const char* pedalBourdon  = "pedal_bourdon";
inline constexpr const char* pedalTrombone = "pedal_trombone";

// Effects & Sustain Tablets + Knee Lever (5)
inline constexpr const char* tremolo      = "tremolo";
inline constexpr const char* vibrato      = "vibrato";
inline constexpr const char* pedalSustain = "pedal_sustain";
inline constexpr const char* upperSustain = "upper_sustain";
inline constexpr const char* kneeSustain  = "knee_sustain";

// Console Knobs (7)
inline constexpr const char* hallTone      = "hall_tone";
inline constexpr const char* pedalVolume   = "pedal_volume";
inline constexpr const char* manualBalance = "manual_balance";
inline constexpr const char* repeatSpeed   = "repeat_speed";
inline constexpr const char* sustainTimeMs = "sustain_time_ms";
inline constexpr const char* expression    = "expression";
inline constexpr const char* masterVolume  = "master_volume";

// Circuit Authenticity & 1965 Tube/Cabinet Calibration (8 + Bypass)
inline constexpr const char* cabinetMix        = "cabinet_mix";
inline constexpr const char* ampCabBypass      = "amp_cab_bypass";
inline constexpr const char* tubeAmount        = "tube_amount";
inline constexpr const char* humAmount         = "hum_amount";
inline constexpr const char* stereoWidth       = "stereo_width";
inline constexpr const char* authentic13Tuning = "authentic_13_tuning";
inline constexpr const char* syncBleed         = "sync_bleed";
inline constexpr const char* a4Hz              = "a4_hz";
inline constexpr const char* polyPedal         = "poly_pedal";

// MIDI Routing & Split Configuration (11)
inline constexpr const char* midiMode        = "midi_mode";
inline constexpr const char* upperMidiCh     = "upper_midi_ch";
inline constexpr const char* lowerMidiCh     = "lower_midi_ch";
inline constexpr const char* pedalMidiCh     = "pedal_midi_ch";
inline constexpr const char* upperOctave     = "upper_octave";
inline constexpr const char* lowerOctave     = "lower_octave";
inline constexpr const char* pedalOctave     = "pedal_octave";
inline constexpr const char* pedalSplitMax   = "pedal_split_max";
inline constexpr const char* lowerSplitMax   = "lower_split_max";
inline constexpr const char* foldPedalOctave = "fold_pedal_octave";
inline constexpr const char* clampKeyRange   = "clamp_key_range";
} // namespace param_ids

inline int choiceIndexToMidiChannel(int idx) noexcept {
    return (idx >= 17) ? -1 : std::clamp(idx, 0, 16);
}

inline juce::StringArray makeManualMidiChannelChoices() {
    juce::StringArray arr;
    arr.add("Omni");
    for (int ch = 1; ch <= 16; ++ch) {
        arr.add("Ch " + juce::String(ch));
    }
    arr.add("Off");
    return arr;
}

inline juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout() {
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;

    auto addBool = [&](const char* id, const char* name, bool def) {
        p.push_back(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{id, 1}, name, def));
    };
    auto addFloat = [&](const char* id, const char* name, float mn, float mx, float step, float def) {
        p.push_back(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID{id, 1}, name, juce::NormalisableRange<float>(mn, mx, step), def));
    };

    // 10 Tone Tablets
    addBool(param_ids::upperDiapason, "Upper Diapason", false);
    addBool(param_ids::upperFlute,    "Upper Flute",    true);
    addBool(param_ids::upperViolin,   "Upper Violin",   false);
    addBool(param_ids::upperOboe,     "Upper Oboe",     false);
    addBool(param_ids::upperTrumpet,  "Upper Trumpet",  false);

    addBool(param_ids::lowerDiapason, "Lower Diapason", true);
    addBool(param_ids::lowerCello,    "Lower Cello",    false);
    addBool(param_ids::lowerHorn,     "Lower Horn",     false);

    addBool(param_ids::pedalBourdon,  "Pedal Bourdon",  true);
    addBool(param_ids::pedalTrombone, "Pedal Trombone", false);

    // 5 Effects & Sustain
    addBool(param_ids::tremolo,      "Tremolo",       false);
    addBool(param_ids::vibrato,      "Vibrato",       false);
    addBool(param_ids::pedalSustain, "Pedal Sustain", false);
    addBool(param_ids::upperSustain, "Upper Sustain", false);
    addBool(param_ids::kneeSustain,  "Knee Lever Sustainer", false);

    // 7 Console Knobs
    addFloat(param_ids::hallTone,      "Hall Tone (Reverb)", 0.0f, 1.0f, 0.001f, 0.00f);
    addFloat(param_ids::pedalVolume,   "Pedal Volume",       0.0f, 1.0f, 0.001f, 0.85f);
    addFloat(param_ids::manualBalance, "Manual Balance", 0.0f, 1.0f, 0.001f, 0.50f);
    addFloat(param_ids::repeatSpeed,   "Repeat Speed",   0.0f, 1.0f, 0.001f, 0.98f);
    addFloat(param_ids::sustainTimeMs, "Sustain Time (ms)", 80.0f, 900.0f, 0.5f, static_cast<float>(kUpperSustainNominalTauMs));
    addFloat(param_ids::expression,    "Expression",     0.0f, 1.0f, 0.001f, 1.00f);
    addFloat(param_ids::masterVolume,  "Master Volume",  0.0f, 1.0f, 0.001f, 0.85f);

    // Circuit & Tube/Cabinet Calibration
    addFloat(param_ids::cabinetMix,        "Tube & Cabinet Mix", 0.0f, 1.0f, 0.001f, 1.00f);
    addBool (param_ids::ampCabBypass,      "Bypass Amp & Cabinet (Line Out)", false);
    addFloat(param_ids::tubeAmount,        "Tube Saturation",    0.0f, 1.5f, 0.005f, 1.00f);
    addFloat(param_ids::humAmount,         "100Hz Tube Hum",     0.0f, 1.0f, 0.005f, 0.00f);
    addFloat(param_ids::stereoWidth,       "XY Cabinet Width",   0.0f, 1.0f, 0.005f, 0.35f);
    addBool (param_ids::authentic13Tuning, "Authentic 1/3 Pure Fifth", true);
    addFloat(param_ids::syncBleed,         "250pF Sync Bleed",   0.0f, 1.5f, 0.01f, 1.00f);
    addFloat(param_ids::a4Hz,              "Master Tuning (Hz)", 430.0f, 450.0f, 0.01f, static_cast<float>(kRefA4Hz));
    addBool (param_ids::polyPedal,         "Polyphonic Pedal",   false);

    // 11 MIDI Routing & Split Parameters
    p.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{param_ids::midiMode, 1}, "MIDI Mode",
        juce::StringArray{"Multi-Channel", "Single-Keyboard Split"}, 0));

    const auto chChoices = makeManualMidiChannelChoices();
    p.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{param_ids::upperMidiCh, 1}, "Upper MIDI Channel", chChoices, 1));
    p.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{param_ids::lowerMidiCh, 1}, "Lower MIDI Channel", chChoices, 2));
    p.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{param_ids::pedalMidiCh, 1}, "Pedal MIDI Channel", chChoices, 3));

    p.push_back(std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID{param_ids::upperOctave, 1}, "Upper Octave Shift", -2, 2, 0));
    p.push_back(std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID{param_ids::lowerOctave, 1}, "Lower Octave Shift", -2, 2, 0));
    p.push_back(std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID{param_ids::pedalOctave, 1}, "Pedal Octave Shift", -2, 2, 0));
    p.push_back(std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID{param_ids::pedalSplitMax, 1}, "Pedal Split Top MIDI", 36, 59, 47));
    p.push_back(std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID{param_ids::lowerSplitMax, 1}, "Upper Split Min MIDI", 48, 72, 60));
    addBool(param_ids::foldPedalOctave, "Fold Pedal Octave", true);
    addBool(param_ids::clampKeyRange,   "Clamp Hardware Key Range", true);

    return {p.begin(), p.end()};
}

struct CachedParameterPointers {
    std::array<std::atomic<float>*, NUM_STOPS> stops{};
    std::atomic<float>* tremolo{nullptr};
    std::atomic<float>* vibrato{nullptr};
    std::atomic<float>* pedalSustain{nullptr};
    std::atomic<float>* upperSustain{nullptr};
    std::atomic<float>* kneeSustain{nullptr};

    std::atomic<float>* hallTone{nullptr};
    std::atomic<float>* pedalVolume{nullptr};
    std::atomic<float>* manualBalance{nullptr};
    std::atomic<float>* repeatSpeed{nullptr};
    std::atomic<float>* sustainTimeMs{nullptr};
    std::atomic<float>* expression{nullptr};
    std::atomic<float>* masterVolume{nullptr};

    std::atomic<float>* cabinetMix{nullptr};
    std::atomic<float>* ampCabBypass{nullptr};
    std::atomic<float>* tubeAmount{nullptr};
    std::atomic<float>* humAmount{nullptr};
    std::atomic<float>* stereoWidth{nullptr};
    std::atomic<float>* authentic13Tuning{nullptr};
    std::atomic<float>* syncBleed{nullptr};
    std::atomic<float>* a4Hz{nullptr};
    std::atomic<float>* polyPedal{nullptr};

    std::atomic<float>* midiMode{nullptr};
    std::atomic<float>* upperMidiCh{nullptr};
    std::atomic<float>* lowerMidiCh{nullptr};
    std::atomic<float>* pedalMidiCh{nullptr};
    std::atomic<float>* upperOctave{nullptr};
    std::atomic<float>* lowerOctave{nullptr};
    std::atomic<float>* pedalOctave{nullptr};
    std::atomic<float>* pedalSplitMax{nullptr};
    std::atomic<float>* lowerSplitMax{nullptr};
    std::atomic<float>* foldPedalOctave{nullptr};
    std::atomic<float>* clampKeyRange{nullptr};

    void bind(juce::AudioProcessorValueTreeState& apvts) noexcept {
        static constexpr std::array<const char*, NUM_STOPS> kStopParamIds = {
            param_ids::upperDiapason,
            param_ids::upperFlute,
            param_ids::upperViolin,
            param_ids::upperOboe,
            param_ids::upperTrumpet,
            param_ids::lowerDiapason,
            param_ids::lowerCello,
            param_ids::lowerHorn,
            param_ids::pedalBourdon,
            param_ids::pedalTrombone,
        };
        for (std::size_t i = 0; i < NUM_STOPS; ++i) {
            stops[i] = apvts.getRawParameterValue(kStopParamIds[i]);
        }
        tremolo           = apvts.getRawParameterValue(param_ids::tremolo);
        vibrato           = apvts.getRawParameterValue(param_ids::vibrato);
        pedalSustain      = apvts.getRawParameterValue(param_ids::pedalSustain);
        upperSustain      = apvts.getRawParameterValue(param_ids::upperSustain);
        kneeSustain       = apvts.getRawParameterValue(param_ids::kneeSustain);

        hallTone          = apvts.getRawParameterValue(param_ids::hallTone);
        pedalVolume       = apvts.getRawParameterValue(param_ids::pedalVolume);
        manualBalance     = apvts.getRawParameterValue(param_ids::manualBalance);
        repeatSpeed       = apvts.getRawParameterValue(param_ids::repeatSpeed);
        sustainTimeMs     = apvts.getRawParameterValue(param_ids::sustainTimeMs);
        expression        = apvts.getRawParameterValue(param_ids::expression);
        masterVolume      = apvts.getRawParameterValue(param_ids::masterVolume);

        cabinetMix        = apvts.getRawParameterValue(param_ids::cabinetMix);
        ampCabBypass      = apvts.getRawParameterValue(param_ids::ampCabBypass);
        tubeAmount        = apvts.getRawParameterValue(param_ids::tubeAmount);
        humAmount         = apvts.getRawParameterValue(param_ids::humAmount);
        stereoWidth       = apvts.getRawParameterValue(param_ids::stereoWidth);
        authentic13Tuning = apvts.getRawParameterValue(param_ids::authentic13Tuning);
        syncBleed         = apvts.getRawParameterValue(param_ids::syncBleed);
        a4Hz              = apvts.getRawParameterValue(param_ids::a4Hz);
        polyPedal         = apvts.getRawParameterValue(param_ids::polyPedal);

        midiMode          = apvts.getRawParameterValue(param_ids::midiMode);
        upperMidiCh       = apvts.getRawParameterValue(param_ids::upperMidiCh);
        lowerMidiCh       = apvts.getRawParameterValue(param_ids::lowerMidiCh);
        pedalMidiCh       = apvts.getRawParameterValue(param_ids::pedalMidiCh);
        upperOctave       = apvts.getRawParameterValue(param_ids::upperOctave);
        lowerOctave       = apvts.getRawParameterValue(param_ids::lowerOctave);
        pedalOctave       = apvts.getRawParameterValue(param_ids::pedalOctave);
        pedalSplitMax     = apvts.getRawParameterValue(param_ids::pedalSplitMax);
        lowerSplitMax     = apvts.getRawParameterValue(param_ids::lowerSplitMax);
        foldPedalOctave   = apvts.getRawParameterValue(param_ids::foldPedalOctave);
        clampKeyRange     = apvts.getRawParameterValue(param_ids::clampKeyRange);
    }

    [[nodiscard]] EngineState readEngineState() const noexcept {
        EngineState st;
        for (std::size_t i = 0; i < NUM_STOPS; ++i) {
            st.activeStops[i] = (stops[i]->load(std::memory_order_relaxed) >= 0.5f);
        }
        st.tremoloOn         = (tremolo->load(std::memory_order_relaxed) >= 0.5f);
        st.vibratoOn         = (vibrato->load(std::memory_order_relaxed) >= 0.5f);
        st.pedalSustain      = (pedalSustain->load(std::memory_order_relaxed) >= 0.5f);
        st.upperSustain      = (upperSustain->load(std::memory_order_relaxed) >= 0.5f);
        st.kneeSustain       = (kneeSustain->load(std::memory_order_relaxed) >= 0.5f);
        st.hallTone          = (hallTone != nullptr) ? static_cast<double>(hallTone->load(std::memory_order_relaxed)) : 0.0;
        st.pedalVolume       = static_cast<double>(pedalVolume->load(std::memory_order_relaxed));
        st.manualBalance     = static_cast<double>(manualBalance->load(std::memory_order_relaxed));
        st.repeatSpeed       = static_cast<double>(repeatSpeed->load(std::memory_order_relaxed));
        st.sustainTimeMs     = static_cast<double>(sustainTimeMs->load(std::memory_order_relaxed));
        st.expression        = static_cast<double>(expression->load(std::memory_order_relaxed));
        st.masterVolume      = static_cast<double>(masterVolume->load(std::memory_order_relaxed));
        const bool bypassCab = (ampCabBypass != nullptr) && (ampCabBypass->load(std::memory_order_relaxed) >= 0.5f);
        st.cabinetMix        = bypassCab ? 0.0 : static_cast<double>(cabinetMix->load(std::memory_order_relaxed));
        st.tubeAmount        = static_cast<double>(tubeAmount->load(std::memory_order_relaxed));
        st.humAmount         = 0.0;
        st.stereoWidth       = 0.35;
        st.authentic13Tuning = true;
        st.syncBleed         = 1.0;
        st.polyPedal         = true;
        return st;
    }

    [[nodiscard]] double readRefA4Hz() const noexcept {
        return static_cast<double>(a4Hz->load(std::memory_order_relaxed));
    }

    [[nodiscard]] MidiRoutingConfig readMidiConfig() const noexcept {
        MidiRoutingConfig cfg;
        auto rdInt = [](std::atomic<float>* ptr) noexcept -> int {
            return static_cast<int>(std::lround(ptr->load(std::memory_order_relaxed)));
        };
        cfg.singleKeyboardSplit = (rdInt(midiMode) == 1);
        cfg.upperChannel = choiceIndexToMidiChannel(rdInt(upperMidiCh));
        cfg.lowerChannel = choiceIndexToMidiChannel(rdInt(lowerMidiCh));
        cfg.pedalChannel = choiceIndexToMidiChannel(rdInt(pedalMidiCh));
        cfg.upperOctaveShift = rdInt(upperOctave);
        cfg.lowerOctaveShift = rdInt(lowerOctave);
        cfg.pedalOctaveShift = rdInt(pedalOctave);
        cfg.pedalSplitTopMidi = rdInt(pedalSplitMax);
        cfg.splitPointMidi = std::max(cfg.pedalSplitTopMidi + 1, rdInt(lowerSplitMax));
        cfg.pedalOctaveFold = (foldPedalOctave->load(std::memory_order_relaxed) >= 0.5f);
        cfg.clampToHardwareRange = (clampKeyRange->load(std::memory_order_relaxed) >= 0.5f);
        return cfg;
    }
};

} // namespace et5m::plugin
