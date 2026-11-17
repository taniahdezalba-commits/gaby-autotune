#include "PluginEditor.h"

using namespace gabyui;

static juce::String u8 (const char* s) { return juce::String (juce::CharPointer_UTF8 (s)); }

static juce::String noteName (int midi)
{
    static const char* names[] = { "Do", "Do#", "Re", "Re#", "Mi", "Fa", "Fa#", "Sol", "Sol#", "La", "La#", "Si" };
    const int pc = ((midi % 12) + 12) % 12;
    return juce::String (names[pc]) + juce::String (midi / 12 - 1);
}

static std::array<bool, 12> maskFor (int key, int scale)
{
    return gaby::scaleMask (key, (gaby::Scale) juce::jlimit (0, 5, scale));
}

// =============================================================================
//  LookAndFeel
// =============================================================================
LookAndFeel::LookAndFeel()
{
    setColour (juce::Slider::textBoxTextColourId, col::textDim);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Label::textColourId, col::text);
}

void LookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                    float startAngle, float endAngle, juce::Slider&)
{
    auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (4.0f);
    const float size = juce::jmin (bounds.getWidth(), bounds.getHeight());
    auto sq = bounds.withSizeKeepingCentre (size, size);
    const auto c = sq.getCentre();
    const float r = size * 0.5f;
    const float angle = startAngle + pos * (endAngle - startAngle);

    // Pista del arco
    juce::Path track;
    track.addCentredArc (c.x, c.y, r - 3.0f, r - 3.0f, 0.0f, startAngle, endAngle, true);
    g.setColour (juce::Colour (0xff2a2a36));
    g.strokePath (track, juce::PathStrokeType (3.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Arco de valor con brillo morado
    juce::Path value;
    value.addCentredArc (c.x, c.y, r - 3.0f, r - 3.0f, 0.0f, startAngle, angle, true);
    g.setColour (col::purple.withAlpha (0.25f));
    g.strokePath (value, juce::PathStrokeType (9.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setGradientFill (juce::ColourGradient (col::violet, sq.getBottomLeft(), col::purple.brighter (0.3f), sq.getTopRight(), false));
    g.strokePath (value, juce::PathStrokeType (3.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Cuerpo de la perilla
    const float kr = r * 0.70f;
    auto knob = juce::Rectangle<float> (kr * 2.0f, kr * 2.0f).withCentre (c);

    g.setColour (juce::Colours::black.withAlpha (0.55f));
    g.fillEllipse (knob.translated (0.0f, 3.0f).expanded (2.0f));

    juce::ColourGradient body (juce::Colour (0xff4a4a56), knob.getX(), knob.getY(),
                               juce::Colour (0xff0e0e14), knob.getRight(), knob.getBottom(), false);
    g.setGradientFill (body);
    g.fillEllipse (knob);

    auto inner = knob.reduced (kr * 0.12f);
    juce::ColourGradient face (juce::Colour (0xff2c2c36), inner.getCentreX(), inner.getY(),
                               juce::Colour (0xff15151c), inner.getCentreX(), inner.getBottom(), false);
    g.setGradientFill (face);
    g.fillEllipse (inner);

    g.setColour (juce::Colours::white.withAlpha (0.08f));
    g.drawEllipse (knob, 1.0f);

    // Indicador
    juce::Path pointer;
    pointer.addRoundedRectangle (-1.5f, -kr * 0.85f, 3.0f, kr * 0.42f, 1.5f);
    pointer.applyTransform (juce::AffineTransform::rotation (angle).translated (c.x, c.y));
    g.setColour (juce::Colours::white.withAlpha (0.9f));
    g.fillPath (pointer);
}

void LookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&,
                                        bool highlighted, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (1.5f);
    const float corner = 8.0f;

    if (b.getToggleState())
    {
        g.setColour (col::purple.withAlpha (0.25f));
        g.fillRoundedRectangle (r.expanded (1.0f), corner + 1.0f);
        g.setGradientFill (juce::ColourGradient (col::violet.withAlpha (0.85f), r.getX(), r.getY(),
                                                 juce::Colour (0xff3b1d6e), r.getX(), r.getBottom(), false));
        g.fillRoundedRectangle (r, corner);
        g.setColour (col::purple.brighter (0.4f));
        g.drawRoundedRectangle (r, corner, 1.4f);
    }
    else
    {
        g.setColour (highlighted ? col::panelHi.brighter (0.1f) : col::panelHi);
        g.fillRoundedRectangle (r, corner);
        g.setColour (col::border);
        g.drawRoundedRectangle (r, corner, 1.0f);
    }

    if (down)
    {
        g.setColour (juce::Colours::white.withAlpha (0.06f));
        g.fillRoundedRectangle (r, corner);
    }
}

void LookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool)
{
    g.setColour (b.getToggleState() ? juce::Colours::white : col::text.withAlpha (0.8f));
    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    g.drawFittedText (b.getButtonText(), b.getLocalBounds().reduced (4, 2), juce::Justification::centred, 1);
}

juce::Label* LookAndFeel::createSliderTextBox (juce::Slider& s)
{
    auto* l = LookAndFeel_V4::createSliderTextBox (s);
    l->setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    l->setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);
    l->setColour (juce::Label::textColourId, col::textDim);
    l->setFont (juce::FontOptions (12.0f));
    return l;
}

// =============================================================================
//  Medidor
// =============================================================================
void LevelMeter::setLevel (float linearPeak)
{
    const float db = juce::Decibels::gainToDecibels (linearPeak, -60.0f);
    levelDb = juce::jmax (db, levelDb - 1.8f); // caída suave
    repaint();
}

void LevelMeter::paint (juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat().reduced (0.0f, 7.0f);
    auto leds = area.removeFromLeft (22.0f).reduced (2.0f, 0.0f);

    constexpr int numSeg = 16;
    const float segH = leds.getHeight() / numSeg;
    const float minDb = -48.0f;

    for (int i = 0; i < numSeg; ++i)
    {
        const float frac = (float) (i + 1) / numSeg;          // 0 = abajo, 1 = arriba
        const float segDb = minDb * (1.0f - frac);
        auto seg = juce::Rectangle<float> (leds.getX(), leds.getBottom() - (i + 1) * segH, leds.getWidth(), segH).reduced (0.0f, 1.2f);

        juce::Colour c = frac > 0.88f ? juce::Colour (0xffef4444)
                       : frac > 0.72f ? juce::Colour (0xfff59e0b)
                       : frac > 0.55f ? juce::Colour (0xffeab308)
                                      : juce::Colour (0xff22c55e);

        const bool lit = levelDb >= segDb - 48.0f / numSeg;
        if (lit)
        {
            g.setColour (c.withAlpha (0.35f));
            g.fillRoundedRectangle (seg.expanded (1.5f, 0.5f), 2.0f);
        }
        g.setColour (lit ? c : c.withAlpha (0.12f));
        g.fillRoundedRectangle (seg, 1.5f);
    }

    // Escala
    g.setColour (col::textDim);
    g.setFont (juce::FontOptions (11.0f));
    const int marks[] = { 0, -6, -12, -18, -24, -36, -48 };
    for (int m : marks)
    {
        const float yy = leds.getY() + leds.getHeight() * ((float) m / minDb);
        g.drawText (juce::String (m == 0 ? "-0" : juce::String (m)),
                    juce::Rectangle<float> (area.getX() + 4.0f, yy - 6.0f, area.getWidth() - 4.0f, 12.0f),
                    juce::Justification::centredLeft);
    }
}

// =============================================================================
//  Pantalla de tono
// =============================================================================
void PitchDisplay::push (float inMidi, float outMidi)
{
    inHist[(size_t) writeIdx] = inMidi;
    outHist[(size_t) writeIdx] = outMidi;
    writeIdx = (writeIdx + 1) % historySize;

    if (outMidi > 0.0f)
        centreMidi += (outMidi - centreMidi) * 0.08f;
    repaint();
}

void PitchDisplay::setScale (int k, int scale)
{
    key = k;
    mask = maskFor (k, scale);
}

void PitchDisplay::setReadout (const juce::String& note, const juce::String& cents)
{
    noteText = note;
    centsText = cents;
}

void PitchDisplay::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff0b0b16), r.getX(), r.getY(),
                                             juce::Colour (0xff06060c), r.getX(), r.getBottom(), false));
    g.fillRoundedRectangle (r, 10.0f);
    g.setColour (col::border);
    g.drawRoundedRectangle (r.reduced (0.5f), 10.0f, 1.0f);

    auto plot = r.reduced (12.0f, 10.0f).withTrimmedLeft (34.0f);
    const float span = 14.0f; // semitonos visibles
    const float lo = centreMidi - span * 0.5f;
    auto yOf = [&] (float midi) { return plot.getBottom() - (midi - lo) / span * plot.getHeight(); };

    // Líneas de notas
    g.setFont (juce::FontOptions (10.0f));
    for (int m = (int) std::floor (lo); m <= (int) std::ceil (lo + span); ++m)
    {
        const float yy = yOf ((float) m);
        if (yy < plot.getY() - 1.0f || yy > plot.getBottom() + 1.0f)
            continue;

        const int pc = ((m % 12) + 12) % 12;
        const bool inScale = mask[(size_t) pc];
        g.setColour (inScale ? col::purple.withAlpha (pc == key ? 0.45f : 0.22f) : juce::Colours::white.withAlpha (0.04f));
        g.drawHorizontalLine ((int) yy, plot.getX(), plot.getRight());

        if (inScale)
        {
            g.setColour (pc == key ? col::purple.brighter (0.5f) : col::textDim);
            g.drawText (noteName (m), juce::Rectangle<float> (r.getX() + 6.0f, yy - 6.0f, 40.0f, 12.0f),
                        juce::Justification::centredLeft);
        }
    }

    // Trazos
    auto drawHistory = [&] (const std::array<float, historySize>& h, juce::Colour c, float thick)
    {
        juce::Path p;
        bool drawing = false;
        for (int i = 0; i < historySize; ++i)
        {
            const float v = h[(size_t) ((writeIdx + i) % historySize)];
            const float xx = plot.getX() + plot.getWidth() * (float) i / (historySize - 1);
            if (v <= 0.0f) { drawing = false; continue; }
            const float yy = juce::jlimit (plot.getY(), plot.getBottom(), yOf (v));
            if (! drawing) { p.startNewSubPath (xx, yy); drawing = true; }
            else           p.lineTo (xx, yy);
        }
        g.setColour (c.withAlpha (0.25f));
        g.strokePath (p, juce::PathStrokeType (thick * 3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (c);
        g.strokePath (p, juce::PathStrokeType (thick, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    };

    drawHistory (inHist, col::cyan.withAlpha (0.8f), 1.6f);
    drawHistory (outHist, col::purple.brighter (0.2f), 2.4f);

    // Lectura de nota
    auto readout = r.reduced (14.0f, 8.0f).removeFromRight (130.0f).removeFromTop (60.0f);
    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (30.0f, juce::Font::bold));
    g.drawText (noteText, readout.removeFromTop (36.0f), juce::Justification::centredRight);
    g.setColour (col::textDim);
    g.setFont (juce::FontOptions (12.0f));
    g.drawText (centsText, readout, juce::Justification::centredRight);

    // Leyenda
    auto legend = r.reduced (14.0f, 8.0f).removeFromBottom (14.0f).removeFromRight (230.0f);
    g.setFont (juce::FontOptions (11.0f));
    g.setColour (col::cyan);
    g.fillEllipse (legend.getX(), legend.getCentreY() - 3.0f, 6.0f, 6.0f);
    g.setColour (col::textDim);
    g.drawText ("Tu voz", legend.withTrimmedLeft (10.0f).withWidth (80.0f), juce::Justification::centredLeft);
    g.setColour (col::purple);
    g.fillEllipse (legend.getX() + 90.0f, legend.getCentreY() - 3.0f, 6.0f, 6.0f);
    g.setColour (col::textDim);
    g.drawText ("Voz afinada", legend.withTrimmedLeft (100.0f), juce::Justification::centredLeft);
}

// =============================================================================
//  Teclado de tonalidad
// =============================================================================
bool KeySelector::isBlack (int pc) { return pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10; }

juce::Rectangle<float> KeySelector::keyRect (int pc) const
{
    static const int whiteIndex[] = { 0, -1, 1, -1, 2, 3, -1, 4, -1, 5, -1, 6 };
    static const int blackAfter[] = { -1, 0, -1, 1, -1, -1, 3, -1, 4, -1, 5, -1 };

    auto area = getLocalBounds().toFloat().reduced (2.0f);
    const float ww = area.getWidth() / 7.0f;

    if (! isBlack (pc))
        return { area.getX() + whiteIndex[pc] * ww, area.getY(), ww, area.getHeight() };

    const float bw = ww * 0.6f;
    return { area.getX() + (blackAfter[pc] + 1) * ww - bw * 0.5f, area.getY(), bw, area.getHeight() * 0.6f };
}

void KeySelector::setState (int key, const std::array<bool, 12>& m)
{
    if (key != selected || m != mask)
    {
        selected = key;
        mask = m;
        repaint();
    }
}

void KeySelector::paint (juce::Graphics& g)
{
    static const char* names[] = { "Do", "Do#", "Re", "Re#", "Mi", "Fa", "Fa#", "Sol", "Sol#", "La", "La#", "Si" };

    auto drawKey = [&] (int pc)
    {
        auto r = keyRect (pc).reduced (isBlack (pc) ? 0.0f : 1.5f, 0.0f);
        const bool black = isBlack (pc);
        const bool sel = pc == selected;

        if (sel)
        {
            g.setColour (col::purple.withAlpha (0.35f));
            g.fillRoundedRectangle (r.expanded (2.0f), 7.0f);
            g.setGradientFill (juce::ColourGradient (col::purple.brighter (0.2f), r.getX(), r.getY(),
                                                     col::violet.darker (0.4f), r.getX(), r.getBottom(), false));
        }
        else if (black)
        {
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2a2a34), r.getX(), r.getY(),
                                                     juce::Colour (0xff0a0a0f), r.getX(), r.getBottom(), false));
        }
        else
        {
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xffd9d9e3), r.getX(), r.getY(),
                                                     juce::Colour (0xff9e9eae), r.getX(), r.getBottom(), false));
        }
        g.fillRoundedRectangle (r, 5.0f);
        g.setColour (juce::Colours::black.withAlpha (0.6f));
        g.drawRoundedRectangle (r, 5.0f, 1.0f);

        // Punto: nota que pertenece a la escala
        if (mask[(size_t) pc])
        {
            const float d = 7.0f;
            const float yy = r.getBottom() - (black ? 14.0f : 34.0f);
            g.setColour (sel ? juce::Colours::white : col::purple);
            g.fillEllipse (r.getCentreX() - d * 0.5f, yy, d, d);
        }

        g.setColour (sel ? juce::Colours::white : (black ? col::textDim : juce::Colour (0xff2a2a35)));
        g.setFont (juce::FontOptions (black ? 10.0f : 13.0f, juce::Font::bold));
        g.drawText (names[pc], r.withTrimmedTop (r.getHeight() - (black ? 30.0f : 22.0f)).withTrimmedBottom (black ? 16.0f : 4.0f),
                    juce::Justification::centred);
    };

    for (int pc = 0; pc < 12; ++pc) if (! isBlack (pc)) drawKey (pc);
    for (int pc = 0; pc < 12; ++pc) if (isBlack (pc))   drawKey (pc);
}

