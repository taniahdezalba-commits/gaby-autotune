#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "GabyTuneDSP.h"

class GabyAutotuneProcessor : public juce::AudioProcessor
{
public:
    GabyAutotuneProcessor();
    ~GabyAutotuneProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Gaby Autotune"; }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.06; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    juce::AudioProcessorValueTreeState apvts;

    // Datos para la interfaz (se leen desde el hilo gráfico)
    std::atomic<float> inputPeak { 0.0f }, outputPeak { 0.0f };
    float getInputMidi() const  { return engine.getInputMidi(); }
    float getOutputMidi() const { return engine.getOutputMidi(); }

    static const juce::StringArray keyNames;
    static const juce::StringArray scaleNames;

private:
    gaby::AutotuneEngine engine;
    juce::AudioBuffer<float> dryBuffer;

    juce::SmoothedValue<float> inGainSm, outGainSm, mixSm;

    std::atomic<float>* pKey = nullptr;
    std::atomic<float>* pScale = nullptr;
    std::atomic<float>* pRetune = nullptr;
    std::atomic<float>* pHumanize = nullptr;
    std::atomic<float>* pInGain = nullptr;
    std::atomic<float>* pOutGain = nullptr;
    std::atomic<float>* pMix = nullptr;
    std::atomic<float>* pPower = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GabyAutotuneProcessor)
};
