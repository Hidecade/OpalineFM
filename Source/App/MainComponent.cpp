#include "MainComponent.h"

#include "Engine/Dx21Tables.h"

#include <algorithm>
#include <fstream>
#include <iterator>

namespace
{
constexpr int kFirstKeyboardNote = 48;
constexpr int kKeyboardNoteCount = 25;

std::vector<std::uint8_t> readBinaryFile(const juce::File& file)
{
    std::ifstream input(file.getFullPathName().toStdString(), std::ios::binary);
    if (!input)
        return {};

    return { std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>() };
}

juce::String displayNameForVoice(int index, const dx21::Dx21PatchWithMetadata& voice)
{
    const auto number = juce::String(index + 1).paddedLeft('0', 2);
    const auto name = voice.name.empty() ? juce::String("VOICE") : juce::String(voice.name);
    return "A" + number + " " + name;
}

bool isBlackKey(int midiNote)
{
    switch (midiNote % 12)
    {
        case 1:
        case 3:
        case 6:
        case 8:
        case 10:
            return true;
        default:
            return false;
    }
}

juce::String lfoWaveName(const int wave)
{
    static constexpr std::array<const char*, 4> names { "SAW UP", "SQUARE", "TRIANGLE", "S/H" };
    return names[static_cast<std::size_t>(juce::jlimit(0, 3, wave))];
}

juce::String operatorRole(const dx21::Dx21Patch& patch, const int opIndex)
{
    const auto& algorithm = dx21::dx21Algorithms()[static_cast<std::size_t>(juce::jlimit(1, 8, patch.algorithm) - 1)];
    bool carrier = false;
    juce::StringArray targets;
    for (int i = 0; i < algorithm.carrierCount; ++i)
        carrier = carrier || algorithm.carriers[static_cast<std::size_t>(i)] == opIndex;

    for (int target = 0; target < dx21::kOperatorCount; ++target)
    {
        for (int dep = 0; dep < algorithm.depCounts[static_cast<std::size_t>(target)]; ++dep)
        {
            if (algorithm.deps[static_cast<std::size_t>(target)][static_cast<std::size_t>(dep)] == opIndex)
                targets.add("OP" + juce::String(target + 1));
        }
    }

    if (carrier && !targets.isEmpty())
        return "Carrier + Mod -> " + targets.joinIntoString(",");
    if (carrier)
        return "Carrier";
    if (!targets.isEmpty())
        return "Mod -> " + targets.joinIntoString(",");
    return "Inactive";
}

float egTimeWeight(const int rate, const int maxRate)
{
    const float normalized = static_cast<float>(juce::jlimit(0, maxRate, rate)) / static_cast<float>(maxRate);
    return 0.12f + (1.0f - normalized) * (1.0f - normalized) * 0.88f;
}

const juce::Colour kAppBackground { 0xff020405 };
const juce::Colour kPanelTop { 0xff29261f };
const juce::Colour kPanelBottom { 0xff14130f };
const juce::Colour kPanelBorder { 0xff343126 };
const juce::Colour kPanelBorderOn { 0xff19d982 };
const juce::Colour kControlWell { 0xff050606 };
const juce::Colour kControlBorder { 0xff282820 };
const juce::Colour kTextPrimary { 0xffedf4f3 };
const juce::Colour kTextMuted { 0xffb7c2bd };
const juce::Colour kValueText { 0xffffd52b };
const juce::Colour kTeal { 0xff25d9c4 };
const juce::Colour kGreen { 0xff35e87e };
const juce::Colour kEnvelope { 0xffead39c };
std::array<std::atomic<bool>, 128> gMidiUiHeldNotes {};
} // namespace

MainComponent::Dx21LookAndFeel::Dx21LookAndFeel()
{
    setColour(juce::Slider::textBoxTextColourId, kValueText);
    setColour(juce::Slider::textBoxBackgroundColourId, juce::Colour(0x00000000));
    setColour(juce::Slider::textBoxOutlineColourId, juce::Colour(0x00000000));
    setColour(juce::Slider::rotarySliderFillColourId, kTeal);
    setColour(juce::Slider::rotarySliderOutlineColourId, juce::Colour(0xff5e625a));
}

void MainComponent::Dx21LookAndFeel::drawRotarySlider(juce::Graphics& g,
                                                      const int x,
                                                      const int y,
                                                      const int width,
                                                      const int height,
                                                      const float sliderPos,
                                                      const float rotaryStartAngle,
                                                      const float rotaryEndAngle,
                                                      juce::Slider&)
{
    const auto bounds = juce::Rectangle<float>(static_cast<float>(x),
                                              static_cast<float>(y),
                                              static_cast<float>(width),
                                              static_cast<float>(height)).reduced(3.0f);
    const auto knob = bounds.withSizeKeepingCentre(juce::jmin(bounds.getWidth(), bounds.getHeight()),
                                                   juce::jmin(bounds.getWidth(), bounds.getHeight()));
    const auto radius = knob.getWidth() * 0.46f;
    const auto centre = knob.getCentre();
    const auto angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);

    juce::Path track;
    track.addCentredArc(centre.x, centre.y, radius - 2.5f, radius - 2.5f, 0.0f, rotaryStartAngle, rotaryEndAngle, true);
    g.setColour(juce::Colour(0xff4f554e));
    g.strokePath(track, juce::PathStrokeType(3.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    juce::Path active;
    active.addCentredArc(centre.x, centre.y, radius - 2.5f, radius - 2.5f, 0.0f, rotaryStartAngle, angle, true);
    g.setColour(kTeal);
    g.strokePath(active, juce::PathStrokeType(3.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    const auto outer = knob.reduced(knob.getWidth() * 0.18f);
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xff3d3c36),
                                           outer.getX(),
                                           outer.getY(),
                                           juce::Colour(0xff0b0b09),
                                           outer.getRight(),
                                           outer.getBottom(),
                                           false));
    g.fillEllipse(outer);
    g.setColour(juce::Colour(0xff070909));
    g.drawEllipse(outer, 2.0f);

    const auto inner = outer.reduced(outer.getWidth() * 0.18f);
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xff2d2c27),
                                           inner.getX(),
                                           inner.getY(),
                                           juce::Colour(0xff11110f),
                                           inner.getRight(),
                                           inner.getBottom(),
                                           false));
    g.fillEllipse(inner);
    g.setColour(juce::Colour(0xff050607));
    g.drawEllipse(inner, 1.4f);

    juce::Path pointer;
    pointer.addRoundedRectangle(-2.0f, -radius * 0.53f, 4.0f, radius * 0.42f, 1.4f);
    pointer.applyTransform(juce::AffineTransform::rotation(angle).translated(centre));
    g.setColour(kEnvelope);
    g.fillPath(pointer);
}

