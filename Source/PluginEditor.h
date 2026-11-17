#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"

namespace gabyui
{
// Paleta inspirada en el diseño de Gaby Records
namespace col
{
    const juce::Colour bgTop    { 0xff12121f };
    const juce::Colour bgBottom { 0xff07070d };
    const juce::Colour panel    { 0xff17171f };
    const juce::Colour panelHi  { 0xff22222d };
    const juce::Colour border   { 0xff2e2e3c };
    const juce::Colour purple   { 0xffa855f7 };
    const juce::Colour violet   { 0xff7c3aed };
    const juce::Colour cyan     { 0xff22d3ee };
    const juce::Colour text     { 0xffe8e8f0 };
    const juce::Colour textDim  { 0xff9a9ab0 };
}

class LookAndFeel : public juce::LookAndFeel_V4
{
public:
    LookAndFeel();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&,
                               bool highlighted, bool down) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool highlighted, bool down) override;
    juce::Label* createSliderTextBox (juce::Slider&) override;
};

// Medidor LED (verde - amarillo - rojo)
class LevelMeter : public juce::Component
{
public:
    void setLevel (float linearPeak);
    void paint (juce::Graphics&) override;
private:
    float levelDb = -60.0f;
};

// Pantalla de tono: muestra la voz original (cian) y la afinada (morado)
class PitchDisplay : public juce::Component
{
public:
    void push (float inMidi, float outMidi);
    void setScale (int key, int scale);
    void setReadout (const juce::String& note, const juce::String& cents);
    void paint (juce::Graphics&) override;
private:
    juce::String noteText { "--" }, centsText;
    static constexpr int historySize = 180;
    std::array<float, historySize> inHist {}, outHist {};
    int writeIdx = 0;
    float centreMidi = 60.0f;
    std::array<bool, 12> mask {};
    int key = 0;
};

// Teclado de una octava para elegir la tonalidad
class KeySelector : public juce::Component
{
public:
    std::function<void (int)> onKeyChosen;
    void setState (int key, const std::array<bool, 12>& mask);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
private:
    juce::Rectangle<float> keyRect (int pc) const;
    static bool isBlack (int pc);
    int selected = 0;
    std::array<bool, 12> mask {};
};

class PowerButton : public juce::ToggleButton
{
public:
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;
};

} // namespace gabyui

class GabyAutotuneEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit GabyAutotuneEditor (GabyAutotuneProcessor&);
    ~GabyAutotuneEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void setChoice (const juce::String& id, int index);
    void setupKnob (juce::Slider&, juce::Label&, const juce::String& text);
    void drawPanel (juce::Graphics&, juce::Rectangle<int>, const juce::String& title) const;

    GabyAutotuneProcessor& proc;
    gabyui::LookAndFeel lnf;

    gabyui::LevelMeter inMeter, outMeter;
    gabyui::PitchDisplay display;
    gabyui::KeySelector keySelector;
    gabyui::PowerButton powerButton;

    juce::Slider gainKnob, retuneKnob, humanizeKnob, mixKnob, volumeKnob;
    juce::Label gainLabel, retuneLabel, humanizeLabel, mixLabel, volumeLabel;

    juce::TextButton prevKey { "<" }, nextKey { ">" };
    juce::Label keyNameLabel;
    juce::OwnedArray<juce::TextButton> scaleButtons;

    using SliderAtt = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAtt = juce::AudioProcessorValueTreeState::ButtonAttachment;
    std::unique_ptr<SliderAtt> gainAtt, retuneAtt, humanizeAtt, mixAtt, volumeAtt;
    std::unique_ptr<ButtonAtt> powerAtt;

    juce::Rectangle<int> headerArea, inPanel, tunePanel, outPanel, volPanel, keyPanel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GabyAutotuneEditor)
};
