#include "plugin/PluginEditor.hpp"
#include "plugin/PluginProcessor.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <juce_graphics/juce_graphics.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <algorithm>
#include <cmath>
#include <memory>

namespace {

void setParamNormalized(juce::AudioProcessorValueTreeState& apvts, const char* id, float plainVal) {
    if (auto* p = apvts.getParameter(id)) {
        p->setValueNotifyingHost(p->convertTo0to1(plainVal));
    }
}

void setChoiceIndex(juce::AudioProcessorValueTreeState& apvts, const char* id, int idx) {
    if (auto* p = apvts.getParameter(id)) {
        p->setValueNotifyingHost(p->convertTo0to1(static_cast<float>(idx)));
    }
}

float channelRms(const juce::AudioBuffer<float>& buf, int ch) {
    if (ch >= buf.getNumChannels() || buf.getNumSamples() <= 0) {
        return 0.0f;
    }
    return buf.getRMSLevel(ch, 0, buf.getNumSamples());
}

float bufferDiffRms(const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b) {
    const int n = std::min(a.getNumSamples(), b.getNumSamples());
    double sumSq = 0.0;
    const float* a0 = a.getReadPointer(0);
    const float* a1 = a.getReadPointer(1);
    const float* b0 = b.getReadPointer(0);
    const float* b1 = b.getReadPointer(1);
    for (int i = 0; i < n; ++i) {
        const double d0 = static_cast<double>(a0[i]) - static_cast<double>(b0[i]);
        const double d1 = static_cast<double>(a1[i]) - static_cast<double>(b1[i]);
        sumSq += 0.5 * (d0 * d0 + d1 * d1);
    }
    return static_cast<float>(std::sqrt(sumSq / static_cast<double>(std::max(1, n))));
}

} // namespace

TEST_CASE("ET5M Plugin: Mono/Stereo single-bus layout, cabinet_mix blend, and Line-Out bypass", "[plugin][buses]") {
    et5m::plugin::ET5MAudioProcessor proc;
    REQUIRE(proc.getBusCount(true) == 0);
    REQUIRE(proc.getBusCount(false) == 1);
    REQUIRE(proc.getTotalNumOutputChannels() == 2);

    {
        juce::AudioProcessor::BusesLayout stereoLayout;
        stereoLayout.outputBuses.add(juce::AudioChannelSet::stereo());
        REQUIRE(proc.isBusesLayoutSupported(stereoLayout));

        juce::AudioProcessor::BusesLayout monoLayout;
        monoLayout.outputBuses.add(juce::AudioChannelSet::mono());
        REQUIRE(proc.isBusesLayoutSupported(monoLayout));

        juce::AudioProcessor::BusesLayout lcrLayout;
        lcrLayout.outputBuses.add(juce::AudioChannelSet::createLCR());
        REQUIRE_FALSE(proc.isBusesLayoutSupported(lcrLayout));

        juce::AudioProcessor::BusesLayout multiBusLayout;
        multiBusLayout.outputBuses.add(juce::AudioChannelSet::stereo());
        multiBusLayout.outputBuses.add(juce::AudioChannelSet::stereo());
        REQUIRE_FALSE(proc.isBusesLayoutSupported(multiBusLayout));
    }

    auto renderScenario = [](float mixVal, bool bypassVal) -> juce::AudioBuffer<float> {
        et5m::plugin::ET5MAudioProcessor p;
        p.prepareToPlay(48000.0, 1024);
        setParamNormalized(p.apvts(), et5m::plugin::param_ids::cabinetMix, mixVal);
        setParamNormalized(p.apvts(), et5m::plugin::param_ids::ampCabBypass, bypassVal ? 1.0f : 0.0f);
        juce::MidiBuffer midiOn;
        midiOn.addEvent(juce::MidiMessage::noteOn(1, 69, static_cast<juce::uint8>(100)), 0);
        juce::AudioBuffer<float> buf(2, 4096);
        p.processBlock(buf, midiOn);
        return buf;
    };

    const auto bufDir = renderScenario(0.0f, false);
    const auto bufCab = renderScenario(1.0f, false);
    const auto bufHalf = renderScenario(0.5f, false);
    const auto bufBypass = renderScenario(1.0f, true);

    REQUIRE(channelRms(bufDir, 0) > 1e-3f);
    REQUIRE(channelRms(bufCab, 0) > 1e-3f);
    REQUIRE(bufferDiffRms(bufDir, bufCab) > 1e-3f);

    double maxErr = 0.0;
    for (int i = 0; i < bufHalf.getNumSamples(); ++i) {
        const double expectedL = 0.5 * (bufDir.getSample(0, i) + bufCab.getSample(0, i));
        const double expectedR = 0.5 * (bufDir.getSample(1, i) + bufCab.getSample(1, i));
        maxErr = std::max(maxErr, std::abs(static_cast<double>(bufHalf.getSample(0, i)) - expectedL));
        maxErr = std::max(maxErr, std::abs(static_cast<double>(bufHalf.getSample(1, i)) - expectedR));
    }
    REQUIRE(maxErr < 1e-6);

    REQUIRE(bufferDiffRms(bufBypass, bufDir) < 1e-6f);
    REQUIRE(bufferDiffRms(bufBypass, bufCab) > 1e-3f);
}

