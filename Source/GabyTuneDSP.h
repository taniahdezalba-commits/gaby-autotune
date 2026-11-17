// =============================================================================
//  Gaby Autotune - motor de afinación (sin dependencias de JUCE)
//  Gaby Records
//
//  1) Detecta el tono de la voz con el algoritmo YIN.
//  2) Calcula la nota más cercana dentro de la tonalidad/escala elegida.
//  3) Corrige el tono con un "pitch shifter" de dos lecturas cruzadas
//     sobre una línea de retardo, con ventanas sincronizadas al periodo.
// =============================================================================
#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <vector>

namespace gaby
{

static constexpr double kPi = 3.14159265358979323846;

// -----------------------------------------------------------------------------
//  Escalas
// -----------------------------------------------------------------------------
enum class Scale { Chromatic = 0, Major, Minor, HarmonicMinor, MinorPentatonic, MajorPentatonic };

inline std::array<bool, 12> scaleMask (int key, Scale scale)
{
    static const int major[]     = { 0, 2, 4, 5, 7, 9, 11 };
    static const int minor[]     = { 0, 2, 3, 5, 7, 8, 10 };
    static const int harmMinor[] = { 0, 2, 3, 5, 7, 8, 11 };
    static const int minPenta[]  = { 0, 3, 5, 7, 10 };
    static const int majPenta[]  = { 0, 2, 4, 7, 9 };

    std::array<bool, 12> mask {};
    auto fill = [&] (const int* steps, int n)
    {
        for (int i = 0; i < n; ++i)
            mask[(size_t) ((key + steps[i]) % 12)] = true;
    };

    switch (scale)
    {
        case Scale::Major:           fill (major, 7);     break;
        case Scale::Minor:           fill (minor, 7);     break;
        case Scale::HarmonicMinor:   fill (harmMinor, 7); break;
        case Scale::MinorPentatonic: fill (minPenta, 5);  break;
        case Scale::MajorPentatonic: fill (majPenta, 5);  break;
        case Scale::Chromatic:
        default:                     mask.fill (true);    break;
    }
    return mask;
}

// Devuelve la nota MIDI (entera) de la escala más cercana a 'midi'.
inline double nearestScaleNote (double midi, const std::array<bool, 12>& mask)
{
    const int base = (int) std::floor (midi);
    double best = midi;
    double bestDist = 1.0e9;

    for (int n = base - 12; n <= base + 12; ++n)
    {
        const int pc = ((n % 12) + 12) % 12;
        if (! mask[(size_t) pc])
            continue;

        const double d = std::abs ((double) n - midi);
        if (d < bestDist)
        {
            bestDist = d;
            best = (double) n;
        }
    }
    return best;
}

inline double freqToMidi (double f) { return 69.0 + 12.0 * std::log2 (f / 440.0); }

// -----------------------------------------------------------------------------
//  Detector de tono YIN
// -----------------------------------------------------------------------------
class PitchDetector
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        // Analizamos a ~44-48 kHz como máximo para ahorrar CPU.
        decim = std::max (1, (int) std::round (sampleRate / 44100.0));
        analysisRate = sr / decim;

        windowSize = 1024;
        minLag = std::max (2, (int) (analysisRate / maxFreq));
        maxLag = std::min (windowSize - 1, (int) (analysisRate / minFreq));

        ring.assign ((size_t) (windowSize + maxLag + 1), 0.0f);
        diff.assign ((size_t) (maxLag + 2), 0.0f);
        frame.assign ((size_t) (windowSize + maxLag + 1), 0.0f);
        writePos = 0;
        decimCount = 0;
        decimAcc = 0.0f;
    }

    // Empuja una muestra (mono). Devuelve true si hay un nuevo cuadro listo
    // para analizar (cada 'hop' muestras de análisis).
    bool push (float x)
    {
        decimAcc += x;
        if (++decimCount < decim)
            return false;

        const float v = decimAcc / (float) decim;
        decimCount = 0;
        decimAcc = 0.0f;

        ring[(size_t) writePos] = v;
        writePos = (writePos + 1) % (int) ring.size();

        if (++hopCount >= hop)
        {
            hopCount = 0;
            return true;
        }
        return false;
    }

    // Analiza el último cuadro. Devuelve la frecuencia en Hz o 0 si no hay voz.
    double analyse()
    {
        const int total = (int) ring.size();
        for (int i = 0; i < total; ++i)
            frame[(size_t) i] = ring[(size_t) ((writePos + i) % total)];

        // Nivel mínimo: silencio = sin tono
        double energy = 0.0;
        for (int i = 0; i < windowSize; ++i)
            energy += (double) frame[(size_t) i] * frame[(size_t) i];
        if (energy / windowSize < 1.0e-7)
        {
            confidence = 0.0;
            return 0.0;
        }

        // Función diferencia
        for (int tau = 1; tau <= maxLag; ++tau)
        {
            double s = 0.0;
            const float* a = frame.data();
            const float* b = frame.data() + tau;
            for (int i = 0; i < windowSize; ++i)
            {
                const float d = a[i] - b[i];
                s += (double) d * d;
            }
            diff[(size_t) tau] = (float) s;
        }

        // Diferencia normalizada acumulada (CMNDF)
        diff[0] = 1.0f;
        double running = 0.0;
        for (int tau = 1; tau <= maxLag; ++tau)
        {
            running += diff[(size_t) tau];
            diff[(size_t) tau] = running > 0.0 ? (float) (diff[(size_t) tau] * tau / running) : 1.0f;
        }

        // Primer mínimo bajo el umbral
        int tauEst = -1;
        for (int tau = minLag; tau <= maxLag; ++tau)
        {
            if (diff[(size_t) tau] < threshold)
            {
                while (tau + 1 <= maxLag && diff[(size_t) (tau + 1)] < diff[(size_t) tau])
                    ++tau;
                tauEst = tau;
                break;
            }
        }

        if (tauEst < 0)
        {
            confidence = 0.0;
            return 0.0;
        }

        confidence = 1.0 - diff[(size_t) tauEst];

        // Interpolación parabólica
        double betterTau = tauEst;
        if (tauEst > 1 && tauEst < maxLag)
        {
            const double s0 = diff[(size_t) (tauEst - 1)];
            const double s1 = diff[(size_t) tauEst];
            const double s2 = diff[(size_t) (tauEst + 1)];
            const double denom = 2.0 * (2.0 * s1 - s2 - s0);
            if (std::abs (denom) > 1.0e-12)
                betterTau = tauEst + (s2 - s0) / denom;
        }

        return analysisRate / betterTau;
    }

    int hopSizeHostSamples() const { return hop * decim; }
    double getConfidence() const   { return confidence; }

