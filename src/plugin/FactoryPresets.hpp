#pragma once

#include "PluginParams.hpp"
#include <array>

namespace et5m::plugin {

struct FactoryPreset {
    const char* name;
    std::array<bool, NUM_STOPS> stops; // U: Diap, Flt, Vln, Obo, Trp | L: Diap, Cel, Hrn | P: Brd, Trb
    bool tremolo;
    bool vibrato;
    bool upperSustain;
    bool pedalSustain;
    float manualBalance;
    float pedalVolume;
    float hallTone;
    float repeatSpeed;
    float sustainTimeMs;
    float cabinetMix;
    float tubeAmount;
    bool ampCabBypass{false};
};

inline constexpr std::array<FactoryPreset, 5> kFactoryPresets = {{
    FactoryPreset{"01. Full 1965 Console",        {{1,1,1,0,1, 1,1,1, 1,1}}, false, true,  false, false, 0.50f, 0.85f, 0.22f, 0.98f, 257.0f, 1.0f, 1.0f, false},
    FactoryPreset{"02. Mellow Flute & Diapason",  {{1,1,0,0,0, 1,0,0, 1,0}}, false, false, false, false, 0.52f, 0.85f, 0.00f, 257.0f, 1.0f, 1.0f, false},
    FactoryPreset{"03. Strings & Oboe Vibrato",   {{0,0,1,1,0, 0,1,0, 1,0}}, false, true,  false, false, 0.55f, 0.82f, 0.35f, 257.0f, 1.0f, 1.0f, false},
    FactoryPreset{"04. Brassy Trumpet & Horn",    {{0,0,0,0,1, 0,0,1, 0,1}}, false, false, false, false, 0.50f, 0.88f, 0.15f, 257.0f, 1.0f, 1.1f, false},
    FactoryPreset{"05. Direct Line Out (Bypass)", {{1,1,0,0,0, 1,0,0, 1,0}}, false, false, false, false, 0.50f, 0.85f, 0.00f, 257.0f, 1.0f, 1.0f, true}
}};

inline void applyFactoryPreset(juce::AudioProcessorValueTreeState& apvts, int presetIndex) {
    if (presetIndex < 0 || presetIndex >= static_cast<int>(kFactoryPresets.size())) {
        return;
    }
    const auto& p = kFactoryPresets[static_cast<std::size_t>(presetIndex)];
    auto setVal = [&](const char* id, float val) {
        if (auto* param = apvts.getParameter(id)) {
            param->setValueNotifyingHost(param->convertTo0to1(val));
        }
    };

    static constexpr std::array<const char*, NUM_STOPS> kIds = {
        param_ids::upperDiapason, param_ids::upperFlute, param_ids::upperViolin,
        param_ids::upperOboe, param_ids::upperTrumpet,
        param_ids::lowerDiapason, param_ids::lowerCello, param_ids::lowerHorn,
        param_ids::pedalBourdon, param_ids::pedalTrombone
    };
    for (std::size_t i = 0; i < NUM_STOPS; ++i) {
        setVal(kIds[i], p.stops[i] ? 1.0f : 0.0f);
    }
    setVal(param_ids::tremolo,       p.tremolo ? 1.0f : 0.0f);
    setVal(param_ids::vibrato,       p.vibrato ? 1.0f : 0.0f);
    setVal(param_ids::upperSustain,  p.upperSustain ? 1.0f : 0.0f);
    setVal(param_ids::pedalSustain,  p.pedalSustain ? 1.0f : 0.0f);
    setVal(param_ids::manualBalance, p.manualBalance);
    setVal(param_ids::pedalVolume,   p.pedalVolume);
    setVal(param_ids::hallTone,      p.hallTone);
    setVal(param_ids::repeatSpeed,   p.repeatSpeed);
    setVal(param_ids::sustainTimeMs, p.sustainTimeMs);
    setVal(param_ids::cabinetMix,    p.cabinetMix);
    setVal(param_ids::tubeAmount,    p.tubeAmount);
    setVal(param_ids::ampCabBypass,  p.ampCabBypass ? 1.0f : 0.0f);
}

inline void clearAllToneAndEffectTablets(juce::AudioProcessorValueTreeState& apvts) {
    auto off = [&](const char* id) {
        if (auto* param = apvts.getParameter(id)) {
            param->setValueNotifyingHost(0.0f);
        }
    };
    for (const char* id : {
             param_ids::upperDiapason, param_ids::upperFlute, param_ids::upperViolin,
             param_ids::upperOboe, param_ids::upperTrumpet,
             param_ids::lowerDiapason, param_ids::lowerCello, param_ids::lowerHorn,
             param_ids::pedalBourdon, param_ids::pedalTrombone,
             param_ids::tremolo, param_ids::vibrato,
             param_ids::pedalSustain, param_ids::upperSustain, param_ids::kneeSustain
         }) {
        off(id);
    }
}

} // namespace et5m::plugin