TEST_CASE("ET5M Plugin: MIDI channel routing, Single-Keyboard Split, and stuck-note protection", "[plugin][midi]") {
    et5m::plugin::ET5MAudioProcessor proc;
    proc.prepareToPlay(48000.0, 512);
    auto& apvts = proc.apvts();

    // Custom Multi-Channel assignment: Upper=Ch 5, Lower=Ch 6, Pedal=Ch 7
    setChoiceIndex(apvts, et5m::plugin::param_ids::midiMode, 0);
    setChoiceIndex(apvts, et5m::plugin::param_ids::upperMidiCh, 5);
    setChoiceIndex(apvts, et5m::plugin::param_ids::lowerMidiCh, 6);
    setChoiceIndex(apvts, et5m::plugin::param_ids::pedalMidiCh, 7);

    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(5, 72, static_cast<juce::uint8>(100)), 0); // Upper C5
    midi.addEvent(juce::MidiMessage::noteOn(6, 52, static_cast<juce::uint8>(100)), 0); // Lower E3
    midi.addEvent(juce::MidiMessage::noteOn(7, 40, static_cast<juce::uint8>(100)), 0); // Pedal E2
    // Unassigned Ch 1 note should be ignored
    midi.addEvent(juce::MidiMessage::noteOn(1, 69, static_cast<juce::uint8>(100)), 0);

    juce::AudioBuffer<float> buf(2, 512);
    proc.processBlock(buf, midi);
    REQUIRE(proc.activeUpperNotes() == 1);
    REQUIRE(proc.activeLowerNotes() == 1);
    REQUIRE(proc.activePedalMidi() == 40);

    // Live channel change before Note-Off must NOT cause stuck notes!
    setChoiceIndex(apvts, et5m::plugin::param_ids::upperMidiCh, 10);
    setChoiceIndex(apvts, et5m::plugin::param_ids::lowerMidiCh, 11);
    setChoiceIndex(apvts, et5m::plugin::param_ids::pedalMidiCh, 12);

    juce::MidiBuffer midiOff;
    midiOff.addEvent(juce::MidiMessage::noteOff(5, 72), 0);
    midiOff.addEvent(juce::MidiMessage::noteOff(6, 52), 0);
    midiOff.addEvent(juce::MidiMessage::noteOff(7, 40), 0);
    proc.processBlock(buf, midiOff);
    juce::MidiBuffer empty;
    for (int b = 0; b < 6; ++b) {
        proc.processBlock(buf, empty);
    }
    REQUIRE(proc.activeUpperNotes() == 0);
    REQUIRE(proc.activeLowerNotes() == 0);
    REQUIRE(proc.activePedalMidi() == -1);

    // Switch to Single-Keyboard Split mode
    setChoiceIndex(apvts, et5m::plugin::param_ids::midiMode, 1);
    setParamNormalized(apvts, et5m::plugin::param_ids::pedalSplitMax, 47.0f);
    setParamNormalized(apvts, et5m::plugin::param_ids::lowerSplitMax, 60.0f);

    juce::MidiBuffer splitOn;
    splitOn.addEvent(juce::MidiMessage::noteOn(1, 41, static_cast<juce::uint8>(100)), 0); // <=47 -> Pedal F2
    splitOn.addEvent(juce::MidiMessage::noteOn(1, 55, static_cast<juce::uint8>(100)), 0); // 48..59 -> Lower G3
    splitOn.addEvent(juce::MidiMessage::noteOn(1, 67, static_cast<juce::uint8>(100)), 0); // >=60 -> Upper G4
    proc.processBlock(buf, splitOn);
    REQUIRE(proc.activePedalMidi() == 41);
    REQUIRE(proc.activeLowerNotes() == 1);
    REQUIRE(proc.activeUpperNotes() == 1);

    juce::MidiBuffer splitOff;
    splitOff.addEvent(juce::MidiMessage::noteOff(1, 41), 0);
    splitOff.addEvent(juce::MidiMessage::noteOff(1, 55), 0);
    splitOff.addEvent(juce::MidiMessage::noteOff(1, 67), 0);
    proc.processBlock(buf, splitOff);
    for (int b = 0; b < 6; ++b) {
        proc.processBlock(buf, empty);
    }
    REQUIRE(proc.activePedalMidi() == -1);
    REQUIRE(proc.activeLowerNotes() == 0);
    REQUIRE(proc.activeUpperNotes() == 0);
}