void KeySelector::mouseDown (const juce::MouseEvent& e)
{
    const auto p = e.position;
    int hit = -1;
    for (int pc = 0; pc < 12 && hit < 0; ++pc)
        if (isBlack (pc) && keyRect (pc).contains (p)) hit = pc;
    for (int pc = 0; pc < 12 && hit < 0; ++pc)
        if (! isBlack (pc) && keyRect (pc).contains (p)) hit = pc;

    if (hit >= 0 && onKeyChosen)
        onKeyChosen (hit);
}

// =============================================================================
//  Botón de encendido
// =============================================================================
void PowerButton::paintButton (juce::Graphics& g, bool highlighted, bool)
{
    auto r = getLocalBounds().toFloat().reduced (3.0f);
    const bool on = getToggleState();
    const auto c = on ? col::purple.brighter (0.3f) : col::textDim.withAlpha (highlighted ? 0.9f : 0.6f);

    if (on)
    {
        g.setColour (col::purple.withAlpha (0.18f));
        g.fillEllipse (r);
    }

    auto icon = r.reduced (r.getWidth() * 0.22f);
    juce::Path arc;
    arc.addCentredArc (icon.getCentreX(), icon.getCentreY(), icon.getWidth() * 0.5f, icon.getHeight() * 0.5f,
                       0.0f, juce::MathConstants<float>::pi * 0.22f, juce::MathConstants<float>::pi * 1.78f, true);
    g.setColour (c);
    g.strokePath (arc, juce::PathStrokeType (2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.drawLine (icon.getCentreX(), icon.getY() - 1.0f, icon.getCentreX(), icon.getCentreY(), 2.2f);
}

// =============================================================================
//  Editor
// =============================================================================
GabyAutotuneEditor::GabyAutotuneEditor (GabyAutotuneProcessor& p)
    : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&lnf);

    addAndMakeVisible (inMeter);
    addAndMakeVisible (outMeter);
    addAndMakeVisible (display);
    addAndMakeVisible (keySelector);
    addAndMakeVisible (powerButton);

    setupKnob (gainKnob, gainLabel, "GANANCIA");
    setupKnob (retuneKnob, retuneLabel, "VELOCIDAD");
    setupKnob (humanizeKnob, humanizeLabel, "HUMANIZE");
    setupKnob (mixKnob, mixLabel, "MEZCLA");
    setupKnob (volumeKnob, volumeLabel, "VOLUMEN");

    gainAtt     = std::make_unique<SliderAtt> (proc.apvts, "inGain", gainKnob);
    retuneAtt   = std::make_unique<SliderAtt> (proc.apvts, "retune", retuneKnob);
    humanizeAtt = std::make_unique<SliderAtt> (proc.apvts, "humanize", humanizeKnob);
    mixAtt      = std::make_unique<SliderAtt> (proc.apvts, "mix", mixKnob);
    volumeAtt   = std::make_unique<SliderAtt> (proc.apvts, "outGain", volumeKnob);
    powerAtt    = std::make_unique<ButtonAtt> (proc.apvts, "power", powerButton);

    retuneKnob.setTextValueSuffix (" ms");
    humanizeKnob.setTextValueSuffix (" %");
    mixKnob.setTextValueSuffix (" %");
    gainKnob.setTextValueSuffix (" dB");
    volumeKnob.setTextValueSuffix (" dB");
    for (auto* s : { &gainKnob, &retuneKnob, &humanizeKnob, &mixKnob, &volumeKnob })
        s->setNumDecimalPlacesToDisplay (1);

    // Escalas
    const char* scaleLabels[] = { "CROM\xc3\x81TICA", "MAYOR", "MENOR", "ARM\xc3\x93NICA", "PENTA m", "PENTA M" };
    for (int i = 0; i < 6; ++i)
    {
        auto* b = scaleButtons.add (new juce::TextButton (u8 (scaleLabels[i])));
        b->setClickingTogglesState (false);
        b->onClick = [this, i] { setChoice ("scale", i); };
        addAndMakeVisible (b);
    }

    // Tonalidad
    keySelector.onKeyChosen = [this] (int k) { setChoice ("key", k); };
    prevKey.onClick = [this] { setChoice ("key", ((int) proc.apvts.getRawParameterValue ("key")->load() + 11) % 12); };
    nextKey.onClick = [this] { setChoice ("key", ((int) proc.apvts.getRawParameterValue ("key")->load() + 1) % 12); };
    addAndMakeVisible (prevKey);
    addAndMakeVisible (nextKey);

    keyNameLabel.setJustificationType (juce::Justification::centred);
    keyNameLabel.setFont (juce::FontOptions (16.0f, juce::Font::bold));
    keyNameLabel.setColour (juce::Label::textColourId, col::text);
    addAndMakeVisible (keyNameLabel);

    setSize (940, 600);
    timerCallback();
    startTimerHz (30);
}