void MainComponent::Dx21LookAndFeel::drawLinearSlider(juce::Graphics& g,
                                                      const int x,
                                                      const int y,
                                                      const int width,
                                                      const int height,
                                                      const float sliderPos,
                                                      const float minSliderPos,
                                                      const float maxSliderPos,
                                                      const juce::Slider::SliderStyle style,
                                                      juce::Slider& slider)
{
    if (style != juce::Slider::LinearVertical)
    {
        juce::LookAndFeel_V4::drawLinearSlider(g, x, y, width, height, sliderPos, minSliderPos, maxSliderPos, style, slider);
        return;
    }

    const auto bounds = juce::Rectangle<float>(static_cast<float>(x),
                                              static_cast<float>(y),
                                              static_cast<float>(width),
                                              static_cast<float>(height)).reduced(8.0f, 3.0f);
    const auto slot = bounds.withSizeKeepingCentre(juce::jmin(bounds.getWidth(), 46.0f),
                                                   bounds.getHeight());
    g.setColour(juce::Colour(0xff030303));
    g.fillRoundedRectangle(slot, 3.0f);
    g.setColour(juce::Colour(0xff24231e));
    g.drawRoundedRectangle(slot, 3.0f, 1.4f);

    const auto wheel = slot.reduced(7.0f, 8.0f);
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xff10100e),
                                           wheel.getCentreX(),
                                           wheel.getY(),
                                           juce::Colour(0xff3d3c38),
                                           wheel.getCentreX(),
                                           wheel.getCentreY(),
                                           false));
    g.fillRoundedRectangle(wheel, 8.0f);
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xff3a3935),
                                           wheel.getCentreX(),
                                           wheel.getCentreY(),
                                           juce::Colour(0xff0b0b0a),
                                           wheel.getCentreX(),
                                           wheel.getBottom(),
                                           false));
    g.fillRoundedRectangle(wheel.withTrimmedTop(wheel.getHeight() * 0.48f), 8.0f);

    const auto minY = wheel.getBottom();
    const auto maxY = wheel.getY();
    const auto valueY = juce::jlimit(maxY + 13.0f, minY - 13.0f, sliderPos);
    const auto grip = juce::Rectangle<float>(wheel.getX() + 2.0f,
                                            valueY - 14.0f,
                                            wheel.getWidth() - 4.0f,
                                            28.0f);
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xff696965),
                                           grip.getX(),
                                           grip.getY(),
                                           juce::Colour(0xff2d2d2a),
                                           grip.getX(),
                                           grip.getBottom(),
                                           false));
    g.fillRoundedRectangle(grip, 4.0f);
    g.setColour(juce::Colour(0xff11110f).withAlpha(0.7f));
    g.drawLine(grip.getX() + 2.0f, grip.getY() + 1.0f, grip.getRight() - 2.0f, grip.getY() + 1.0f, 1.0f);
    g.drawLine(grip.getX() + 2.0f, grip.getBottom() - 1.0f, grip.getRight() - 2.0f, grip.getBottom() - 1.0f, 1.0f);

    g.setColour(juce::Colour(0xff050505).withAlpha(0.72f));
    g.fillRect(wheel.withWidth(5.0f));
    g.fillRect(wheel.withX(wheel.getRight() - 5.0f).withWidth(5.0f));
    g.setColour(juce::Colour(0xff75746d).withAlpha(0.32f));
    g.drawVerticalLine(static_cast<int>(wheel.getCentreX()), wheel.getY() + 5.0f, wheel.getBottom() - 5.0f);

    g.setColour(juce::Colour(0xff050505));
    g.drawRoundedRectangle(wheel, 8.0f, 1.6f);
    g.setColour(juce::Colour(0xff000000).withAlpha(0.45f));
    g.fillRoundedRectangle(slot.reduced(1.5f).withTrimmedBottom(slot.getHeight() * 0.72f), 3.0f);
    g.fillRoundedRectangle(slot.reduced(1.5f).withTrimmedTop(slot.getHeight() * 0.72f), 3.0f);
}

void MainComponent::LcdComponent::setLines(juce::String topLine, juce::String bottomLine)
{
    line1 = topLine.substring(0, 16).paddedRight(' ', 16);
    line2 = bottomLine.substring(0, 16).paddedRight(' ', 16);
    repaint();
}

void MainComponent::LcdComponent::paint(juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat().reduced(3.0f);
    g.setColour(juce::Colour(0xff07111b));
    g.fillRoundedRectangle(area, 4.0f);
    g.setColour(juce::Colour(0xff1f6fff));
    g.drawRoundedRectangle(area, 4.0f, 1.0f);
    g.setColour(juce::Colour(0xff89d7ff));
    g.setFont(juce::FontOptions(18.0f, juce::Font::plain).withStyle("Monospaced"));
    g.drawText(line1, getLocalBounds().reduced(12, 8).removeFromTop(24), juce::Justification::centredLeft);
    g.drawText(line2, getLocalBounds().reduced(12, 8).withTrimmedTop(24), juce::Justification::centredLeft);
}

void MainComponent::AlgorithmComponent::setAlgorithm(const int newAlgorithm, const int newFeedback)
{
    algorithm = juce::jlimit(1, 8, newAlgorithm);
    feedback = juce::jlimit(0, 7, newFeedback);
    repaint();
}

void MainComponent::AlgorithmComponent::paint(juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat().reduced(6.0f);
    g.setGradientFill(juce::ColourGradient(kPanelTop,
                                           area.getX(),
                                           area.getY(),
                                           kPanelBottom,
                                           area.getX(),
                                           area.getBottom(),
                                           false));
    g.fillRoundedRectangle(area, 5.0f);
    g.setColour(kPanelBorder);
    g.drawRoundedRectangle(area, 5.0f, 1.0f);

    const auto& algo = dx21::dx21Algorithms()[static_cast<std::size_t>(algorithm - 1)];
    std::array<juce::Point<float>, 4> p;
    const float x0 = area.getX() + area.getWidth() * 0.22f;
    const float x1 = area.getCentreX();
    const float x2 = area.getRight() - area.getWidth() * 0.22f;
    const float y0 = area.getY() + 24.0f;
    const float y1 = area.getCentreY();
    const float y2 = area.getBottom() - 26.0f;
    switch (algorithm)
    {
        case 1: p = { juce::Point<float>(x1, y2), { x1, y1 + 8 }, { x1, y1 - 14 }, { x1, y0 } }; break;
        case 2: p = { juce::Point<float>(x1, y2), { x1, y1 }, { x0, y0 }, { x2, y0 } }; break;
        case 3: p = { juce::Point<float>(x1, y2), { x1, y1 }, { x1, y0 }, { x2, y1 } }; break;
        case 4: p = { juce::Point<float>(x0, y2), { x0, y1 }, { x2, y1 }, { x2, y0 } }; break;
        case 5: p = { juce::Point<float>(x0, y2), { x0, y1 }, { x2, y2 }, { x2, y1 } }; break;
        case 6: p = { juce::Point<float>(x0, y2), { x1, y2 }, { x2, y2 }, { x1, y0 } }; break;
        case 7: p = { juce::Point<float>(x0, y2), { x1, y2 }, { x2, y2 }, { x2, y1 } }; break;
        default: p = { juce::Point<float>(x0 - 8, y1), { x1 - 12, y1 }, { x1 + 18, y1 }, { x2 + 8, y1 } }; break;
    }

    g.setColour(kEnvelope);
    for (int target = 0; target < dx21::kOperatorCount; ++target)
    {
        for (int dep = 0; dep < algo.depCounts[static_cast<std::size_t>(target)]; ++dep)
        {
            const int source = algo.deps[static_cast<std::size_t>(target)][static_cast<std::size_t>(dep)];
            g.drawLine(juce::Line<float>(p[static_cast<std::size_t>(source)], p[static_cast<std::size_t>(target)]), 1.5f);
        }
    }

    g.setFont(juce::FontOptions(12.0f, juce::Font::bold));
    for (int i = 0; i < dx21::kOperatorCount; ++i)
    {
        bool carrier = false;
        for (int c = 0; c < algo.carrierCount; ++c)
            carrier = carrier || algo.carriers[static_cast<std::size_t>(c)] == i;
        g.setColour(carrier ? kGreen : kTextPrimary);
        const auto box = juce::Rectangle<float>(p[static_cast<std::size_t>(i)].x - 13.0f,
                                                p[static_cast<std::size_t>(i)].y - 10.0f,
                                                26.0f,
                                                20.0f);
        g.fillRoundedRectangle(box, 3.0f);
        g.setColour(juce::Colour(0xff11100d));
        g.drawText("OP" + juce::String(i + 1), box, juce::Justification::centred);
    }

    g.setColour(kTextMuted);
    g.drawText("ALG " + juce::String(algorithm) + "  FB " + juce::String(feedback),
               getLocalBounds().reduced(10).removeFromBottom(18),
               juce::Justification::centredLeft);
}

MainComponent::ScopeComponent::ScopeComponent()
{
    for (auto& sample : samples)
        sample.store(0.0f, std::memory_order_relaxed);
    startTimerHz(30);
}

void MainComponent::ScopeComponent::pushSample(const float sample)
{
    const int index = writeIndex.fetch_add(1, std::memory_order_relaxed) & 255;
    samples[static_cast<std::size_t>(index)].store(juce::jlimit(-1.0f, 1.0f, sample), std::memory_order_relaxed);
}

