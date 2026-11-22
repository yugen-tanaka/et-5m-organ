#include "PluginEditor.hpp"
#include "FactoryPresets.hpp"

#include <algorithm>
#include <cmath>

namespace et5m::plugin {

namespace {

namespace pal {
const juce::Colour mahoganyTop{0xff402016};
const juce::Colour mahoganyMid{0xff2e150e};
const juce::Colour mahoganyDark{0xff1c0c08};
const juce::Colour brassGold{0xffd4b572};
const juce::Colour brassDim{0xff8c7240};
const juce::Colour ivoryBright{0xfff6eedc};
const juce::Colour ivoryMid{0xffe5d8bc};
const juce::Colour ivoryShade{0xffb9ab8d};
const juce::Colour charcoalInk{0xff1d1916};
const juce::Colour crimsonInk{0xffa91e22};
const juce::Colour creamText{0xfff1e9d5};
const juce::Colour mutedText{0xffc4b69b};
const juce::Colour amberLed{0xffffb838};
const juce::Colour bayCardBg{0xd91c1512};
const juce::Colour grilleGold{0xffa89066};
const juce::Colour grilleCharcoal{0xff241c17};
} // namespace pal

juce::String midiNoteToName(int midi) {
    if (midi < 0 || midi > 127) return "--";
    static constexpr const char* kNotes[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    const int pc = midi % 12;
    const int oct = (midi / 12) - 1;
    return juce::String(kNotes[pc]) + juce::String(oct);
}

void drawConsoleGroupLabel(juce::Graphics& g, juce::Rectangle<int> bounds, const juce::String& text) {
    if (bounds.getWidth() < 20) return;
    const juce::Font f(juce::FontOptions(10.0f, juce::Font::bold));
    g.setFont(f);
    g.setColour(pal::brassGold.withAlpha(0.88f));
    g.drawText(text, bounds.removeFromTop(14), juce::Justification::centred, false);
}

} // namespace

IvoryRockerTablet::IvoryRockerTablet(const juce::String& label, TabletInk ink)
    : juce::Button(label), label_(label), ink_(ink) {
    setClickingTogglesState(true);
}

void IvoryRockerTablet::paintButton(juce::Graphics& g, bool isMouseOver, bool isButtonDown) {
    const auto r = getLocalBounds().toFloat().reduced(0.5f, 1.5f);
    const bool on = getToggleState();
    const float x = r.getX();
    const float y = r.getY();
    const float w = r.getWidth();
    const float h = r.getHeight();
    const float sidePad = 2.0f;

    const auto inkCol = (ink_ == TabletInk::CrimsonRed) ? pal::crimsonInk : pal::charcoalInk;
    const float fontSize = (label_.length() >= 8) ? 9.2f : 10.4f;

    if (!on) {
        // OFF: Rigid tablet resting flat/level in the recessed slot
        const float topY = y + 2.5f;
        const float faceBot = y + h - 4.0f;
        const float lipBot = y + h - 1.5f;

        // Subtle bottom front edge thickness lip
        g.setColour(juce::Colour(0xff9e8e70));
        g.fillRoundedRectangle(x + sidePad, faceBot - 1.5f, w - sidePad * 2.0f, lipBot - (faceBot - 1.5f), 2.5f);

        // Flat horizontal ivory top surface
        juce::ColourGradient offGrad(
            isMouseOver ? juce::Colour(0xfffaf3e3) : juce::Colour(0xfff3ead6), x, topY,
            juce::Colour(0xffe4d7ba), x, faceBot, false);
        g.setGradientFill(offGrad);
        g.fillRoundedRectangle(x + sidePad, topY, w - sidePad * 2.0f, faceBot - topY, 3.0f);

        // Subtle edge outline & subtle top highlight
        g.setColour(juce::Colour(0x404a3c2a));
        g.drawRoundedRectangle(x + sidePad, topY, w - sidePad * 2.0f, faceBot - topY, 3.0f, 0.8f);
        g.setColour(juce::Colours::white.withAlpha(isMouseOver ? 0.45f : 0.30f));
        g.drawLine(x + sidePad + 2.5f, topY + 0.6f, x + w - sidePad - 2.5f, topY + 0.6f, 1.0f);

        g.setColour(inkCol.withAlpha(isButtonDown ? 0.72f : 0.88f));
        g.setFont(juce::Font(juce::FontOptions(fontSize, juce::Font::bold)));
        auto textRect = juce::Rectangle<float>(x + 3.0f, topY + 4.0f, w - 6.0f, (faceBot - topY) - 8.0f);
        g.drawFittedText(label_, textRect.toNearestInt(), juce::Justification::centred, 2, 0.74f);
        return;
    }

    // ON: Rigid flat-top seesaw tilted forward: rear/top RAISED UP (exposed thickness wall),
    // front/bottom PRESSED DOWN into sunken slot gap
    const float wallTop = y + 1.5f;
    const float faceTop = y + std::max(12.5f, h * 0.11f);
    const float faceBot = y + h - std::max(8.5f, h * 0.08f);
    const float slotBot = y + h - 1.0f;

    // Bottom (front) dark sunken slot gap where front end presses into panel
    g.setColour(juce::Colours::black.withAlpha(0.60f));
    g.fillRoundedRectangle(x + sidePad, faceBot - 2.0f, w - sidePad * 2.0f, slotBot - (faceBot - 2.0f), 2.0f);

    // Top (rear) raised 3D thickness wall / bevel exposed above slot
    juce::ColourGradient wallGrad(juce::Colour(0xff968567), x, wallTop,
                                  juce::Colour(0xffd8c7a7), x, faceTop, false);
    g.setGradientFill(wallGrad);
    g.fillRoundedRectangle(x + sidePad, wallTop, w - sidePad * 2.0f, (faceTop + 2.0f) - wallTop, 3.0f);

    // Continuous rigid flat rectangular top surface sloping forward-downward
    juce::ColourGradient onGrad(
        isMouseOver ? juce::Colour(0xfffffdf4) : juce::Colour(0xfffaf2df), x, faceTop,
        juce::Colour(0xffd8caac), x, faceBot, false);
    g.setGradientFill(onGrad);
    g.fillRoundedRectangle(x + sidePad, faceTop, w - sidePad * 2.0f, faceBot - faceTop, 3.0f);

    // Subtle border & crisp white ridge highlight where flat top meets raised rear wall
    g.setColour(juce::Colour(0x454a3c2a));
    g.drawRoundedRectangle(x + sidePad, faceTop, w - sidePad * 2.0f, faceBot - faceTop, 3.0f, 0.9f);
    g.setColour(juce::Colours::white.withAlpha(isMouseOver ? 0.72f : 0.55f));
    g.drawLine(x + sidePad + 2.5f, faceTop + 0.5f, x + w - sidePad - 2.5f, faceTop + 0.5f, 1.1f);

    g.setColour(inkCol);
    g.setFont(juce::Font(juce::FontOptions(fontSize, juce::Font::bold)));
    auto textRect = juce::Rectangle<float>(x + 3.0f, faceTop + 4.0f, w - 6.0f, (faceBot - faceTop) - 8.0f);
    g.drawFittedText(label_, textRect.toNearestInt(), juce::Justification::centred, 2, 0.74f);
}

VintageConsoleLookAndFeel::VintageConsoleLookAndFeel() {
    setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff23140f));
    setColour(juce::ComboBox::textColourId, pal::creamText);
    setColour(juce::ComboBox::outlineColourId, pal::brassDim);
    setColour(juce::ComboBox::arrowColourId, pal::brassGold);
    setColour(juce::PopupMenu::backgroundColourId, juce::Colour(0xff261611));
    setColour(juce::PopupMenu::textColourId, pal::creamText);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, juce::Colour(0xff5a3122));
    setColour(juce::TextButton::buttonColourId, juce::Colour(0xff381e15));
    setColour(juce::TextButton::textColourOffId, pal::creamText);
    setColour(juce::ToggleButton::textColourId, pal::creamText);
    setColour(juce::ToggleButton::tickColourId, pal::amberLed);
}