GabyAutotuneEditor::~GabyAutotuneEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void GabyAutotuneEditor::setupKnob (juce::Slider& s, juce::Label& l, const juce::String& text)
{
    s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 18);
    s.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    s.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    s.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    s.setColour (juce::Slider::textBoxTextColourId, col::textDim);
    s.setColour (juce::Slider::textBoxHighlightColourId, col::violet);
    addAndMakeVisible (s);

    l.setText (text, juce::dontSendNotification);
    l.setJustificationType (juce::Justification::centred);
    l.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    l.setColour (juce::Label::textColourId, col::text);
    addAndMakeVisible (l);
}

void GabyAutotuneEditor::setChoice (const juce::String& id, int index)
{
    if (auto* param = proc.apvts.getParameter (id))
    {
        param->beginChangeGesture();
        param->setValueNotifyingHost (param->convertTo0to1 ((float) index));
        param->endChangeGesture();
    }
    timerCallback();
}

void GabyAutotuneEditor::drawPanel (juce::Graphics& g, juce::Rectangle<int> area, const juce::String& title) const
{
    auto r = area.toFloat();
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff1b1b25), r.getX(), r.getY(),
                                             juce::Colour (0xff121219), r.getX(), r.getBottom(), false));
    g.fillRoundedRectangle (r, 12.0f);
    g.setColour (col::border);
    g.drawRoundedRectangle (r.reduced (0.5f), 12.0f, 1.0f);

    if (title.isNotEmpty())
    {
        auto t = area.reduced (14, 10).removeFromTop (18);
        g.setColour (col::purple);
        g.fillRoundedRectangle ((float) t.getX(), (float) t.getCentreY() - 4.0f, 16.0f, 8.0f, 4.0f);
        g.setColour (col::text);
        g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        g.drawText (title, t.withTrimmedLeft (24), juce::Justification::centredLeft);
    }
}