void MainComponent::ScopeComponent::paint(juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat().reduced(3.0f);
    g.setColour(kControlWell);
    g.fillRoundedRectangle(area, 4.0f);
    g.setColour(kControlBorder);
    g.drawRoundedRectangle(area, 4.0f, 1.0f);

    juce::Path path;
    const int newest = writeIndex.load(std::memory_order_relaxed);
    for (int i = 0; i < 256; ++i)
    {
        const int readIndex = (newest + i) & 255;
        const float value = samples[static_cast<std::size_t>(readIndex)].load(std::memory_order_relaxed);
        const float x = area.getX() + area.getWidth() * static_cast<float>(i) / 255.0f;
        const float y = area.getCentreY() - value * area.getHeight() * 0.42f;
        if (i == 0)
            path.startNewSubPath(x, y);
        else
            path.lineTo(x, y);
    }
    g.setColour(kTeal);
    g.strokePath(path, juce::PathStrokeType(1.5f));
}

MainComponent::OperatorComponent::OperatorComponent(const int operatorIndex, ChangeCallback callback)
    : opIndex(operatorIndex),
      onChange(std::move(callback)),
      enableButton("OP" + juce::String(operatorIndex + 1))
{
    addAndMakeVisible(enableButton);
    enableButton.addListener(this);
    roleLabel.setJustificationType(juce::Justification::centredLeft);
    roleLabel.setColour(juce::Label::textColourId, kTextMuted);
    addAndMakeVisible(roleLabel);

    static constexpr std::array<const char*, 6> opNames { "Ratio", "Detune", "Level", "Rate", "LevelSc", "Vel" };
    static constexpr std::array<const char*, 5> egNames { "AR", "D1R", "D1L", "D2R", "RR" };
    for (int i = 0; i < 6; ++i)
        addLabeledSlider(opLabels[static_cast<std::size_t>(i)], opSliders[static_cast<std::size_t>(i)], opNames[static_cast<std::size_t>(i)]);
    for (int i = 0; i < 5; ++i)
        addLabeledSlider(egLabels[static_cast<std::size_t>(i)], egSliders[static_cast<std::size_t>(i)], egNames[static_cast<std::size_t>(i)]);

    setupSlider(opSliders[0], 0, 63, 1, op.ratioIndex);
    setupSlider(opSliders[1], -3, 3, 1, op.detune);
    setupSlider(opSliders[2], 0, 99, 1, op.level);
    setupSlider(opSliders[3], 0, 3, 1, op.rateScale);
    setupSlider(opSliders[4], 0, 99, 1, op.levelScale);
    setupSlider(opSliders[5], 0, 7, 1, op.velocity);
    setupSlider(egSliders[0], 0, 31, 1, op.envelope.attackRate);
    setupSlider(egSliders[1], 0, 31, 1, op.envelope.decay1Rate);
    setupSlider(egSliders[2], 0, 15, 1, op.envelope.decay1Level);
    setupSlider(egSliders[3], 0, 31, 1, op.envelope.decay2Rate);
    setupSlider(egSliders[4], 0, 15, 1, op.envelope.releaseRate);
}

MainComponent::OperatorComponent::~OperatorComponent()
{
    for (auto& slider : opSliders)
        slider.setLookAndFeel(nullptr);
    for (auto& slider : egSliders)
        slider.setLookAndFeel(nullptr);
}

void MainComponent::OperatorComponent::setupSlider(juce::Slider& slider, const double min, const double max, const double step, const double value)
{
    slider.setRange(min, max, step);
    slider.setValue(value, juce::dontSendNotification);
    slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 46, 18);
    slider.setLookAndFeel(&dx21LookAndFeel);
    slider.addListener(this);
}

void MainComponent::OperatorComponent::addLabeledSlider(juce::Label& label, juce::Slider& slider, const juce::String& text)
{
    label.setText(text, juce::dontSendNotification);
    label.setJustificationType(juce::Justification::centred);
    label.setColour(juce::Label::textColourId, kTextMuted);
    addAndMakeVisible(label);
    addAndMakeVisible(slider);
}

void MainComponent::OperatorComponent::setOperator(const dx21::Dx21Operator& newOperator)
{
    op = newOperator;
    opSliders[0].setValue(op.ratioIndex, juce::dontSendNotification);
    opSliders[1].setValue(op.detune, juce::dontSendNotification);
    opSliders[2].setValue(op.level, juce::dontSendNotification);
    opSliders[3].setValue(op.rateScale, juce::dontSendNotification);
    opSliders[4].setValue(op.levelScale, juce::dontSendNotification);
    opSliders[5].setValue(op.velocity, juce::dontSendNotification);
    egSliders[0].setValue(op.envelope.attackRate, juce::dontSendNotification);
    egSliders[1].setValue(op.envelope.decay1Rate, juce::dontSendNotification);
    egSliders[2].setValue(op.envelope.decay1Level, juce::dontSendNotification);
    egSliders[3].setValue(op.envelope.decay2Rate, juce::dontSendNotification);
    egSliders[4].setValue(op.envelope.releaseRate, juce::dontSendNotification);
    enableButton.setToggleState(op.enabled, juce::dontSendNotification);
    updateEnableButtonStyle();
    repaint();
}

void MainComponent::OperatorComponent::setRole(juce::String newRole)
{
    role = std::move(newRole);
    roleLabel.setText(role, juce::dontSendNotification);
    repaint();
}

void MainComponent::OperatorComponent::paint(juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat().reduced(4.0f);
    g.setGradientFill(juce::ColourGradient(kPanelTop,
                                           area.getX(),
                                           area.getY(),
                                           kPanelBottom,
                                           area.getX(),
                                           area.getBottom(),
                                           false));
    g.fillRoundedRectangle(area, 6.0f);
    g.setColour(op.enabled ? kPanelBorderOn.withAlpha(0.58f) : kPanelBorder);
    g.drawRoundedRectangle(area, 6.0f, op.enabled ? 1.4f : 1.0f);

    auto graph = getLocalBounds().reduced(10).withTrimmedTop(36).removeFromTop(46).toFloat();
    g.setColour(kControlWell);
    g.fillRoundedRectangle(graph, 4.0f);
    const float left = graph.getX() + 8.0f;
    const float right = graph.getRight() - 8.0f;
    const float top = graph.getY() + 8.0f;
    const float bottom = graph.getBottom() - 8.0f;
    const float usableWidth = right - left;
    const float attackWeight = egTimeWeight(op.envelope.attackRate, 31);
    const float decay1Weight = egTimeWeight(op.envelope.decay1Rate, 31);
    const float decay2Weight = egTimeWeight(op.envelope.decay2Rate, 31);
    const float releaseWeight = egTimeWeight(op.envelope.releaseRate, 15);
    const float totalWeight = attackWeight + decay1Weight + decay2Weight + releaseWeight;
    const float x1 = left + usableWidth * attackWeight / totalWeight;
    const float x2 = x1 + usableWidth * decay1Weight / totalWeight;
    const float x3 = x2 + usableWidth * decay2Weight / totalWeight;
    const float sustain = static_cast<float>(juce::jlimit(0, 15, op.envelope.decay1Level)) / 15.0f;
    const float y2 = bottom - sustain * (bottom - top);
    const float y3 = op.envelope.decay2Rate > 0 ? bottom : y2;
    juce::Path path;
    path.startNewSubPath(left, bottom);
    path.lineTo(x1, top);
    path.lineTo(x2, y2);
    path.lineTo(x3, y3);
    path.lineTo(right, bottom);
    g.setColour(kControlBorder);
    g.drawVerticalLine(static_cast<int>(std::round(x1)), top, bottom);
    g.drawVerticalLine(static_cast<int>(std::round(x2)), top, bottom);
    g.drawVerticalLine(static_cast<int>(std::round(x3)), top, bottom);
    g.setColour(op.enabled ? kEnvelope : juce::Colour(0xff6f6a5b));
    g.strokePath(path, juce::PathStrokeType(2.0f));

    g.setColour(kTextMuted);
    g.setFont(juce::FontOptions(10.0f, juce::Font::plain));
    g.drawText("AR", juce::Rectangle<float>(left, graph.getY(), x1 - left, 12.0f), juce::Justification::centred);
    g.drawText("D1", juce::Rectangle<float>(x1, graph.getY(), x2 - x1, 12.0f), juce::Justification::centred);
    g.drawText("D2", juce::Rectangle<float>(x2, graph.getY(), x3 - x2, 12.0f), juce::Justification::centred);
    g.drawText("RR", juce::Rectangle<float>(x3, graph.getY(), right - x3, 12.0f), juce::Justification::centred);
}