void VintageConsoleLookAndFeel::drawRotarySlider(
    juce::Graphics& g, int x, int y, int width, int height,
    float sliderPos, float rotaryStartAngle, float rotaryEndAngle, juce::Slider& slider) {
    const float maxR = std::min(width, height) * 0.44f;
    const float cx = static_cast<float>(x) + static_cast<float>(width) * 0.5f;
    const float cy = static_cast<float>(y) + static_cast<float>(height) * 0.5f;
    const float angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);

    // Subtle brass tick marks along arc perimeter
    constexpr int kNumTicks = 9;
    for (int i = 0; i < kNumTicks; ++i) {
        const float frac = static_cast<float>(i) / static_cast<float>(kNumTicks - 1);
        const float a = rotaryStartAngle + frac * (rotaryEndAngle - rotaryStartAngle);
        const float r0 = maxR * 0.96f;
        const float r1 = maxR * 1.07f;
        const float tx0 = cx + std::sin(a) * r0;
        const float ty0 = cy - std::cos(a) * r0;
        const float tx1 = cx + std::sin(a) * r1;
        const float ty1 = cy - std::cos(a) * r1;
        const bool passed = (frac <= sliderPos + 0.001f);
        g.setColour(passed ? pal::brassGold.withAlpha(0.85f) : pal::brassDim.withAlpha(0.40f));
        g.drawLine(tx0, ty0, tx1, ty1, passed ? 1.5f : 1.0f);
    }

    // 1965 cream/ivory single one-piece molded lever-pointer knob (seamless base + protruding handle + dorsal ridge)
    const float hubR = maxR * 0.44f;
    const float joinAngle = 0.62f;
    const float tipY = -maxR * 0.95f;
    const float tipR = hubR * 0.22f;
    const float tailY = hubR * 0.58f;

    juce::Path unifiedKnob;
    unifiedKnob.addCentredArc(0.0f, 0.0f, hubR, hubR, 0.0f,
                              joinAngle, juce::MathConstants<float>::twoPi - joinAngle, true);
    unifiedKnob.lineTo(-tipR, tipY);
    unifiedKnob.quadraticTo(0.0f, tipY - tipR * 1.25f, tipR, tipY);
    unifiedKnob.closeSubPath();

    const auto xform = juce::AffineTransform::rotation(angle).translated(cx, cy);

    // Soft cast shadow on glossy mahogany panel
    juce::Path shadowPath(unifiedKnob);
    shadowPath.applyTransform(juce::AffineTransform::rotation(angle).translated(cx + 2.4f, cy + 3.4f));
    g.setColour(juce::Colours::black.withAlpha(0.42f));
    g.fillPath(shadowPath);

    // Single seamless one-piece cream body with zero dividing seams or dark outlines
    unifiedKnob.applyTransform(xform);
    juce::ColourGradient bodyGrad(
        slider.isMouseOverOrDragging() ? juce::Colour(0xfffffef7) : juce::Colour(0xfffbf4e3),
        cx - maxR * 0.45f, cy - maxR * 0.55f,
        juce::Colour(0xffd2c0a0), cx + maxR * 0.45f, cy + maxR * 0.55f, false);
    g.setGradientFill(bodyGrad);
    g.fillPath(unifiedKnob);

    // Central raised dorsal spine/ridge integrated into the one-piece cream molding
    const float ridgeW = hubR * 0.16f;
    const float ridgeTipY = tipY - tipR * 0.65f;
    const float ridgeTailY = tailY * 0.92f;

    juce::Path ridgeLeft;
    ridgeLeft.startNewSubPath(0.0f, ridgeTailY);
    ridgeLeft.lineTo(-ridgeW, ridgeTailY * 0.45f);
    ridgeLeft.lineTo(-ridgeW * 0.85f, ridgeTipY * 0.85f);
    ridgeLeft.lineTo(0.0f, ridgeTipY);
    ridgeLeft.closeSubPath();
    ridgeLeft.applyTransform(xform);
    g.setColour(slider.isMouseOverOrDragging() ? juce::Colour(0xfffffdf0) : juce::Colour(0xfffffef8));
    g.fillPath(ridgeLeft);

    juce::Path ridgeRight;
    ridgeRight.startNewSubPath(0.0f, ridgeTailY);
    ridgeRight.lineTo(ridgeW, ridgeTailY * 0.45f);
    ridgeRight.lineTo(ridgeW * 0.85f, ridgeTipY * 0.85f);
    ridgeRight.lineTo(0.0f, ridgeTipY);
    ridgeRight.closeSubPath();
    ridgeRight.applyTransform(xform);
    g.setColour(juce::Colour(0xffcab898));
    g.fillPath(ridgeRight);
}