private:
    double sr = 44100.0, analysisRate = 44100.0;
    int decim = 1, decimCount = 0;
    float decimAcc = 0.0f;

    int windowSize = 1024, minLag = 40, maxLag = 600;
    int writePos = 0, hop = 256, hopCount = 0;
    double minFreq = 70.0, maxFreq = 1000.0, threshold = 0.15, confidence = 0.0;

    std::vector<float> ring, diff, frame;
};

// -----------------------------------------------------------------------------
//  Pitch shifter de dos lecturas (una por canal)
// -----------------------------------------------------------------------------
class PitchShifter
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        const int size = (int) std::ceil (sampleRate * 0.12) + 8; // 120 ms
        buffer.assign ((size_t) size, 0.0f);
        writePos = 0;
        for (auto& t : taps)
        {
            t.window = defaultWindow();
            t.nextWindow = t.window;
        }
        taps[0].phase = 0.0;
        taps[1].phase = 0.5;
    }

    // Longitud de ventana deseada (en muestras), normalmente múltiplo del periodo.
    void setWindow (double samples)
    {
        const double w = std::clamp (samples, sr * 0.012, sr * 0.06);
        for (auto& t : taps)
            t.nextWindow = w;
    }

    float process (float in, double ratio)
    {
        buffer[(size_t) writePos] = in;

        float out = 0.0f;
        double gainSq = 0.0;
        double gains[2];
        float reads[2];

        for (int i = 0; i < 2; ++i)
        {
            auto& t = taps[(size_t) i];

            const double delay = t.phase * t.window + 1.0;
            reads[i] = readInterpolated (delay);
            gains[i] = std::sin (kPi * t.phase);
            gainSq += gains[i] * gains[i];

            // Avanza la fase: (1 - ratio) / ventana por muestra
            t.phase += (1.0 - ratio) / t.window;

            // Al cruzar el borde, la ganancia es cero: momento seguro para
            // cambiar el tamaño de ventana.
            if (t.phase >= 1.0)
            {
                t.phase -= 1.0;
                t.window = t.nextWindow;
            }
            else if (t.phase < 0.0)
            {
                t.phase += 1.0;
                t.window = t.nextWindow;
            }
        }

        const double norm = gainSq > 1.0e-6 ? 1.0 / std::sqrt (gainSq) : 0.0;
        out = (float) ((reads[0] * gains[0] + reads[1] * gains[1]) * norm);

        writePos = (writePos + 1) % (int) buffer.size();
        return out;
    }

    // Mantiene las dos lecturas sincronizadas (para que ambos canales suenen igual)
    void copyPhaseFrom (const PitchShifter& other)
    {
        for (size_t i = 0; i < 2; ++i)
        {
            taps[i].phase = other.taps[i].phase;
            taps[i].window = other.taps[i].window;
            taps[i].nextWindow = other.taps[i].nextWindow;
        }
    }

    void reset() { std::fill (buffer.begin(), buffer.end(), 0.0f); }

private:
    struct Tap { double phase = 0.0, window = 1000.0, nextWindow = 1000.0; };

    double defaultWindow() const { return sr * 0.03; }

    float readInterpolated (double delay) const
    {
        const int size = (int) buffer.size();
        double pos = (double) writePos - delay;
        while (pos < 0.0) pos += size;

        const int i0 = (int) pos;
        const double frac = pos - i0;
        const int i1 = (i0 + 1) % size;
        return (float) (buffer[(size_t) i0] * (1.0 - frac) + buffer[(size_t) i1] * frac);
    }

    double sr = 44100.0;
    std::vector<float> buffer;
    int writePos = 0;
    std::array<Tap, 2> taps {};
};