void GabyAutotuneEditor::paint (juce::Graphics& g)
{
    auto full = getLocalBounds().toFloat();

    // Fondo
    g.setGradientFill (juce::ColourGradient (col::bgTop, 0.0f, 0.0f, col::bgBottom, 0.0f, full.getBottom(), false));
    g.fillAll();

    // Marco con brillo morado
    auto frame = full.reduced (3.0f);
    g.setColour (col::purple.withAlpha (0.15f));
    g.drawRoundedRectangle (frame, 16.0f, 5.0f);
    g.setColour (col::purple.withAlpha (0.55f));
    g.drawRoundedRectangle (frame, 16.0f, 1.2f);

    // Encabezado
    drawPanel (g, headerArea, {});
    {
        auto h = headerArea.reduced (16, 0).toFloat();
        // Logo de barras
        const float heights[] = { 0.35f, 0.65f, 1.0f, 0.65f, 0.35f };
        float bx = h.getX();
        for (float hh : heights)
        {
            const float bh = 26.0f * hh;
            juce::Rectangle<float> bar (bx, h.getCentreY() - bh * 0.5f, 3.5f, bh);
            g.setGradientFill (juce::ColourGradient (col::cyan, bar.getX(), bar.getY(), col::purple, bar.getX(), bar.getBottom(), false));
            g.fillRoundedRectangle (bar, 1.75f);
            bx += 6.5f;
        }
        g.setColour (col::text);
        g.setFont (juce::FontOptions (20.0f, juce::Font::bold));
        g.drawText ("Gaby Autotune", juce::Rectangle<float> (bx + 8.0f, h.getY(), 220.0f, h.getHeight()), juce::Justification::centredLeft);
        g.setColour (col::textDim);
        g.setFont (juce::FontOptions (10.0f));
        g.drawText ("G A B Y   R E C O R D S", juce::Rectangle<float> (bx + 160.0f, h.getY() + 3.0f, 160.0f, h.getHeight()), juce::Justification::centredLeft);

        // Caja central de tonalidad
        auto box = keyNameLabel.getBounds().toFloat().expanded (42.0f, 4.0f);
        g.setColour (juce::Colour (0xff0b0b12));
        g.fillRoundedRectangle (box, 8.0f);
        g.setColour (col::border);
        g.drawRoundedRectangle (box, 8.0f, 1.0f);
    }

    drawPanel (g, inPanel, "ENTRADA");
    drawPanel (g, outPanel, "SALIDA");
    drawPanel (g, tunePanel, u8 ("AFINACI\xc3\x93N"));
    drawPanel (g, volPanel, "VOLUMEN FINAL");
    drawPanel (g, keyPanel, "TONALIDAD");

    // Ayudas
    g.setColour (col::textDim);
    g.setFont (juce::FontOptions (11.0f));
    g.drawFittedText (u8 ("0 ms = efecto tumbado\n50+ ms = afinaci\xc3\xb3n natural"),
                      tunePanel.reduced (10, 12).removeFromBottom (32), juce::Justification::centred, 2);
    g.drawFittedText (u8 ("Toca una tecla para elegir la tonalidad de tu canci\xc3\xb3n. Los puntos morados son las notas de la escala."),
                      keyPanel.reduced (20, 10).removeFromBottom (30), juce::Justification::centred, 2);
}

