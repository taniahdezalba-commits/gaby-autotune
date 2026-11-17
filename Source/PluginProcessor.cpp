#include "PluginProcessor.h"
#include "PluginEditor.h"

const juce::StringArray GabyAutotuneProcessor::keyNames {
    "Do", "Do#", "Re", "Re#", "Mi", "Fa", "Fa#", "Sol", "Sol#", "La", "La#", "Si"
};

const juce::StringArray GabyAutotuneProcessor::scaleNames {
    "Cromatica", "Mayor", "Menor", "Menor armonica", "Pentatonica menor", "Pentatonica mayor"
};

GabyAutotuneProcessor::GabyAutotuneProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "GabyAutotune", createLayout())
{
    pKey      = apvts.getRawParameterValue ("key");
    pScale    = apvts.getRawParameterValue ("scale");
    pRetune   = apvts.getRawParameterValue ("retune");
    pHumanize = apvts.getRawParameterValue ("humanize");
    pInGain   = apvts.getRawParameterValue ("inGain");
    pOutGain  = apvts.getRawParameterValue ("outGain");
    pMix      = apvts.getRawParameterValue ("mix");
    pPower    = apvts.getRawParameterValue ("power");
}

juce::AudioProcessorValueTreeState::ParameterLayout GabyAutotuneProcessor::createLayout()
{
    using namespace juce;
    std::vector<std::unique_ptr<RangedAudioParameter>> p;

    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "key", 1 }, "Tonalidad", keyNames, 0));
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "scale", 1 }, "Escala", scaleNames, 0));

    p.push_back (std::make_unique<AudioParameterFloat> (
        ParameterID { "retune", 1 }, "Velocidad",
        NormalisableRange<float> (0.0f, 400.0f, 0.1f, 0.4f), 0.0f,
        AudioParameterFloatAttributes().withLabel ("ms")));

    p.push_back (std::make_unique<AudioParameterFloat> (
        ParameterID { "humanize", 1 }, "Humanize",
        NormalisableRange<float> (0.0f, 100.0f, 0.1f), 0.0f,
        AudioParameterFloatAttributes().withLabel ("%")));

    p.push_back (std::make_unique<AudioParameterFloat> (
        ParameterID { "inGain", 1 }, "Ganancia",
        NormalisableRange<float> (-24.0f, 24.0f, 0.1f), 0.0f,
        AudioParameterFloatAttributes().withLabel ("dB")));

    p.push_back (std::make_unique<AudioParameterFloat> (
        ParameterID { "outGain", 1 }, "Volumen",
        NormalisableRange<float> (-24.0f, 24.0f, 0.1f), 0.0f,
        AudioParameterFloatAttributes().withLabel ("dB")));

    p.push_back (std::make_unique<AudioParameterFloat> (
        ParameterID { "mix", 1 }, "Mezcla",
        NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f,
        AudioParameterFloatAttributes().withLabel ("%")));

    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { "power", 1 }, "Encendido", true));

    return { p.begin(), p.end() };
}

bool GabyAutotuneProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

void GabyAutotuneProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.prepare (sampleRate, getTotalNumOutputChannels());
    dryBuffer.setSize (getTotalNumOutputChannels(), samplesPerBlock);

    inGainSm.reset (sampleRate, 0.05);
    outGainSm.reset (sampleRate, 0.05);
    mixSm.reset (sampleRate, 0.05);
    inGainSm.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (pInGain->load()));
    outGainSm.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (pOutGain->load()));
    mixSm.setCurrentAndTargetValue (pMix->load() * 0.01f);
}

static float peakOf (const juce::AudioBuffer<float>& b, int numCh, int n)
{
    float pk = 0.0f;
    for (int c = 0; c < numCh; ++c)
        pk = juce::jmax (pk, b.getMagnitude (c, 0, n));
    return pk;
}

static void storeMax (std::atomic<float>& a, float v)
{
    float prev = a.load();
    while (v > prev && ! a.compare_exchange_weak (prev, v)) {}
}

void GabyAutotuneProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numCh = getTotalNumOutputChannels();
    const int n = buffer.getNumSamples();

    for (int c = getTotalNumInputChannels(); c < numCh; ++c)
        buffer.clear (c, 0, n);

    if (dryBuffer.getNumSamples() < n || dryBuffer.getNumChannels() < numCh)
        dryBuffer.setSize (numCh, n, false, false, true);

    // Ganancia de entrada
    inGainSm.setTargetValue (juce::Decibels::decibelsToGain (pInGain->load()));
    for (int i = 0; i < n; ++i)
    {
        const float g = inGainSm.getNextValue();
        for (int c = 0; c < numCh; ++c)
            buffer.getWritePointer (c)[i] *= g;
    }
    storeMax (inputPeak, peakOf (buffer, numCh, n));

    if (pPower->load() > 0.5f)
    {
        gaby::TuneSettings s;
        s.key = juce::jlimit (0, 11, (int) pKey->load());
        s.scale = (gaby::Scale) juce::jlimit (0, 5, (int) pScale->load());
        s.retuneMs = pRetune->load();
        s.humanize = pHumanize->load() * 0.01;
        engine.setSettings (s);

        for (int c = 0; c < numCh; ++c)
            dryBuffer.copyFrom (c, 0, buffer, c, 0, n);

        engine.process (buffer.getArrayOfWritePointers(), numCh, n);

        // Mezcla voz original / afinada
        mixSm.setTargetValue (pMix->load() * 0.01f);
        for (int i = 0; i < n; ++i)
        {
            const float m = mixSm.getNextValue();
            for (int c = 0; c < numCh; ++c)
            {
                auto* w = buffer.getWritePointer (c);
                w[i] = w[i] * m + dryBuffer.getSample (c, i) * (1.0f - m);
            }
        }
    }

    // Volumen de salida
    outGainSm.setTargetValue (juce::Decibels::decibelsToGain (pOutGain->load()));
    for (int i = 0; i < n; ++i)
    {
        const float g = outGainSm.getNextValue();
        for (int c = 0; c < numCh; ++c)
            buffer.getWritePointer (c)[i] *= g;
    }
    storeMax (outputPeak, peakOf (buffer, numCh, n));
}

juce::AudioProcessorEditor* GabyAutotuneProcessor::createEditor()
{
    return new GabyAutotuneEditor (*this);
}

void GabyAutotuneProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void GabyAutotuneProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new GabyAutotuneProcessor();
}
