#include "PluginProcessor.hpp"
#include "PluginEditor.hpp"

#include <algorithm>
#include <cmath>

namespace et5m::plugin {

ET5MAudioProcessor::ET5MAudioProcessor()
    : AudioProcessor(BusesProperties()
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts_(*this, nullptr, "ET5MParameters", createParameterLayout()) {
    rawParams_.bind(apvts_);
    engine_.prepare(48000.0);
}

void ET5MAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock) {
    const double fs = (sampleRate > 1000.0) ? sampleRate : 48000.0;
    const std::size_t scratchSize = static_cast<std::size_t>(std::max(512, samplesPerBlock));
    mixL_.assign(scratchSize, 0.0);
    mixR_.assign(scratchSize, 0.0);
    dirL_.assign(scratchSize, 0.0);
    dirR_.assign(scratchSize, 0.0);
    cabL_.assign(scratchSize, 0.0);
    cabR_.assign(scratchSize, 0.0);

    cc11Expression_ = 1.0;
    cc7Volume_ = 1.0;
    cc64UpperSustain_ = false;
    cc64PedalSustain_ = false;

    engine_.prepare(fs);
    syncEngineState(rawParams_.readEngineState(), rawParams_.readRefA4Hz());
    lfoRateHz_.store(static_cast<float>(engine_.lfoRateHz()), std::memory_order_relaxed);
}

void ET5MAudioProcessor::releaseResources() {
    engine_.allNotesOff();
}

bool ET5MAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    if (!layouts.inputBuses.isEmpty() || layouts.outputBuses.size() != 1) {
        return false;
    }
    const auto& mainOut = layouts.getMainOutputChannelSet();
    return (mainOut == juce::AudioChannelSet::mono()
            || mainOut == juce::AudioChannelSet::stereo());
}

void ET5MAudioProcessor::syncEngineState(const EngineState& baseState, double refA4Hz) noexcept {
    EngineState eff = baseState;
    eff.expression = std::clamp(baseState.expression * cc11Expression_ * cc7Volume_, 0.0, 1.0);
    eff.kneeSustain = (baseState.kneeSustain || cc64UpperSustain_);
    eff.pedalSustain = (baseState.pedalSustain || cc64PedalSustain_);
    engine_.setRefA4Hz(refA4Hz);
    engine_.setState(eff);
}

void ET5MAudioProcessor::handleMidiController(int channel, int ccNumber, int value, const MidiRoutingConfig& midiCfg) noexcept {
    if (ccNumber == 11) {
        cc11Expression_ = static_cast<double>(std::clamp(value, 0, 127)) / 127.0;
    } else if (ccNumber == 7) {
        cc7Volume_ = static_cast<double>(std::clamp(value, 0, 127)) / 127.0;
    } else if (ccNumber == 64) {
        const bool down = (value >= 64);
        if (midiCfg.singleKeyboardSplit) {
            cc64UpperSustain_ = down;
        } else {
            const bool matchUpper = (midiCfg.upperChannel == 0 || midiCfg.upperChannel == channel);
            const bool matchPedal = (midiCfg.pedalChannel == 0 || midiCfg.pedalChannel == channel);
            if (matchUpper) cc64UpperSustain_ = down;
            if (matchPedal) cc64PedalSustain_ = down;
        }
    } else if (ccNumber == 120 || ccNumber == 123) {
        engine_.allNotesOff();
    }
}

void ET5MAudioProcessor::renderChunk(int startSample, int numSamples, juce::AudioBuffer<float>& buffer) noexcept {
    if (numSamples <= 0) return;

    int offset = 0;
    const int scratchCap = static_cast<int>(mixL_.size());
    const int nCh = buffer.getNumChannels();
    while (offset < numSamples) {
        const int n = std::min(scratchCap, numSamples - offset);
        const std::size_t un = static_cast<std::size_t>(n);

        engine_.processBlock(mixL_.data(), mixR_.data(), dirL_.data(), dirR_.data(), cabL_.data(), cabR_.data(), un);

        for (int i = 0; i < n; ++i) {
            const std::size_t si = static_cast<std::size_t>(i);
            sumSqMix_ += 0.5 * (mixL_[si] * mixL_[si] + mixR_[si] * mixR_[si]);
            sumSqCab_ += 0.5 * (cabL_[si] * cabL_[si] + cabR_[si] * cabR_[si]);
            sumSqDir_ += 0.5 * (dirL_[si] * dirL_[si] + dirR_[si] * dirR_[si]);
        }

        const int destStart = startSample + offset;
        if (nCh == 1) {
            float* dst = buffer.getWritePointer(0, destStart);
            for (int i = 0; i < n; ++i) {
                const std::size_t si = static_cast<std::size_t>(i);
                dst[i] = static_cast<float>(0.5 * (mixL_[si] + mixR_[si]));
            }
        } else if (nCh >= 2) {
            float* dstL = buffer.getWritePointer(0, destStart);
            float* dstR = buffer.getWritePointer(1, destStart);
            for (int i = 0; i < n; ++i) {
                const std::size_t si = static_cast<std::size_t>(i);
                dstL[i] = static_cast<float>(mixL_[si]);
                dstR[i] = static_cast<float>(mixR_[si]);
            }
        }

        offset += n;
    }
}