void GabyAutotuneEditor::resized()
{
    headerArea = { 15, 12, 910, 52 };
    inPanel    = { 15, 78, 170, 280 };
    tunePanel  = { 15, 370, 170, 215 };
    outPanel   = { 755, 78, 170, 280 };
    volPanel   = { 755, 370, 170, 215 };
    keyPanel   = { 200, 340, 540, 245 };

    // Encabezado
    keyNameLabel.setBounds (headerArea.getCentreX() - 90, headerArea.getY() + 12, 180, 28);
    prevKey.setBounds (keyNameLabel.getX() - 38, headerArea.getY() + 12, 30, 28);
    nextKey.setBounds (keyNameLabel.getRight() + 8, headerArea.getY() + 12, 30, 28);
    powerButton.setBounds (headerArea.getRight() - 50, headerArea.getY() + 8, 36, 36);

    // Entrada
    inMeter.setBounds (inPanel.getX() + 52, inPanel.getY() + 34, 80, 148);
    gainLabel.setBounds (inPanel.getX(), inPanel.getY() + 184, inPanel.getWidth(), 16);
    gainKnob.setBounds (inPanel.getX() + 35, inPanel.getY() + 198, 100, 78);

    // Salida
    outMeter.setBounds (outPanel.getX() + 52, outPanel.getY() + 34, 80, 148);
    mixLabel.setBounds (outPanel.getX(), outPanel.getY() + 184, outPanel.getWidth(), 16);
    mixKnob.setBounds (outPanel.getX() + 35, outPanel.getY() + 198, 100, 78);

    // Afinación
    retuneLabel.setBounds (tunePanel.getX() + 4, tunePanel.getY() + 40, 81, 16);
    retuneKnob.setBounds (tunePanel.getX() + 4, tunePanel.getY() + 58, 81, 100);
    humanizeLabel.setBounds (tunePanel.getX() + 85, tunePanel.getY() + 40, 81, 16);
    humanizeKnob.setBounds (tunePanel.getX() + 85, tunePanel.getY() + 58, 81, 100);

    // Volumen final
    volumeLabel.setBounds (volPanel.getX(), volPanel.getY() + 40, volPanel.getWidth(), 16);
    volumeKnob.setBounds (volPanel.getX() + 30, volPanel.getY() + 60, 110, 130);

    // Centro
    display.setBounds (200, 78, 540, 190);

    const int bw = (540 - 5 * 8) / 6;
    for (int i = 0; i < scaleButtons.size(); ++i)
        scaleButtons[i]->setBounds (200 + i * (bw + 8), 280, bw, 48);

    keySelector.setBounds (keyPanel.getX() + 24, keyPanel.getY() + 38, keyPanel.getWidth() - 48, 160);
}