juce::String TelemetryDisplay::formatVoiceText(int up, int lo, int pedMidi) {
    return "UP:" + juce::String(up)
         + "  LO:" + juce::String(lo)
         + "  PED:" + midiNoteToName(pedMidi);
}

void TelemetryDisplay::update(float mainRms) {
    mainRms_ = mainRms;
    repaint();
}

void TelemetryDisplay::paint(juce::Graphics& g) {
    const auto r = getLocalBounds().toFloat().reduced(1.0f);
    g.setColour(juce::Colour(0xff140c09));
    g.fillRoundedRectangle(r, 4.0f);
    g.setColour(pal::brassDim.withAlpha(0.75f));
    g.drawRoundedRectangle(r, 4.0f, 1.0f);

    auto meterArea = getLocalBounds().reduced(8, 3);
    g.setColour(pal::mutedText);
    g.setFont(juce::Font(juce::FontOptions(8.5f, juce::Font::bold)));
    g.drawText("OUT", meterArea.removeFromLeft(28), juce::Justification::centredLeft, false);

    auto bar = meterArea.reduced(1, 5).toFloat();
    g.setColour(juce::Colour(0xff261b16));
    g.fillRoundedRectangle(bar, 2.0f);
    const float db = juce::Decibels::gainToDecibels(mainRms_, -60.0f);
    const float norm = juce::jlimit(0.0f, 1.0f, (db + 60.0f) / 60.0f);
    if (norm > 0.0f) {
        g.setColour(pal::amberLed);
        g.fillRoundedRectangle(bar.withWidth(bar.getWidth() * norm), 2.0f);
    }
    g.setColour(juce::Colour(0x55140c09));
    for (float f : {0.25f, 0.50f, 0.75f}) {
        const float tx = bar.getX() + bar.getWidth() * f;
        g.drawVerticalLine(juce::roundToInt(tx), bar.getY(), bar.getBottom());
    }
    g.setColour(pal::brassDim.withAlpha(0.35f));
    g.drawRoundedRectangle(bar, 2.0f, 0.8f);
}