void MainComponent::OperatorComponent::resized()
{
    auto area = getLocalBounds().reduced(8);
    auto top = area.removeFromTop(24);
    enableButton.setBounds(top.removeFromLeft(78));
    top.removeFromLeft(5);
    roleLabel.setBounds(top);
    area.removeFromTop(52);
    auto egRow = area.removeFromTop(74);
    const int egWidth = juce::jmax(42, egRow.getWidth() / 5);
    for (int i = 0; i < 5; ++i)
    {
        auto cell = egRow.removeFromLeft(egWidth).reduced(1);
        egLabels[static_cast<std::size_t>(i)].setBounds(cell.removeFromTop(15));
        egSliders[static_cast<std::size_t>(i)].setBounds(cell);
    }
    auto opRow = area.removeFromTop(74);
    const int opWidth = juce::jmax(42, opRow.getWidth() / 6);
    for (int i = 0; i < 6; ++i)
    {
        auto cell = opRow.removeFromLeft(opWidth).reduced(1);
        opLabels[static_cast<std::size_t>(i)].setBounds(cell.removeFromTop(15));
        opSliders[static_cast<std::size_t>(i)].setBounds(cell);
    }
}

void MainComponent::OperatorComponent::sliderValueChanged(juce::Slider*)
{
    op.ratioIndex = static_cast<int>(opSliders[0].getValue());
    op.detune = static_cast<int>(opSliders[1].getValue());
    op.level = static_cast<int>(opSliders[2].getValue());
    op.rateScale = static_cast<int>(opSliders[3].getValue());
    op.levelScale = static_cast<int>(opSliders[4].getValue());
    op.velocity = static_cast<int>(opSliders[5].getValue());
    op.envelope.attackRate = static_cast<int>(egSliders[0].getValue());
    op.envelope.decay1Rate = static_cast<int>(egSliders[1].getValue());
    op.envelope.decay1Level = static_cast<int>(egSliders[2].getValue());
    op.envelope.decay2Rate = static_cast<int>(egSliders[3].getValue());
    op.envelope.releaseRate = static_cast<int>(egSliders[4].getValue());
    notify();
}

void MainComponent::OperatorComponent::buttonClicked(juce::Button*)
{
    op.enabled = !op.enabled;
    enableButton.setToggleState(op.enabled, juce::dontSendNotification);
    updateEnableButtonStyle();
    notify();
}

void MainComponent::OperatorComponent::updateEnableButtonStyle()
{
    enableButton.setButtonText("OP " + juce::String(opIndex + 1));
    enableButton.setColour(juce::TextButton::buttonColourId, op.enabled ? juce::Colour(0xff0aa878) : juce::Colour(0xff2b2a24));
    enableButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff0aa878));
    enableButton.setColour(juce::TextButton::textColourOffId, op.enabled ? juce::Colours::white : kTextMuted);
    enableButton.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
}

void MainComponent::OperatorComponent::notify()
{
    repaint();
    if (onChange)
        onChange(opIndex, op);
}

MainComponent::KeyboardComponent::KeyboardComponent(MainComponent& ownerIn)
    : owner(ownerIn)
{
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
}

void MainComponent::KeyboardComponent::paint(juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat();
    g.fillAll(juce::Colour(0xff151515));

    const float keyWidth = area.getWidth() / static_cast<float>(kKeyboardNoteCount);
    for (int i = 0; i < kKeyboardNoteCount; ++i)
    {
        const int note = kFirstKeyboardNote + i;
        const auto keyArea = juce::Rectangle<float>(area.getX() + keyWidth * i, area.getY(), keyWidth - 1.0f, area.getHeight());
        const bool black = isBlackKey(note);
        const bool held = note == heldNote || owner.isMidiUiNoteHeld(note);
        g.setColour(held ? juce::Colour(0xff6fd4ff) : (black ? juce::Colour(0xff202020) : juce::Colour(0xffefefef)));
        g.fillRoundedRectangle(keyArea.reduced(1.0f), 3.0f);
        g.setColour(black ? juce::Colour(0xff050505) : juce::Colour(0xff303030));
        g.drawRoundedRectangle(keyArea.reduced(1.0f), 3.0f, 1.0f);
    }
}

int MainComponent::KeyboardComponent::noteForPosition(const juce::Point<int> position) const
{
    const int width = juce::jmax(1, getWidth());
    const int index = juce::jlimit(0, kKeyboardNoteCount - 1, position.x * kKeyboardNoteCount / width);
    return kFirstKeyboardNote + index;
}

void MainComponent::KeyboardComponent::updateHeldNote(const int note)
{
    if (heldNote == note)
        return;

    if (heldNote >= 0)
        owner.noteOff(heldNote);

    heldNote = note;
    if (heldNote >= 0)
        owner.noteOn(heldNote, 104);

    repaint();
}

void MainComponent::KeyboardComponent::mouseDown(const juce::MouseEvent& event)
{
    updateHeldNote(noteForPosition(event.getPosition()));
}

void MainComponent::KeyboardComponent::mouseDrag(const juce::MouseEvent& event)
{
    updateHeldNote(noteForPosition(event.getPosition()));
}

void MainComponent::KeyboardComponent::mouseUp(const juce::MouseEvent&)
{
    updateHeldNote(-1);
}

void MainComponent::KeyboardComponent::mouseExit(const juce::MouseEvent&)
{
    updateHeldNote(-1);
}