TEST_CASE("ET5M Plugin: CC#11 Expression, CC#7 Volume, and CC#64 Knee Sustain", "[plugin][cc]") {
    auto measureTailRms = [](bool sendCc64Down) -> float {
        et5m::plugin::ET5MAudioProcessor proc;
        proc.prepareToPlay(48000.0, 512);
        juce::AudioBuffer<float> buf(2, 512);

        juce::MidiBuffer on;
        if (sendCc64Down) {
            on.addEvent(juce::MidiMessage::controllerEvent(1, 64, 127), 0);
        }
        on.addEvent(juce::MidiMessage::noteOn(1, 69, static_cast<juce::uint8>(100)), 1);
        juce::MidiBuffer empty;
        for (int b = 0; b < 8; ++b) {
            proc.processBlock(buf, (b == 0) ? on : empty);
        }

        juce::MidiBuffer off;
        off.addEvent(juce::MidiMessage::noteOff(1, 69), 0);
        proc.processBlock(buf, off);

        // Render 150 ms after Note-Off
        for (int b = 0; b < 14; ++b) {
            proc.processBlock(buf, empty);
        }
        return channelRms(buf, 0);
    };

    const float rmsDryTail = measureTailRms(false);
    const float rmsSustainTail = measureTailRms(true);
    REQUIRE(rmsSustainTail > rmsDryTail * 4.0f);

    // Verify CC#11 Expression & CC#7 Volume attenuate signal cleanly
    et5m::plugin::ET5MAudioProcessor proc;
    proc.prepareToPlay(48000.0, 1024);
    juce::AudioBuffer<float> bufFull(2, 2048);
    juce::MidiBuffer mFull;
    mFull.addEvent(juce::MidiMessage::noteOn(1, 69, static_cast<juce::uint8>(100)), 0);
    proc.processBlock(bufFull, mFull);
    const float fullRms = channelRms(bufFull, 0);

    juce::AudioBuffer<float> bufQuiet(2, 2048);
    juce::MidiBuffer mCc;
    juce::MidiBuffer mEmpty;
    mCc.addEvent(juce::MidiMessage::controllerEvent(1, 11, 32), 0);
    mCc.addEvent(juce::MidiMessage::controllerEvent(1, 7, 64), 0);
    proc.processBlock(bufQuiet, mCc);
    proc.processBlock(bufQuiet, mEmpty);
    const float quietRms = channelRms(bufQuiet, 0);
    REQUIRE(quietRms < fullRms * 0.25f);
    REQUIRE(quietRms > 1e-5f);
}