ET5MAudioProcessorEditor::ET5MAudioProcessorEditor(ET5MAudioProcessor& p)
    : AudioProcessorEditor(&p), proc_(p) {
    setLookAndFeel(&consoleLook_);
    setResizable(true, true);
    setResizeLimits(780, 540, 1360, 920);
    setSize(920, 620);

    // Preset controls
    for (int i = 0; i < proc_.getNumPrograms(); ++i) {
        presetBox_.addItem(proc_.getProgramName(i), i + 1);
    }
    syncPresetBox();
    presetBox_.onChange = [this] {
        const int idx = presetBox_.getSelectedItemIndex();
        if (idx >= 0) proc_.setCurrentProgram(idx);
    };
    addAndMakeVisible(presetBox_);

    prevPresetBtn_.onClick = [this] {
        const int n = proc_.getNumPrograms();
        const int next = (proc_.getCurrentProgram() - 1 + n) % n;
        proc_.setCurrentProgram(next);
        syncPresetBox();
    };
    nextPresetBtn_.onClick = [this] {
        const int n = proc_.getNumPrograms();
        const int next = (proc_.getCurrentProgram() + 1) % n;
        proc_.setCurrentProgram(next);
        syncPresetBox();
    };
    allOffBtn_.onClick = [this] {
        clearAllToneAndEffectTablets(proc_.apvts());
    };
    addAndMakeVisible(prevPresetBtn_);
    addAndMakeVisible(nextPresetBtn_);
    addAndMakeVisible(allOffBtn_);
    addAndMakeVisible(telemetry_);

    // Group 0: Effect & Pedal (4 tablets: TREMOLO [red], VIBRATO [red], BOURDON, TROMBONE)
    addTablet(param_ids::tremolo,       "TREMOLO",  TabletInk::CrimsonRed,   0);
    addTablet(param_ids::vibrato,       "VIBRATO",  TabletInk::CrimsonRed,   0);
    addTablet(param_ids::pedalBourdon,  "BOURDON",  TabletInk::DarkCharcoal, 0);
    addTablet(param_ids::pedalTrombone, "TROMBONE", TabletInk::DarkCharcoal, 0);

    // Group 1: Lower Manual (3 tablets: DIAPASON, CELLO, HORN)
    addTablet(param_ids::lowerDiapason, "DIAPASON", TabletInk::DarkCharcoal, 1);
    addTablet(param_ids::lowerCello,    "CELLO",    TabletInk::DarkCharcoal, 1);
    addTablet(param_ids::lowerHorn,     "HORN",     TabletInk::DarkCharcoal, 1);

    // Group 2: Upper Manual (5 tablets: DIAPASON, FLUTE, VIOLIN, OBOE, TRUMPET)
    addTablet(param_ids::upperDiapason, "DIAPASON", TabletInk::DarkCharcoal, 2);
    addTablet(param_ids::upperFlute,    "FLUTE",    TabletInk::DarkCharcoal, 2);
    addTablet(param_ids::upperViolin,   "VIOLIN",   TabletInk::DarkCharcoal, 2);
    addTablet(param_ids::upperOboe,     "OBOE",     TabletInk::DarkCharcoal, 2);
    addTablet(param_ids::upperTrumpet,  "TRUMPET",  TabletInk::DarkCharcoal, 2);

    // Group 3: Right Cheekblock Sustain Tablets (3)
    addTablet(param_ids::pedalSustain,  "PED\nSUST",   TabletInk::DarkCharcoal, 3);
    addTablet(param_ids::upperSustain,  "UPPER\nSUST", TabletInk::DarkCharcoal, 3);
    addTablet(param_ids::kneeSustain,   "KNEE\nLEVER", TabletInk::DarkCharcoal, 3);

    // Left Cheekblock Console Knobs (5) + Right Cheekblock Sustain Time (1)
    addKnob(param_ids::pedalVolume,   "PEDAL VOL",  false);
    addKnob(param_ids::manualBalance, "MANUAL BAL", false);
    addKnob(param_ids::repeatSpeed,   "REPEAT SPD", false);
    addKnob(param_ids::expression,    "EXPRESSION", false);
    addKnob(param_ids::masterVolume,  "MASTER VOL", false);
    addKnob(param_ids::sustainTimeMs, "SUST TIME",  false, " ms");

    // Lower Bay Left: Tube Amp, Cabinet & Line Out
    addKnob(param_ids::cabinetMix, "CABINET MIX", true);
    addKnob(param_ids::tubeAmount, "TUBE DRIVE",  true);
    addKnob(param_ids::a4Hz,       "A4 TUNING",   true, " Hz");
    addToggle(param_ids::ampCabBypass, "Bypass Tube Amp & Cabinet (Line Out)", true);

    // Lower Bay Right: MIDI Routing & Manual Split Assignment
    addCombo(param_ids::midiMode,    "Mode",  {"Multi-Ch", "Split"});
    addCombo(param_ids::upperMidiCh, "UPPER", makeManualMidiChannelChoices());
    addCombo(param_ids::lowerMidiCh, "LOWER", makeManualMidiChannelChoices());
    addCombo(param_ids::pedalMidiCh, "PEDAL", makeManualMidiChannelChoices());

    addHSlider(param_ids::upperOctave,   "Oct",     true,  false);
    addHSlider(param_ids::lowerOctave,   "Oct",     true,  false);
    addHSlider(param_ids::pedalOctave,   "Oct",     true,  false);
    addHSlider(param_ids::pedalSplitMax, "Ped Top", false, true);
    addHSlider(param_ids::lowerSplitMax, "Split",   false, true);

    addToggle(param_ids::foldPedalOctave, "Fold Pedal Octave", false);
    addToggle(param_ids::clampKeyRange,   "Clamp Hardware Range", false);

    startTimerHz(20);
    timerCallback();
    resized();
}