MainComponent::MainComponent()
    : keyboard(*this)
{
    setupLabel(titleLabel, "DX21 Web Synth");
    titleLabel.setFont(juce::FontOptions(22.0f, juce::Font::bold));
    addAndMakeVisible(titleLabel);

    statusLabel.setJustificationType(juce::Justification::centredLeft);
    statusLabel.setColour(juce::Label::textColourId, kTextMuted);
    addAndMakeVisible(statusLabel);

    setupComboBox(voiceSelect);
    voiceSelect.addListener(this);
    addAndMakeVisible(voiceSelect);

    setupComboBox(audioOutputSelect);
    audioOutputSelect.addListener(this);
    addAndMakeVisible(audioOutputSelect);

    setupComboBox(midiInputSelect);
    midiInputSelect.addListener(this);
    addAndMakeVisible(midiInputSelect);

    powerButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff172b27));
    powerButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff0aa878));
    powerButton.setColour(juce::TextButton::textColourOffId, kTextPrimary);
    powerButton.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
    powerButton.addListener(this);
    addAndMakeVisible(powerButton);

    setupLabel(volumeLabel, "Volume");
    addAndMakeVisible(volumeLabel);
    setupSlider(volumeSlider, 0.0, 1.0, 0.01, masterVolume, juce::Slider::LinearVertical);
    volumeSlider.addListener(this);
    addAndMakeVisible(volumeSlider);

    setupLabel(transposeLabel, "Transpose");
    addAndMakeVisible(transposeLabel);
    setupSlider(transposeSlider, -24.0, 24.0, 1.0, 0.0, juce::Slider::LinearVertical);
    transposeSlider.addListener(this);
    addAndMakeVisible(transposeSlider);

    setupLabel(algorithmLabel, "Algorithm");
    setupSlider(algorithmSlider, 1, 8, 1, 1, juce::Slider::RotaryHorizontalVerticalDrag);
    algorithmSlider.addListener(this);
    addAndMakeVisible(algorithmLabel);
    addAndMakeVisible(algorithmSlider);

    setupLabel(feedbackLabel, "Feedback");
    setupSlider(feedbackSlider, 0, 7, 1, 2, juce::Slider::RotaryHorizontalVerticalDrag);
    feedbackSlider.addListener(this);
    addAndMakeVisible(feedbackLabel);
    addAndMakeVisible(feedbackSlider);
    addAndMakeVisible(algorithmView);
    addAndMakeVisible(lcd);
    addAndMakeVisible(scope);

    setupLabel(lfoWaveLabel, "LFO Wave");
    lfoWaveSelect.addItem("SAW UP", 1);
    lfoWaveSelect.addItem("SQUARE", 2);
    lfoWaveSelect.addItem("TRIANGLE", 3);
    lfoWaveSelect.addItem("S/H", 4);
    setupComboBox(lfoWaveSelect);
    lfoWaveSelect.addListener(this);
    addAndMakeVisible(lfoWaveLabel);
    addAndMakeVisible(lfoWaveSelect);
    lfoSyncButton.setColour(juce::ToggleButton::textColourId, kTextMuted);
    lfoSyncButton.setColour(juce::ToggleButton::tickColourId, kTeal);
    lfoSyncButton.setColour(juce::ToggleButton::tickDisabledColourId, juce::Colour(0xff555044));
    lfoSyncButton.addListener(this);
    addAndMakeVisible(lfoSyncButton);

    setupLabel(lfoSpeedLabel, "Speed");
    setupLabel(lfoDelayLabel, "Delay");
    setupLabel(lfoPitchDepthLabel, "PMD");
    setupLabel(lfoAmpDepthLabel, "AMD");
    setupLabel(lfoPitchSensitivityLabel, "PMS");
    setupLabel(lfoAmpSensitivityLabel, "AMS");
    setupSlider(lfoSpeedSlider, 0, 99, 1, 24, juce::Slider::RotaryHorizontalVerticalDrag);
    setupSlider(lfoDelaySlider, 0, 99, 1, 0, juce::Slider::RotaryHorizontalVerticalDrag);
    setupSlider(lfoPitchDepthSlider, 0, 99, 1, 0, juce::Slider::RotaryHorizontalVerticalDrag);
    setupSlider(lfoAmpDepthSlider, 0, 99, 1, 0, juce::Slider::RotaryHorizontalVerticalDrag);
    setupSlider(lfoPitchSensitivitySlider, 0, 7, 1, 3, juce::Slider::RotaryHorizontalVerticalDrag);
    setupSlider(lfoAmpSensitivitySlider, 0, 3, 1, 0, juce::Slider::RotaryHorizontalVerticalDrag);
    for (auto* slider : { &lfoSpeedSlider, &lfoDelaySlider, &lfoPitchDepthSlider, &lfoAmpDepthSlider,
                          &lfoPitchSensitivitySlider, &lfoAmpSensitivitySlider })
    {
        slider->addListener(this);
        addAndMakeVisible(*slider);
    }
    for (auto* label : { &lfoSpeedLabel, &lfoDelayLabel, &lfoPitchDepthLabel, &lfoAmpDepthLabel,
                         &lfoPitchSensitivityLabel, &lfoAmpSensitivityLabel })
        addAndMakeVisible(*label);

    setupLabel(effectReverbLabel, "Reverb");
    setupLabel(effectMixLabel, "Mix");
    setupLabel(effectToneLabel, "Tone");
    setupLabel(effectChorusLabel, "Chorus");
    setupLabel(effectDelayLabel, "Delay");
    setupSlider(effectReverbSlider, 0, 99, 1, 0, juce::Slider::RotaryHorizontalVerticalDrag);
    setupSlider(effectMixSlider, 0, 99, 1, 0, juce::Slider::RotaryHorizontalVerticalDrag);
    setupSlider(effectToneSlider, 0, 99, 1, 50, juce::Slider::RotaryHorizontalVerticalDrag);
    setupSlider(effectChorusSlider, 0, 99, 1, 0, juce::Slider::RotaryHorizontalVerticalDrag);
    setupSlider(effectDelaySlider, 0, 99, 1, 0, juce::Slider::RotaryHorizontalVerticalDrag);
    for (auto* slider : { &effectReverbSlider, &effectMixSlider, &effectToneSlider, &effectChorusSlider, &effectDelaySlider })
    {
        slider->addListener(this);
        addAndMakeVisible(*slider);
    }
    for (auto* label : { &effectReverbLabel, &effectMixLabel, &effectToneLabel, &effectChorusLabel, &effectDelayLabel })
        addAndMakeVisible(*label);

    setupLabel(pitchWheelLabel, "Pitch");
    setupSlider(pitchWheelSlider, -1.0, 1.0, 0.01, 0.0, juce::Slider::LinearVertical);
    pitchWheelSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    pitchWheelSlider.setScrollWheelEnabled(false);
    pitchWheelSlider.setDoubleClickReturnValue(true, 0.0);
    pitchWheelSlider.setLookAndFeel(&dx21LookAndFeel);
    pitchWheelSlider.addListener(this);
    addAndMakeVisible(pitchWheelLabel);
    addAndMakeVisible(pitchWheelSlider);
    setupLabel(modWheelLabel, "Mod");
    setupSlider(modWheelSlider, 0.0, 1.0, 0.01, 0.0, juce::Slider::LinearVertical);
    modWheelSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    modWheelSlider.setScrollWheelEnabled(false);
    modWheelSlider.setDoubleClickReturnValue(true, 0.0);
    modWheelSlider.setLookAndFeel(&dx21LookAndFeel);
    modWheelSlider.addListener(this);
    addAndMakeVisible(modWheelLabel);
    addAndMakeVisible(modWheelSlider);

    addAndMakeVisible(keyboard);

    for (int i = 0; i < dx21::kOperatorCount; ++i)
    {
        operatorPanels[static_cast<std::size_t>(i)] = std::make_unique<OperatorComponent>(
            i,
            [this](const int index, const dx21::Dx21Operator& op)
            {
                currentPatch.operators[static_cast<std::size_t>(index)] = op;
                applyPatchToEngine();
            });
        addAndMakeVisible(*operatorPanels[static_cast<std::size_t>(i)]);
    }

    loadFactoryVoices();
    applySelectedVoice();
    populateAudioOutputSelect();
    populateMidiInputSelect();
    midiStatus = "MIDI: off";
    refreshStatus();

    setSize(1280, 760);
}

MainComponent::~MainComponent()
{
    for (auto& input : midiInputs)
    {
        if (input)
            input->stop();
    }
    midiInputs.clear();

    for (auto* slider : { &volumeSlider, &transposeSlider, &algorithmSlider, &feedbackSlider,
                          &lfoSpeedSlider, &lfoDelaySlider, &lfoPitchDepthSlider, &lfoAmpDepthSlider,
                          &lfoPitchSensitivitySlider, &lfoAmpSensitivitySlider,
                          &effectReverbSlider, &effectMixSlider, &effectToneSlider, &effectChorusSlider,
                          &effectDelaySlider, &pitchWheelSlider, &modWheelSlider })
    {
        slider->setLookAndFeel(nullptr);
    }

    if (audioStarted)
    {
        audioSourcePlayer.setSource(nullptr);
        if (audioDeviceManager)
        {
            audioDeviceManager->removeAudioCallback(&audioSourcePlayer);
            audioDeviceManager->closeAudioDevice();
        }
    }
}

void MainComponent::prepareToPlay(int, const double sampleRate)
{
    std::lock_guard<std::mutex> lock(engineMutex);
    engine.prepare(sampleRate);
    engine.setPatch(currentPatch);
}

void MainComponent::getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill)
{
    bufferToFill.clearActiveBufferRegion();
    if (!powerOn)
        return;

    auto* left = bufferToFill.buffer->getWritePointer(0, bufferToFill.startSample);
    auto* right = bufferToFill.buffer->getNumChannels() > 1
        ? bufferToFill.buffer->getWritePointer(1, bufferToFill.startSample)
        : left;

    std::lock_guard<std::mutex> lock(engineMutex);
    for (int i = 0; i < bufferToFill.numSamples; ++i)
    {
        const auto sample = engine.renderSample();
        left[i] = sample.left * masterVolume;
        right[i] = sample.right * masterVolume;
        if ((i & 7) == 0)
            scope.pushSample(left[i]);
    }
}