TEST_CASE("ET5M Plugin: 5 Factory Presets and XML state roundtrip", "[plugin][presets][state]") {
    et5m::plugin::ET5MAudioProcessor proc;
    REQUIRE(proc.getNumPrograms() == 5);

    for (int i = 0; i < proc.getNumPrograms(); ++i) {
        proc.setCurrentProgram(i);
        REQUIRE(proc.getCurrentProgram() == i);
        REQUIRE(proc.getProgramName(i).isNotEmpty());
    }

    // Configure preset + custom MIDI config + Line Out bypass
    proc.setCurrentProgram(3); // 04. Brassy Trumpet & Horn
    setChoiceIndex(proc.apvts(), et5m::plugin::param_ids::midiMode, 1);
    setChoiceIndex(proc.apvts(), et5m::plugin::param_ids::upperMidiCh, 9);
    setParamNormalized(proc.apvts(), et5m::plugin::param_ids::pedalSplitMax, 45.0f);
    setParamNormalized(proc.apvts(), et5m::plugin::param_ids::ampCabBypass, 1.0f);

    juce::MemoryBlock stateBlock;
    proc.getStateInformation(stateBlock);
    REQUIRE(stateBlock.getSize() > 64);

    // Load into fresh processor
    et5m::plugin::ET5MAudioProcessor restored;
    restored.setStateInformation(stateBlock.getData(), static_cast<int>(stateBlock.getSize()));
    REQUIRE(restored.getCurrentProgram() == 3);

    auto getVal = [&](const char* id) -> float {
        return restored.apvts().getRawParameterValue(id)->load();
    };
    REQUIRE(std::lround(getVal(et5m::plugin::param_ids::midiMode)) == 1);
    REQUIRE(std::lround(getVal(et5m::plugin::param_ids::upperMidiCh)) == 9);
    REQUIRE(std::lround(getVal(et5m::plugin::param_ids::pedalSplitMax)) == 45);
    REQUIRE(getVal(et5m::plugin::param_ids::ampCabBypass) > 0.5f);
    REQUIRE(getVal(et5m::plugin::param_ids::upperTrumpet) > 0.5f);
}

TEST_CASE("ET5M Plugin: Offscreen 1965 GUI instantiation, resize, and screenshot export", "[plugin][gui]") {
    const juce::ScopedJuceInitialiser_GUI guiInit;

    REQUIRE(et5m::plugin::TelemetryDisplay::formatVoiceText(0, 0, -1) == "UP:0  LO:0  PED:--");
    REQUIRE(et5m::plugin::TelemetryDisplay::formatVoiceText(3, 2, 36) == "UP:3  LO:2  PED:C2");
    REQUIRE(et5m::plugin::TelemetryDisplay::formatVoiceText(1, 0, 48) == "UP:1  LO:0  PED:C3");

    et5m::plugin::ET5MAudioProcessor proc;
    proc.prepareToPlay(48000.0, 512);
    proc.setCurrentProgram(0); // 01. Full 1965 Console (mix of ON and OFF tablets)

    // Trigger live notes so telemetry shows active voices in screenshot
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 64, static_cast<juce::uint8>(100)), 0);
    midi.addEvent(juce::MidiMessage::noteOn(1, 67, static_cast<juce::uint8>(100)), 0);
    midi.addEvent(juce::MidiMessage::noteOn(1, 72, static_cast<juce::uint8>(100)), 0);
    midi.addEvent(juce::MidiMessage::noteOn(2, 52, static_cast<juce::uint8>(100)), 0);
    midi.addEvent(juce::MidiMessage::noteOn(3, 36, static_cast<juce::uint8>(100)), 0);
    juce::AudioBuffer<float> buf(2, 1024);
    proc.processBlock(buf, midi);

    std::unique_ptr<juce::AudioProcessorEditor> editor(proc.createEditor());
    REQUIRE(editor != nullptr);
    REQUIRE(editor->getWidth() == 920);
    REQUIRE(editor->getHeight() == 620);

    // Render full 920x620 screenshot
    auto snapshot = editor->createComponentSnapshot(editor->getLocalBounds());
    REQUIRE(snapshot.isValid());
    REQUIRE(snapshot.getWidth() == 920);
    REQUIRE(snapshot.getHeight() == 620);

    const juce::File outPng = juce::File::getSpecialLocation(juce::File::tempDirectory)
                                  .getChildFile("et5m_organ_gui_test.png");
    if (outPng.existsAsFile()) {
        outPng.deleteFile();
    }
    {
        juce::FileOutputStream stream(outPng);
        REQUIRE(stream.openedOk());
        juce::PNGImageFormat png;
        REQUIRE(png.writeImageToStream(snapshot, stream));
    }
    REQUIRE(outPng.existsAsFile());
    REQUIRE(outPng.getSize() > 20000);
    REQUIRE(outPng.deleteFile());

    // Verify dynamic resizing works without crashing or collapsing
    editor->setSize(820, 580);
    REQUIRE(editor->getWidth() == 820);
    REQUIRE(editor->getHeight() == 580);
    auto resizedSnap = editor->createComponentSnapshot(editor->getLocalBounds());
    REQUIRE(resizedSnap.isValid());
    REQUIRE(resizedSnap.getWidth() == 820);
    REQUIRE(resizedSnap.getHeight() == 580);
}