ET5MAudioProcessorEditor::~ET5MAudioProcessorEditor() {
    stopTimer();
    setLookAndFeel(nullptr);
}

void ET5MAudioProcessorEditor::addTablet(const char* paramId, const juce::String& title, TabletInk ink, int group) {
    TabletSlot slot;
    slot.button = std::make_unique<IvoryRockerTablet>(title, ink);
    slot.attachment = std::make_unique<ButtonAttachment>(proc_.apvts(), paramId, *slot.button);
    slot.group = group;
    addAndMakeVisible(*slot.button);
    tablets_.push_back(std::move(slot));
}

void ET5MAudioProcessorEditor::addKnob(const char* paramId, const juce::String& title, bool isBay, const juce::String& suffix) {
    KnobSlot slot;
    slot.isBay = isBay;
    slot.slider = std::make_unique<juce::Slider>(juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow);
    slot.slider->setTextBoxStyle(juce::Slider::TextBoxBelow, false, isBay ? 68 : 72, 14);
    slot.slider->setColour(juce::Slider::textBoxTextColourId, pal::creamText);
    slot.slider->setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    if (suffix.isNotEmpty()) {
        slot.slider->setTextValueSuffix(suffix);
    }
    slot.label = std::make_unique<juce::Label>(title, title);
    slot.label->setJustificationType(juce::Justification::centred);
    slot.label->setColour(juce::Label::textColourId, pal::brassGold);
    slot.label->setFont(juce::Font(juce::FontOptions(9.0f, juce::Font::bold)));
    slot.attachment = std::make_unique<SliderAttachment>(proc_.apvts(), paramId, *slot.slider);
    addAndMakeVisible(*slot.slider);
    addAndMakeVisible(*slot.label);
    knobs_.push_back(std::move(slot));
}

void ET5MAudioProcessorEditor::addCombo(const char* paramId, const juce::String& title, const juce::StringArray& items) {
    ComboSlot slot;
    slot.box = std::make_unique<juce::ComboBox>();
    slot.box->addItemList(items, 1);
    slot.label = std::make_unique<juce::Label>(title, title);
    slot.label->setJustificationType(juce::Justification::centredLeft);
    slot.label->setColour(juce::Label::textColourId, (title == "Mode") ? pal::mutedText : pal::brassGold);
    slot.label->setFont(juce::Font(juce::FontOptions(9.0f, juce::Font::bold)));
    slot.attachment = std::make_unique<ComboAttachment>(proc_.apvts(), paramId, *slot.box);
    addAndMakeVisible(*slot.box);
    addAndMakeVisible(*slot.label);
    combos_.push_back(std::move(slot));
}

void ET5MAudioProcessorEditor::addHSlider(const char* paramId, const juce::String& title, bool isOctaveRow, bool formatMidiNote) {
    HSliderSlot slot;
    slot.isOctaveRow = isOctaveRow;
    slot.slider = std::make_unique<juce::Slider>(juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight);
    slot.slider->setTextBoxStyle(juce::Slider::TextBoxRight, false, formatMidiNote ? 42 : 30, 15);
    slot.slider->setColour(juce::Slider::textBoxTextColourId, pal::creamText);
    slot.slider->setColour(juce::Slider::textBoxOutlineColourId, pal::brassDim.withAlpha(0.5f));
    slot.slider->setColour(juce::Slider::thumbColourId, pal::ivoryBright);
    slot.slider->setColour(juce::Slider::trackColourId, pal::brassGold);
    slot.label = std::make_unique<juce::Label>(title, title);
    slot.label->setJustificationType(juce::Justification::centredLeft);
    slot.label->setColour(juce::Label::textColourId, pal::mutedText);
    slot.label->setFont(juce::Font(juce::FontOptions(9.0f, juce::Font::bold)));
    slot.attachment = std::make_unique<SliderAttachment>(proc_.apvts(), paramId, *slot.slider);
    if (formatMidiNote) {
        slot.slider->textFromValueFunction = [](double v) {
            return midiNoteToName(static_cast<int>(std::lround(v)));
        };
    } else {
        slot.slider->textFromValueFunction = [](double v) {
            const int iv = static_cast<int>(std::lround(v));
            return (iv > 0) ? ("+" + juce::String(iv)) : juce::String(iv);
        };
    }
    slot.slider->updateText();
    addAndMakeVisible(*slot.slider);
    addAndMakeVisible(*slot.label);
    hSliders_.push_back(std::move(slot));
}

void ET5MAudioProcessorEditor::addToggle(const char* paramId, const juce::String& title, bool isLeftBay) {
    ToggleSlot slot;
    slot.isLeftBay = isLeftBay;
    slot.button = std::make_unique<juce::ToggleButton>(title);
    slot.attachment = std::make_unique<ButtonAttachment>(proc_.apvts(), paramId, *slot.button);
    addAndMakeVisible(*slot.button);
    toggles_.push_back(std::move(slot));
}

void ET5MAudioProcessorEditor::syncPresetBox() {
    presetBox_.setSelectedId(proc_.getCurrentProgram() + 1, juce::dontSendNotification);
}