void ET5MAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) {
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();

    sumSqMix_ = 0.0;
    sumSqCab_ = 0.0;
    sumSqDir_ = 0.0;

    const auto baseState = rawParams_.readEngineState();
    const auto midiCfg = rawParams_.readMidiConfig();
    const double refA4 = rawParams_.readRefA4Hz();
    syncEngineState(baseState, refA4);

    const int numSamples = buffer.getNumSamples();
    int sampleCursor = 0;

    for (const auto metadata : midiMessages) {
        const int evSample = std::clamp(metadata.samplePosition, 0, numSamples);
        if (evSample > sampleCursor) {
            renderChunk(sampleCursor, evSample - sampleCursor, buffer);
            sampleCursor = evSample;
        }

        const auto msg = metadata.getMessage();
        if (msg.isNoteOn()) {
            engine_.handleMidiNoteOn(msg.getChannel(), msg.getNoteNumber(), midiCfg);
        } else if (msg.isNoteOff()) {
            engine_.handleMidiNoteOff(msg.getChannel(), msg.getNoteNumber());
        } else if (msg.isController()) {
            handleMidiController(msg.getChannel(), msg.getControllerNumber(), msg.getControllerValue(), midiCfg);
            syncEngineState(baseState, refA4);
        } else if (msg.isAllNotesOff() || msg.isAllSoundOff()) {
            engine_.allNotesOff();
        }
    }

    if (sampleCursor < numSamples) {
        renderChunk(sampleCursor, numSamples - sampleCursor, buffer);
    }

    const double invN = (numSamples > 0) ? (1.0 / static_cast<double>(numSamples)) : 0.0;
    mainRms_.store(static_cast<float>(std::sqrt(sumSqMix_ * invN)), std::memory_order_relaxed);
    cabRms_.store(static_cast<float>(std::sqrt(sumSqCab_ * invN)), std::memory_order_relaxed);
    dirRms_.store(static_cast<float>(std::sqrt(sumSqDir_ * invN)), std::memory_order_relaxed);
    lfoRateHz_.store(static_cast<float>(engine_.lfoRateHz()), std::memory_order_relaxed);
    upperActiveNotes_.store(engine_.activeUpperNoteCount(), std::memory_order_relaxed);
    lowerActiveNotes_.store(engine_.activeLowerNoteCount(), std::memory_order_relaxed);
    pedalActiveMidi_.store(engine_.activePedalMidiNote(), std::memory_order_relaxed);
}

juce::AudioProcessorEditor* ET5MAudioProcessor::createEditor() {
    return new ET5MAudioProcessorEditor(*this);
}

int ET5MAudioProcessor::getNumPrograms() {
    return static_cast<int>(kFactoryPresets.size());
}

int ET5MAudioProcessor::getCurrentProgram() {
    return currentProgram_;
}

void ET5MAudioProcessor::setCurrentProgram(int index) {
    if (index < 0 || index >= getNumPrograms()) return;
    currentProgram_ = index;
    applyFactoryPreset(apvts_, index);
}

const juce::String ET5MAudioProcessor::getProgramName(int index) {
    if (index < 0 || index >= getNumPrograms()) return {};
    return kFactoryPresets[static_cast<std::size_t>(index)].name;
}

void ET5MAudioProcessor::getStateInformation(juce::MemoryBlock& destData) {
    auto state = apvts_.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    if (xml != nullptr) {
        xml->setAttribute("currentProgram", currentProgram_);
        copyXmlToBinary(*xml, destData);
    }
}

void ET5MAudioProcessor::setStateInformation(const void* data, int sizeInBytes) {
    std::unique_ptr<juce::XmlElement> xml(getXmlFromBinary(data, sizeInBytes));
    if (xml != nullptr && xml->hasTagName(apvts_.state.getType())) {
        currentProgram_ = std::clamp(xml->getIntAttribute("currentProgram", 0), 0, getNumPrograms() - 1);
        apvts_.replaceState(juce::ValueTree::fromXml(*xml));
    }
}

} // namespace et5m::plugin

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new et5m::plugin::ET5MAudioProcessor();
}