void MainComponent::releaseResources() {}

void MainComponent::paint(juce::Graphics& g)
{
    g.fillAll(kAppBackground);
    auto area = getLocalBounds().reduced(12).toFloat();
    g.setGradientFill(juce::ColourGradient(kPanelTop,
                                           area.getX(),
                                           area.getY(),
                                           kPanelBottom,
                                           area.getX(),
                                           area.getBottom(),
                                           false));
    g.fillRoundedRectangle(area, 6.0f);
    g.setColour(kPanelBorder);
    g.drawRoundedRectangle(area, 6.0f, 1.0f);
}

void MainComponent::resized()
{
    auto area = getLocalBounds().reduced(20);
    auto header = area.removeFromTop(34);
    titleLabel.setBounds(header.removeFromLeft(210));
    powerButton.setBounds(header.removeFromRight(82));
    header.removeFromRight(10);
    midiInputSelect.setBounds(header.removeFromRight(180));
    header.removeFromRight(8);
    audioOutputSelect.setBounds(header.removeFromRight(235));
    header.removeFromRight(12);
    statusLabel.setBounds(header);

    area.removeFromTop(10);
    auto top = area.removeFromTop(238);
    auto patch = top.removeFromLeft(342).reduced(4);
    voiceSelect.setBounds(patch.removeFromTop(30));
    patch.removeFromTop(6);
    auto powerRow = patch.removeFromTop(32);
    lcd.setBounds(powerRow.withHeight(62).withWidth(218));
    patch.removeFromTop(8);
    scope.setBounds(patch.removeFromTop(54).reduced(0, 3));
    patch.removeFromTop(16);
    auto faders = patch.removeFromTop(112);
    volumeLabel.setBounds(faders.removeFromLeft(74).removeFromTop(20));
    volumeSlider.setBounds(patch.getX() + 22, patch.getY() + 105, 58, 104);
    transposeLabel.setBounds(patch.getX() + 92, patch.getY() + 85, 88, 20);
    transposeSlider.setBounds(patch.getX() + 110, patch.getY() + 105, 58, 104);

    auto voice = top.removeFromLeft(260).reduced(4);
    algorithmLabel.setBounds(voice.getX(), voice.getY(), 90, 20);
    algorithmSlider.setBounds(voice.getX(), voice.getY() + 22, 82, 72);
    feedbackLabel.setBounds(voice.getX() + 92, voice.getY(), 90, 20);
    feedbackSlider.setBounds(voice.getX() + 92, voice.getY() + 22, 82, 72);
    algorithmView.setBounds(voice.getX(), voice.getY() + 102, voice.getWidth(), 126);

    auto lfo = top.removeFromLeft(330).reduced(4);
    lfoWaveLabel.setBounds(lfo.removeFromTop(20));
    auto lfoTop = lfo.removeFromTop(30);
    lfoWaveSelect.setBounds(lfoTop.removeFromLeft(142));
    lfoTop.removeFromLeft(8);
    lfoSyncButton.setBounds(lfoTop.removeFromLeft(74));
    auto lfoGrid = lfo.withTrimmedTop(8);
    std::array<juce::Label*, 6> lfoLabels { &lfoSpeedLabel, &lfoDelayLabel, &lfoPitchDepthLabel, &lfoAmpDepthLabel, &lfoPitchSensitivityLabel, &lfoAmpSensitivityLabel };
    std::array<juce::Slider*, 6> lfoSliders { &lfoSpeedSlider, &lfoDelaySlider, &lfoPitchDepthSlider, &lfoAmpDepthSlider, &lfoPitchSensitivitySlider, &lfoAmpSensitivitySlider };
    for (int row = 0; row < 2; ++row)
    {
        auto rowArea = lfoGrid.removeFromTop(84);
        for (int col = 0; col < 3; ++col)
        {
            const int index = row * 3 + col;
            auto cell = rowArea.removeFromLeft(102).reduced(3);
            lfoLabels[static_cast<std::size_t>(index)]->setBounds(cell.removeFromTop(18));
            lfoSliders[static_cast<std::size_t>(index)]->setBounds(cell);
        }
    }

    auto effects = top.reduced(4);
    std::array<juce::Label*, 5> effectLabels { &effectReverbLabel, &effectMixLabel, &effectToneLabel, &effectChorusLabel, &effectDelayLabel };
    std::array<juce::Slider*, 5> effectSliders { &effectReverbSlider, &effectMixSlider, &effectToneSlider, &effectChorusSlider, &effectDelaySlider };
    effects.removeFromTop(16);
    for (int i = 0; i < 5; ++i)
    {
        auto cell = effects.removeFromLeft(74).reduced(2);
        effectLabels[static_cast<std::size_t>(i)]->setBounds(cell.removeFromTop(18));
        effectSliders[static_cast<std::size_t>(i)]->setBounds(cell.removeFromTop(74));
    }

    area.removeFromTop(12);
    auto middle = area.removeFromTop(146);
    auto pitchArea = middle.removeFromLeft(74).reduced(4);
    pitchWheelLabel.setBounds(pitchArea.removeFromTop(18));
    pitchWheelSlider.setBounds(pitchArea);
    auto modArea = middle.removeFromLeft(74).reduced(4);
    modWheelLabel.setBounds(modArea.removeFromTop(18));
    modWheelSlider.setBounds(modArea);
    middle.removeFromLeft(10);
    keyboard.setBounds(middle.reduced(2));

    area.removeFromTop(10);
    auto ops = area;
    const int panelWidth = ops.getWidth() / dx21::kOperatorCount;
    const int panelHeight = juce::jmin(ops.getHeight(), 246);
    for (int i = 0; i < dx21::kOperatorCount; ++i)
    {
        operatorPanels[static_cast<std::size_t>(i)]->setBounds(ops.getX() + i * panelWidth,
                                                               ops.getY(),
                                                               panelWidth - 8,
                                                               panelHeight);
    }
}

void MainComponent::loadFactoryVoices()
{
    factoryVoices.clear();

#ifdef DX21_ASSET_DIR
    const auto syxFile = juce::File(juce::String(DX21_ASSET_DIR)).getChildFile("DX21.syx");
    const auto bytes = readBinaryFile(syxFile);
    if (!bytes.empty())
    {
        try
        {
            const auto presets = dx21::parseDx21BulkVmem(bytes);
            for (const auto& preset : presets)
                factoryVoices.push_back(dx21::withVmemPreset(dx21::Dx21Patch {}, preset));
        }
        catch (const std::exception&)
        {
            factoryVoices.clear();
        }
    }
#endif

    if (factoryVoices.empty())
        factoryVoices.push_back(dx21::Dx21PatchWithMetadata { dx21::Dx21Patch {}, "Init Voice", false, {} });

    voiceSelect.clear(juce::dontSendNotification);
    for (int i = 0; i < static_cast<int>(factoryVoices.size()); ++i)
        voiceSelect.addItem(displayNameForVoice(i, factoryVoices[static_cast<std::size_t>(i)]), i + 1);

    voiceSelect.setSelectedId(1, juce::dontSendNotification);
}

void MainComponent::applySelectedVoice()
{
    const int index = juce::jlimit(0, static_cast<int>(factoryVoices.size()) - 1, voiceSelect.getSelectedId() - 1);
    currentPatch = factoryVoices[static_cast<std::size_t>(index)].patch;
    currentPatch.transpose = static_cast<int>(transposeSlider.getValue());

    syncUiFromPatch();
    applyPatchToEngine();
}