void ET5MAudioProcessorEditor::timerCallback() {
    telemetry_.update(proc_.mainBusRms());

    const std::array<bool, 3> nextActive{
        proc_.activeUpperNotes() > 0,
        proc_.activeLowerNotes() > 0,
        proc_.activePedalMidi() >= 0
    };
    if (nextActive != manualActive_) {
        manualActive_ = nextActive;
        if (!rightCardBounds_.isEmpty()) {
            repaint(rightCardBounds_);
        }
    }
}

void ET5MAudioProcessorEditor::paint(juce::Graphics& g) {
    const auto bounds = getLocalBounds().toFloat();

    // Gloss dark mahogany console cabinet
    juce::ColourGradient bgGrad(pal::mahoganyTop, 0.0f, 0.0f, pal::mahoganyDark, 0.0f, bounds.getBottom(), false);
    g.setGradientFill(bgGrad);
    g.fillRect(bounds);

    // Subtle lacquer specular reflection band across top console rail
    juce::ColourGradient sheen(juce::Colours::white.withAlpha(0.08f), 0.0f, 0.0f,
                               juce::Colours::transparentWhite, 0.0f, 120.0f, false);
    g.setGradientFill(sheen);
    g.fillRect(bounds.withHeight(120.0f));

    // Recessed stop tablet rail background
    g.setColour(pal::mahoganyDark.withAlpha(0.85f));
    g.fillRoundedRectangle(tabletRailBounds_.toFloat(), 6.0f);
    g.setColour(pal::brassDim.withAlpha(0.6f));
    g.drawRoundedRectangle(tabletRailBounds_.toFloat(), 6.0f, 1.2f);

    drawConsoleGroupLabel(g, groupEffectPedalBounds_, "EFFECT & PEDAL");
    drawConsoleGroupLabel(g, groupLowerBounds_,       "LOWER MANUAL");
    drawConsoleGroupLabel(g, groupUpperBounds_,       "UPPER MANUAL");

    // Cheekblock console rail
    g.setColour(pal::mahoganyMid);
    g.fillRoundedRectangle(cheekRailBounds_.toFloat(), 5.0f);
    g.setColour(pal::brassDim.withAlpha(0.45f));
    g.drawRoundedRectangle(cheekRailBounds_.toFloat(), 5.0f, 1.0f);
    drawConsoleGroupLabel(g, leftCheekBounds_,  "VOLUME & MODULATION");
    drawConsoleGroupLabel(g, rightCheekBounds_, "SUSTAIN & KNEE LEVER");

    // Recessed shared panel cutout slots behind grouped rocker tablets
    for (const auto& pocket : slotPocketBounds_) {
        if (!pocket.isEmpty()) {
            g.setColour(juce::Colour(0xff0b0504));
            g.fillRoundedRectangle(pocket.toFloat(), 3.5f);
            g.setColour(juce::Colour(0xff3e281d));
            g.drawRoundedRectangle(pocket.toFloat(), 3.5f, 1.0f);
        }
    }

    // Lower speaker enclosure: striped woven grille cloth texture (gold/charcoal vertical threads)
    if (!bayBounds_.isEmpty()) {
        g.saveState();
        g.reduceClipRegion(bayBounds_);
        g.setColour(pal::grilleCharcoal);
        g.fillRect(bayBounds_);
        for (int sx = bayBounds_.getX(); sx < bayBounds_.getRight(); sx += 7) {
            g.setColour(pal::grilleGold.withAlpha(0.42f));
            g.fillRect(sx, bayBounds_.getY(), 3, bayBounds_.getHeight());
        }
        for (int sy = bayBounds_.getY(); sy < bayBounds_.getBottom(); sy += 4) {
            g.setColour(juce::Colours::black.withAlpha(0.18f));
            g.drawHorizontalLine(sy, static_cast<float>(bayBounds_.getX()), static_cast<float>(bayBounds_.getRight()));
        }
        g.restoreState();

        auto drawBayCard = [&](juce::Rectangle<int> card, const juce::String& title) {
            g.setColour(pal::bayCardBg);
            g.fillRoundedRectangle(card.toFloat(), 7.0f);
            g.setColour(pal::brassDim);
            g.drawRoundedRectangle(card.toFloat(), 7.0f, 1.2f);
            g.setColour(pal::brassGold);
            g.setFont(juce::Font(juce::FontOptions(10.0f, juce::Font::bold)));
            g.drawText(title, card.withHeight(20).reduced(10, 2), juce::Justification::centredLeft, false);
        };
        drawBayCard(leftCardBounds_,  "TUBE AMP, CABINET & LINE OUT");
        drawBayCard(rightCardBounds_, "MIDI ROUTING & MANUAL SPLIT ASSIGNMENT");

        for (const auto& rowR : manualRowBounds_) {
            if (!rowR.isEmpty()) {
                g.setColour(juce::Colour(0x38000000));
                g.fillRoundedRectangle(rowR.toFloat(), 4.0f);
                g.setColour(pal::brassDim.withAlpha(0.30f));
                g.drawRoundedRectangle(rowR.toFloat(), 4.0f, 0.8f);
            }
        }

        for (std::size_t i = 0; i < 3; ++i) {
            const auto st = manualStatusBounds_[i];
            if (st.isEmpty()) continue;
            const float dotR = 2.5f;
            const float dotCx = static_cast<float>(st.getCentreX());
            const float dotCy = static_cast<float>(st.getCentreY());
            g.setColour(manualActive_[i] ? pal::amberLed.withAlpha(0.85f) : pal::mutedText.withAlpha(0.18f));
            g.fillEllipse(dotCx - dotR, dotCy - dotR, dotR * 2.0f, dotR * 2.0f);
        }
    }
}