// -----------------------------------------------------------------------------
//  Motor completo
// -----------------------------------------------------------------------------
struct TuneSettings
{
    int key = 0;                       // 0 = Do (C), 1 = Do#, ... 11 = Si
    Scale scale = Scale::Chromatic;
    double retuneMs = 0.0;             // 0 = efecto robótico, 400 = muy natural
    double humanize = 0.0;             // 0..1
    double amount = 1.0;               // 0..1 cuánta corrección aplicar
};

class AutotuneEngine
{
public:
    void prepare (double sampleRate, int numChannels)
    {
        sr = sampleRate;
        detector.prepare (sampleRate);
        shifters.resize ((size_t) std::max (1, numChannels));
        for (auto& s : shifters)
            s.prepare (sampleRate);

        correction = 0.0;
        currentRatio = 1.0;
        targetRatio = 1.0;
        ratioStep = 0.0;
        heldSamples = 0.0;
        lastTarget = -1.0;
        inputMidi.store (-1.0f);
        outputMidi.store (-1.0f);
    }

    void setSettings (const TuneSettings& s) { settings = s; mask = scaleMask (s.key, s.scale); }

    // Procesa un bloque. 'channels' son punteros a cada canal (in-place).
    void process (float* const* channels, int numChannels, int numSamples)
    {
        const int hopHost = detector.hopSizeHostSamples();

        for (int n = 0; n < numSamples; ++n)
        {
            // Detección en mono
            float mono = 0.0f;
            for (int c = 0; c < numChannels; ++c)
                mono += channels[c][n];
            mono /= (float) std::max (1, numChannels);

            if (detector.push (mono))
                updateCorrection (detector.analyse(), hopHost);

            // Interpolación suave del ratio dentro del bloque
            currentRatio += ratioStep;
            if ((ratioStep > 0.0 && currentRatio > targetRatio) || (ratioStep < 0.0 && currentRatio < targetRatio))
            {
                currentRatio = targetRatio;
                ratioStep = 0.0;
            }

            // Los canales extra copian la fase del canal 0 ANTES de que avance
            const int nCh = std::min (numChannels, (int) shifters.size());
            for (int c = nCh - 1; c >= 0; --c)
            {
                if (c > 0)
                    shifters[(size_t) c].copyPhaseFrom (shifters[0]);
                channels[c][n] = shifters[(size_t) c].process (channels[c][n], currentRatio);
            }
        }
    }

    float getInputMidi() const  { return inputMidi.load(); }
    float getOutputMidi() const { return outputMidi.load(); }

private:
    void updateCorrection (double freq, int hopHost)
    {
        double desired = 0.0;
        const bool voiced = freq > 0.0;

        if (voiced)
        {
            const double midi = freqToMidi (freq);
            const double target = nearestScaleNote (midi, mask);

            if (std::abs (target - lastTarget) > 0.01)
            {
                heldSamples = 0.0;
                lastTarget = target;
            }
            else
            {
                heldSamples += hopHost;
            }

            desired = (target - midi) * settings.amount;

            // Ventana sincronizada al periodo (2 periodos, mínimo ~12 ms)
            const double period = sr / freq;
            double w = period * 2.0;
            while (w < sr * 0.02) w += period;
            for (auto& s : shifters)
                s.setWindow (w);

            inputMidi.store ((float) midi);
        }
        else
        {
            heldSamples = 0.0;
            lastTarget = -1.0;
            inputMidi.store (-1.0f);
        }

        // Velocidad de corrección (retune speed). Humanize alarga el tiempo
        // en notas sostenidas para conservar el vibrato natural.
        double tauMs = settings.retuneMs;
        if (voiced && heldSamples > sr * 0.15)
            tauMs += settings.humanize * 250.0;

        double alpha = 1.0;
        if (tauMs > 0.5)
            alpha = 1.0 - std::exp (-(double) hopHost / (tauMs * 0.001 * sr));

        if (! voiced)
            alpha = std::min (1.0, (double) hopHost / (0.03 * sr)); // regresa a 0 suave

        correction += (desired - correction) * alpha;
        correction = std::clamp (correction, -12.0, 12.0);

        targetRatio = std::pow (2.0, correction / 12.0);
        ratioStep = (targetRatio - currentRatio) / (double) std::max (1, hopHost);

        const float in = inputMidi.load();
        outputMidi.store (in > 0.0f ? (float) (in + correction) : -1.0f);
    }

    double sr = 44100.0;
    PitchDetector detector;
    std::vector<PitchShifter> shifters;

    TuneSettings settings;
    std::array<bool, 12> mask = scaleMask (0, Scale::Chromatic);

    double correction = 0.0, currentRatio = 1.0, targetRatio = 1.0, ratioStep = 0.0;
    double heldSamples = 0.0, lastTarget = -1.0;

    std::atomic<float> inputMidi { -1.0f }, outputMidi { -1.0f };
};

} // namespace gaby