void GabyAutotuneEditor::timerCallback()
{
    inMeter.setLevel (proc.inputPeak.exchange (0.0f));
    outMeter.setLevel (proc.outputPeak.exchange (0.0f));

    const float inMidi = proc.getInputMidi();
    const float outMidi = proc.getOutputMidi();
    display.push (inMidi, outMidi);

    if (outMidi > 0.0f && inMidi > 0.0f)
    {
        const int note = (int) std::round (outMidi);
        const int cents = (int) std::round ((inMidi - (float) note) * 100.0f);
        display.setReadout (noteName (note), (cents >= 0 ? "+" : "") + juce::String (cents) + " cents");
    }
    else
    {
        display.setReadout ("--", {});
    }

    const int key = juce::jlimit (0, 11, (int) proc.apvts.getRawParameterValue ("key")->load());
    const int scale = juce::jlimit (0, 5, (int) proc.apvts.getRawParameterValue ("scale")->load());

    static const char* keyDisplay[] = { "Do", "Do#", "Re", "Re#", "Mi", "Fa", "Fa#", "Sol", "Sol#", "La", "La#", "Si" };
    static const char* scaleDisplay[] = { "Crom\xc3\xa1tica", "Mayor", "Menor", "Menor arm\xc3\xb3nica", "Penta menor", "Penta mayor" };
    keyNameLabel.setText (juce::String (keyDisplay[key]) + " " + u8 (scaleDisplay[scale]), juce::dontSendNotification);

    for (int i = 0; i < scaleButtons.size(); ++i)
        scaleButtons[i]->setToggleState (i == scale, juce::dontSendNotification);

    const auto mask = maskFor (key, scale);
    keySelector.setState (key, mask);
    display.setScale (key, scale);
}