void ET5MAudioProcessorEditor::resized() {
    auto area = getLocalBounds().reduced(10);

    // 1. Header bar
    headerBounds_ = area.removeFromTop(32);
    auto hdr = headerBounds_;
    prevPresetBtn_.setBounds(hdr.removeFromLeft(32).reduced(1, 3));
    presetBox_.setBounds(hdr.removeFromLeft(205).reduced(2, 3));
    nextPresetBtn_.setBounds(hdr.removeFromLeft(32).reduced(1, 3));
    allOffBtn_.setBounds(hdr.removeFromLeft(66).reduced(2, 3));
    telemetry_.setBounds(hdr.removeFromRight(184).reduced(0, 2));

    area.removeFromTop(6);

    // Equal rail heights so top tone tablets & sustain tablets share identical aspect ratio
    const int bayH = juce::jlimit(168, 204, area.getHeight() * 34 / 100);
    const int railsAvailH = std::max(240, area.getHeight() - 12 - bayH);
    const int tabletRailH = railsAvailH / 2;
    const int cheekRailH = railsAvailH - tabletRailH;

    // 2. Upper Stop Tablet Rail: 12 rocker tablets grouped into 3 shared cutout slots (4 + 3 + 5)
    tabletRailBounds_ = area.removeFromTop(tabletRailH);
    auto tr = tabletRailBounds_.reduced(10, 6);
    const int labelH = 15;
    const int groupGap = 24;
    const int padX = 4;
    const int seam = 2;
    const int tabW = juce::jlimit(44, 62, (tr.getWidth() - 2 * groupGap - 6 * padX - 9 * seam) / 12);
    const int tabH = std::min(juce::roundToInt(static_cast<float>(tabW) * 2.10f), tr.getHeight() - labelH - 4);

    auto pocketWidth = [&](int count, int itemW) { return count * itemW + (count - 1) * seam + padX * 2; };
    const int w0 = pocketWidth(4, tabW);
    const int w1 = pocketWidth(3, tabW);
    const int w2 = pocketWidth(5, tabW);
    const int totalTabsW = w0 + groupGap + w1 + groupGap + w2;
    const int startX = tr.getX() + std::max(0, (tr.getWidth() - totalTabsW) / 2);

    groupEffectPedalBounds_ = {startX, tr.getY(), w0, tr.getHeight()};
    groupLowerBounds_       = {startX + w0 + groupGap, tr.getY(), w1, tr.getHeight()};
    groupUpperBounds_       = {startX + w0 + groupGap + w1 + groupGap, tr.getY(), w2, tr.getHeight()};

    auto layoutTabletGroup = [&](int groupId, juce::Rectangle<int> box, std::size_t pocketIdx, int itemW, int itemH) {
        std::vector<IvoryRockerTablet*> btns;
        for (auto& t : tablets_) {
            if (t.group == groupId) btns.push_back(t.button.get());
        }
        if (btns.empty()) {
            slotPocketBounds_[pocketIdx] = {};
            return;
        }
        const int gw = pocketWidth(static_cast<int>(btns.size()), itemW);
        const int gx = box.getX() + std::max(0, (box.getWidth() - gw) / 2);
        const int gy = box.getY() + labelH + std::max(0, (box.getHeight() - labelH - itemH - 4) / 2);
        juce::Rectangle<int> pocket(gx, gy, gw, itemH + 4);
        slotPocketBounds_[pocketIdx] = pocket;

        int bx = gx + padX;
        for (auto* b : btns) {
            b->setBounds(bx, gy + 2, itemW, itemH);
            bx += itemW + seam;
        }
    };
    layoutTabletGroup(0, groupEffectPedalBounds_, 0, tabW, tabH);
    layoutTabletGroup(1, groupLowerBounds_, 1, tabW, tabH);
    layoutTabletGroup(2, groupUpperBounds_, 2, tabW, tabH);

    area.removeFromTop(6);

    // 3. Cheekblock & Sustain Control Rail (no center emblem)
    cheekRailBounds_ = area.removeFromTop(cheekRailH);
    auto cr = cheekRailBounds_.reduced(10, 6);
    leftCheekBounds_ = cr.removeFromLeft(static_cast<int>(cr.getWidth() * 0.54f));
    cr.removeFromLeft(10);
    rightCheekBounds_ = cr;

    // Layout 5 console knobs in leftCheekBounds_
    auto lc = leftCheekBounds_;
    lc.removeFromTop(15);
    std::vector<KnobSlot*> leftKnobs, bayKnobs;
    KnobSlot* sustKnob = nullptr;
    for (auto& k : knobs_) {
        if (k.isBay) bayKnobs.push_back(&k);
        else if (k.label->getText() == "SUST TIME") sustKnob = &k;
        else leftKnobs.push_back(&k);
    }
    if (!leftKnobs.empty()) {
        const int kw = lc.getWidth() / static_cast<int>(leftKnobs.size());
        for (auto* k : leftKnobs) {
            auto cell = lc.removeFromLeft(kw).reduced(2, 2);
            k->label->setBounds(cell.removeFromTop(14));
            k->slider->setBounds(cell);
        }
    }

    // Layout 3 sustain tablets matching top tone tablet aspect ratio & size + sustainTime knob
    auto rc = rightCheekBounds_;
    if (sustKnob != nullptr) {
        auto sc = rc.removeFromRight(88).reduced(2, 2);
        sc.removeFromTop(15);
        sustKnob->label->setBounds(sc.removeFromTop(14));
        sustKnob->slider->setBounds(sc);
    }
    const int sustTabH = std::min(tabH, rc.getHeight() - labelH - 4);
    layoutTabletGroup(3, rc, 3, tabW, sustTabH);

    area.removeFromTop(6);

    // 4. Compact Lower Grille-Cloth Enclosure Bay
    bayBounds_ = area;
    auto bayInner = bayBounds_.reduced(10, 8);
    leftCardBounds_ = bayInner.removeFromLeft(static_cast<int>(bayInner.getWidth() * 0.36f));
    bayInner.removeFromLeft(10);
    rightCardBounds_ = bayInner;

    // Left Card: 3 compact knobs (CABINET MIX, TUBE DRIVE, A4 TUNING) + Bypass checkbox
    auto lci = leftCardBounds_.reduced(8, 6);
    lci.removeFromTop(18);
    auto lBottom = lci.removeFromBottom(24);
    if (!bayKnobs.empty()) {
        const int kw = lci.getWidth() / static_cast<int>(bayKnobs.size());
        for (auto* k : bayKnobs) {
            auto cell = lci.removeFromLeft(kw).reduced(3, 2);
            k->label->setBounds(cell.removeFromTop(13));
            k->slider->setBounds(cell);
        }
    }
    std::vector<ToggleSlot*> leftTogs, rightTogs;
    for (auto& t : toggles_) {
        if (t.isLeftBay) leftTogs.push_back(&t);
        else rightTogs.push_back(&t);
    }
    if (!leftTogs.empty()) {
        const int tw = lBottom.getWidth() / static_cast<int>(leftTogs.size());
        for (auto* t : leftTogs) t->button->setBounds(lBottom.removeFromLeft(tw).reduced(2, 1));
    }

    // Right Card: Left column = 3 stacked Manual Rows (UPPER, LOWER, PEDAL); Right column = Mode & Splits
    auto rci = rightCardBounds_.reduced(8, 6);
    rci.removeFromTop(18);
    auto rBottom = rci.removeFromBottom(24);
    if (!rightTogs.empty()) {
        const int tw = rBottom.getWidth() / static_cast<int>(rightTogs.size());
        for (auto* t : rightTogs) t->button->setBounds(rBottom.removeFromLeft(tw).reduced(2, 1));
    }

    auto leftCol = rci.removeFromLeft(static_cast<int>(rci.getWidth() * 0.59f));
    rci.removeFromLeft(8);
    auto rightCol = rci;

    const int rowH = leftCol.getHeight() / 3;
    for (std::size_t i = 0; i < 3; ++i) {
        auto rowRect = leftCol.removeFromTop(rowH).reduced(0, 2);
        manualRowBounds_[i] = rowRect;
        auto cell = rowRect.reduced(5, 3);
        manualStatusBounds_[i] = cell.removeFromRight(14);
        cell.removeFromRight(4);
        const std::size_t comboIdx = i + 1;
        if (comboIdx < combos_.size() && i < hSliders_.size()) {
            combos_[comboIdx].label->setBounds(cell.removeFromLeft(44));
            combos_[comboIdx].box->setBounds(cell.removeFromLeft(76).reduced(2, 2));
            cell.removeFromLeft(6);
            hSliders_[i].label->setBounds(cell.removeFromLeft(22));
            hSliders_[i].slider->setBounds(cell);
        }
    }

    // Right column: Row 1 = Mode dropdown; Row 2 = Ped Top slider; Row 3 = Split slider
    auto mr1 = rightCol.removeFromTop(rowH).reduced(2, 4);
    if (!combos_.empty()) {
        combos_[0].label->setBounds(mr1.removeFromLeft(46));
        combos_[0].box->setBounds(mr1.reduced(0, 1));
    }
    auto mr2 = rightCol.removeFromTop(rowH).reduced(2, 4);
    if (hSliders_.size() > 3) {
        hSliders_[3].label->setBounds(mr2.removeFromLeft(46));
        hSliders_[3].slider->setBounds(mr2);
    }
    auto mr3 = rightCol.reduced(2, 4);
    if (hSliders_.size() > 4) {
        hSliders_[4].label->setBounds(mr3.removeFromLeft(46));
        hSliders_[4].slider->setBounds(mr3);
    }
}

} // namespace et5m::plugin