void MainComponent::syncUiFromPatch()
{
    syncingUi = true;
    algorithmSlider.setValue(currentPatch.algorithm, juce::dontSendNotification);
    feedbackSlider.setValue(currentPatch.feedback, juce::dontSendNotification);
    transposeSlider.setValue(currentPatch.transpose, juce::dontSendNotification);
    lfoWaveSelect.setSelectedId(juce::jlimit(0, 3, currentPatch.lfo.wave) + 1, juce::dontSendNotification);
    lfoSyncButton.setToggleState(currentPatch.lfo.sync, juce::dontSendNotification);
    lfoSpeedSlider.setValue(currentPatch.lfo.speed, juce::dontSendNotification);
    lfoDelaySlider.setValue(currentPatch.lfo.delay, juce::dontSendNotification);
    lfoPitchDepthSlider.setValue(currentPatch.lfo.pitchDepth, juce::dontSendNotification);
    lfoAmpDepthSlider.setValue(currentPatch.lfo.ampDepth, juce::dontSendNotification);
    lfoPitchSensitivitySlider.setValue(currentPatch.lfo.pitchSensitivity, juce::dontSendNotification);
    lfoAmpSensitivitySlider.setValue(currentPatch.lfo.ampSensitivity, juce::dontSendNotification);
    effectReverbSlider.setValue(currentPatch.effects.reverb, juce::dontSendNotification);
    effectMixSlider.setValue(currentPatch.effects.mix, juce::dontSendNotification);
    effectToneSlider.setValue(currentPatch.effects.tone, juce::dontSendNotification);
    effectChorusSlider.setValue(currentPatch.effects.chorus, juce::dontSendNotification);
    effectDelaySlider.setValue(currentPatch.effects.delay, juce::dontSendNotification);
    syncingUi = false;

    const int presetIndex = juce::jmax(0, voiceSelect.getSelectedId() - 1);
    const auto bank = presetIndex < 16 ? "A" : "B";
    const auto number = juce::String(presetIndex % 16 + 1).paddedLeft('0', 2);
    const auto name = factoryVoices.empty() ? juce::String("INIT VOICE")
                                            : juce::String(factoryVoices[static_cast<std::size_t>(presetIndex)].name);
    lcd.setLines("PLAY SINGLE", bank + number + " " + name);
    refreshAlgorithmAndRoles();
}

void MainComponent::updatePatchFromGlobalControls()
{
    currentPatch.algorithm = static_cast<int>(algorithmSlider.getValue());
    currentPatch.feedback = static_cast<int>(feedbackSlider.getValue());
    currentPatch.transpose = static_cast<int>(transposeSlider.getValue());
    currentPatch.lfo.wave = juce::jlimit(0, 3, lfoWaveSelect.getSelectedId() - 1);
    currentPatch.lfo.sync = lfoSyncButton.getToggleState();
    currentPatch.lfo.speed = static_cast<int>(lfoSpeedSlider.getValue());
    currentPatch.lfo.delay = static_cast<int>(lfoDelaySlider.getValue());
    currentPatch.lfo.pitchDepth = static_cast<int>(lfoPitchDepthSlider.getValue());
    currentPatch.lfo.ampDepth = static_cast<int>(lfoAmpDepthSlider.getValue());
    currentPatch.lfo.pitchSensitivity = static_cast<int>(lfoPitchSensitivitySlider.getValue());
    currentPatch.lfo.ampSensitivity = static_cast<int>(lfoAmpSensitivitySlider.getValue());
    currentPatch.effects.reverb = static_cast<int>(effectReverbSlider.getValue());
    currentPatch.effects.mix = static_cast<int>(effectMixSlider.getValue());
    currentPatch.effects.tone = static_cast<int>(effectToneSlider.getValue());
    currentPatch.effects.chorus = static_cast<int>(effectChorusSlider.getValue());
    currentPatch.effects.delay = static_cast<int>(effectDelaySlider.getValue());
    refreshAlgorithmAndRoles();
}

void MainComponent::applyPatchToEngine()
{
    std::lock_guard<std::mutex> lock(engineMutex);
    engine.setPatch(currentPatch);
}

void MainComponent::setupSlider(juce::Slider& slider,
                                const double min,
                                const double max,
                                const double step,
                                const double value,
                                const juce::Slider::SliderStyle style)
{
    slider.setRange(min, max, step);
    slider.setValue(value, juce::dontSendNotification);
    slider.setSliderStyle(style);
    slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 54, 18);
    slider.setMouseDragSensitivity(130);
    if (style == juce::Slider::RotaryHorizontalVerticalDrag)
        slider.setLookAndFeel(&dx21LookAndFeel);
}

void MainComponent::setupLabel(juce::Label& label, const juce::String& text)
{
    label.setText(text, juce::dontSendNotification);
    label.setJustificationType(juce::Justification::centredLeft);
    label.setColour(juce::Label::textColourId, kTextPrimary);
}

void MainComponent::setupComboBox(juce::ComboBox& comboBox)
{
    comboBox.setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff1c1a15));
    comboBox.setColour(juce::ComboBox::textColourId, kTextPrimary);
    comboBox.setColour(juce::ComboBox::outlineColourId, juce::Colour(0xff3a3529));
    comboBox.setColour(juce::ComboBox::arrowColourId, kTextMuted);
}

void MainComponent::populateAudioOutputSelect()
{
    audioOutputChoices.clear();
    audioOutputSelect.clear(juce::dontSendNotification);
    audioOutputSelect.addItem("Audio: Default", 1);

    juce::OwnedArray<juce::AudioIODeviceType> types;
    juce::AudioDeviceManager tempManager;
    tempManager.createAudioDeviceTypes(types);

    int id = 2;
    for (auto* type : types)
    {
        if (type == nullptr)
            continue;

        type->scanForDevices();
        const auto names = type->getDeviceNames(false);
        for (const auto& name : names)
        {
            audioOutputChoices.push_back({ type->getTypeName(), name });
            audioOutputSelect.addItem("Audio: " + type->getTypeName() + " - " + name, id++);
        }
    }

    audioOutputSelect.setSelectedId(1, juce::dontSendNotification);
}

void MainComponent::populateMidiInputSelect()
{
    midiInputDevices = juce::MidiInput::getAvailableDevices();
    midiInputSelect.clear(juce::dontSendNotification);
    midiInputSelect.addItem("MIDI: Off", 1);
    midiInputSelect.addItem("MIDI: All Inputs", 2);

    int id = 3;
    for (const auto& device : midiInputDevices)
        midiInputSelect.addItem("MIDI: " + device.name, id++);

    if (midiInputDevices.isEmpty())
        midiInputSelect.setText("MIDI: No Input", juce::dontSendNotification);
    else
        midiInputSelect.setSelectedId(1, juce::dontSendNotification);
}

void MainComponent::refreshAlgorithmAndRoles()
{
    algorithmView.setAlgorithm(currentPatch.algorithm, currentPatch.feedback);
    for (int i = 0; i < dx21::kOperatorCount; ++i)
    {
        auto& panel = operatorPanels[static_cast<std::size_t>(i)];
        if (panel)
        {
            panel->setOperator(currentPatch.operators[static_cast<std::size_t>(i)]);
            panel->setRole(operatorRole(currentPatch, i));
        }
    }
}

void MainComponent::refreshStatus()
{
    statusLabel.setText(midiStatus + "   Voices: " + juce::String(factoryVoices.size())
                            + "   LFO: " + lfoWaveName(currentPatch.lfo.wave),
                        juce::dontSendNotification);
}

bool MainComponent::ensureAudioStarted()
{
    if (audioStarted)
        return true;

    audioDeviceManager = std::make_unique<juce::AudioDeviceManager>();
    juce::AudioDeviceManager::AudioDeviceSetup setup;
    const int outputId = audioOutputSelect.getSelectedId();
    if (outputId >= 2)
    {
        const auto index = static_cast<std::size_t>(outputId - 2);
        if (index < audioOutputChoices.size())
        {
            const auto& choice = audioOutputChoices[index];
            audioDeviceManager->setCurrentAudioDeviceType(choice.typeName, true);
            setup.outputDeviceName = choice.deviceName;
        }
    }
   #if JUCE_WINDOWS
    else
    {
        audioDeviceManager->setCurrentAudioDeviceType("DirectSound", true);
    }
   #endif

    setup.inputDeviceName = {};
    const auto error = audioDeviceManager->initialise(0, 2, nullptr, true, {}, outputId >= 2 ? &setup : nullptr);
    if (error.isNotEmpty())
    {
        midiStatus = "Audio: " + error;
        refreshStatus();
        audioDeviceManager = nullptr;
        return false;
    }

    audioDeviceManager->addAudioCallback(&audioSourcePlayer);
    audioSourcePlayer.setSource(this);
    audioStarted = true;
    return true;
}

bool MainComponent::startPlayback()
{
    if (powerOn)
        return true;

    if (!ensureAudioStarted())
        return false;

    powerOn = true;
    powerButton.setButtonText("ON");
    powerButton.setToggleState(true, juce::dontSendNotification);
    powerButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff0aa878));
    return true;
}

void MainComponent::restartAudioOutput()
{
    if (!audioStarted)
        return;

    const bool shouldResume = powerOn;
    allNotesOff();
    powerOn = false;
    audioSourcePlayer.setSource(nullptr);

    if (audioDeviceManager)
    {
        audioDeviceManager->removeAudioCallback(&audioSourcePlayer);
        audioDeviceManager->closeAudioDevice();
    }

    audioStarted = false;
    audioDeviceManager = nullptr;

    if (shouldResume)
        startPlayback();
}

void MainComponent::connectMidiInputs()
{
    for (auto& input : midiInputs)
    {
        if (input)
            input->stop();
    }
    midiInputs.clear();

    midiInputDevices = juce::MidiInput::getAvailableDevices();
    const auto selectedId = midiInputSelect.getSelectedId();

    if (selectedId <= 1)
    {
        midiStatus = "MIDI: off";
        return;
    }

    const auto connectDevice = [this](const juce::MidiDeviceInfo& device)
    {
        if (auto input = juce::MidiInput::openDevice(device.identifier, this))
        {
            input->start();
            midiInputs.push_back(std::move(input));
        }
    };

    if (selectedId >= 3)
    {
        const int index = selectedId - 3;
        if (juce::isPositiveAndBelow(index, midiInputDevices.size()))
            connectDevice(midiInputDevices[index]);
    }
    else
    {
        for (const auto& device : midiInputDevices)
            connectDevice(device);
    }

    if (midiInputs.empty())
    {
        midiStatus = midiInputDevices.isEmpty() ? "MIDI: no input" : "MIDI: open failed";
        return;
    }

    if (selectedId >= 3 && midiInputs.size() == 1)
        midiStatus = "MIDI: " + midiInputDevices[selectedId - 3].name;
    else
    {
        midiStatus = "MIDI: " + juce::String(static_cast<int>(midiInputs.size())) + " input";
        if (midiInputs.size() != 1)
            midiStatus += "s";
    }
}

void MainComponent::noteOn(const int note, const int velocity)
{
    if (!powerOn && !startPlayback())
        return;

    std::lock_guard<std::mutex> lock(engineMutex);
    engine.noteOn(note, velocity);
}

void MainComponent::noteOff(const int note)
{
    std::lock_guard<std::mutex> lock(engineMutex);
    engine.noteOff(note);
}

void MainComponent::allNotesOff()
{
    for (auto& held : gMidiUiHeldNotes)
        held.store(false, std::memory_order_relaxed);
    repaintKeyboardAsync();

    std::lock_guard<std::mutex> lock(engineMutex);
    engine.panic();
}

bool MainComponent::isMidiUiNoteHeld(const int note) const
{
    if (!juce::isPositiveAndBelow(note, static_cast<int>(gMidiUiHeldNotes.size())))
        return false;

    return gMidiUiHeldNotes[static_cast<std::size_t>(note)].load(std::memory_order_relaxed);
}

void MainComponent::repaintKeyboardAsync()
{
    if (juce::MessageManager::getInstance()->isThisTheMessageThread())
    {
        keyboard.repaint();
        return;
    }

    juce::MessageManager::callAsync([this]
    {
        keyboard.repaint();
    });
}

void MainComponent::handleIncomingMidiMessage(juce::MidiInput*, const juce::MidiMessage& message)
{
    if (message.isNoteOn())
    {
        const int note = message.getNoteNumber();
        const int velocity = static_cast<int>(message.getVelocity() * 127.0f);
        if (juce::isPositiveAndBelow(note, static_cast<int>(gMidiUiHeldNotes.size())))
        {
            gMidiUiHeldNotes[static_cast<std::size_t>(note)].store(true, std::memory_order_relaxed);
            repaintKeyboardAsync();
        }

        if (!powerOn)
        {
            juce::MessageManager::callAsync([this, note, velocity]
            {
                noteOn(note, velocity);
            });
            return;
        }

        noteOn(note, velocity);
    }
    else if (message.isNoteOff())
    {
        const int note = message.getNoteNumber();
        if (juce::isPositiveAndBelow(note, static_cast<int>(gMidiUiHeldNotes.size())))
        {
            gMidiUiHeldNotes[static_cast<std::size_t>(note)].store(false, std::memory_order_relaxed);
            repaintKeyboardAsync();
        }

        noteOff(note);
    }
    else if (message.isPitchWheel())
    {
        const double value = juce::jlimit(-1.0,
                                          1.0,
                                          (static_cast<double>(message.getPitchWheelValue()) - 8192.0) / 8192.0);
        std::lock_guard<std::mutex> lock(engineMutex);
        engine.setPitchBend(value);
        juce::MessageManager::callAsync([this, value]
        {
            pitchWheelSlider.setValue(value, juce::dontSendNotification);
        });
    }
    else if (message.isController() && message.getControllerNumber() == 1)
    {
        const double value = static_cast<double>(message.getControllerValue()) / 127.0;
        std::lock_guard<std::mutex> lock(engineMutex);
        engine.setModWheel(value);
        juce::MessageManager::callAsync([this, value]
        {
            modWheelSlider.setValue(value, juce::dontSendNotification);
        });
    }
    else if (message.isAllNotesOff())
    {
        allNotesOff();
    }
}

void MainComponent::comboBoxChanged(juce::ComboBox* comboBoxThatHasChanged)
{
    if (comboBoxThatHasChanged == &voiceSelect)
    {
        allNotesOff();
        applySelectedVoice();
    }
    else if (comboBoxThatHasChanged == &lfoWaveSelect && !syncingUi)
    {
        updatePatchFromGlobalControls();
        applyPatchToEngine();
        refreshStatus();
    }
    else if (comboBoxThatHasChanged == &audioOutputSelect)
    {
        restartAudioOutput();
        refreshStatus();
    }
    else if (comboBoxThatHasChanged == &midiInputSelect)
    {
        connectMidiInputs();
        refreshStatus();
    }
}

void MainComponent::buttonClicked(juce::Button* button)
{
    if (button == &powerButton)
    {
        if (!powerOn)
        {
            startPlayback();
            return;
        }

        powerOn = false;
        powerButton.setButtonText("OFF");
        powerButton.setToggleState(false, juce::dontSendNotification);
        powerButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff172b27));
        allNotesOff();
    }
    else if (button == &lfoSyncButton && !syncingUi)
    {
        updatePatchFromGlobalControls();
        applyPatchToEngine();
    }
}

void MainComponent::sliderValueChanged(juce::Slider* slider)
{
    if (slider == &volumeSlider)
    {
        masterVolume = static_cast<float>(volumeSlider.getValue());
    }
    else if (slider == &transposeSlider)
    {
        if (!syncingUi)
        {
            updatePatchFromGlobalControls();
            applyPatchToEngine();
        }
    }
    else if (slider == &pitchWheelSlider)
    {
        std::lock_guard<std::mutex> lock(engineMutex);
        engine.setPitchBend(pitchWheelSlider.getValue());
    }
    else if (slider == &modWheelSlider)
    {
        std::lock_guard<std::mutex> lock(engineMutex);
        engine.setModWheel(modWheelSlider.getValue());
    }
    else if (!syncingUi)
    {
        updatePatchFromGlobalControls();
        applyPatchToEngine();
        refreshStatus();
    }
}

void MainComponent::sliderDragEnded(juce::Slider* slider)
{
    if (slider == &pitchWheelSlider)
    {
        pitchWheelSlider.setValue(0.0, juce::sendNotificationSync);
    }
}
