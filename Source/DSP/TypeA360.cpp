// TypeA360.cpp
// See TypeA360.h for the circuit. Plain C++98.

#include "TypeA360.h"

#include <cmath>
#include <cstring>

namespace tbtypea
{

namespace
{
    // ---- Levels --------------------------------------------------------------
    // -18 dBFS sine = Dolby level = +4 dBu (1.228 V rms) at the line input
    const double kLineVoltsPerFS = 1.228 / 0.12589254;
    const double kDolbyOut1      = 0.300;                 // V rms at Output 1 (manual 10.7)
    const double kInputPot       = kDolbyOut1 / 1.228;    // input level pot set for Dolby level
    // Output 2 amp (x5/3), output level pot and line amp set so Dolby level
    // leaves the line amp at 0.614 V rms (+4 dBu after the 1:2 transformer)
    const double kOut2ToLineAmp  = 0.614 / kDolbyOut1;
    const double kLineAmpClip    = 7.93;    // V peak: +21 dBm into 600 ohms after 1:2 (manual 10.6)
    const double kT3Ratio        = 2.0;

    // ---- Band compressors ------------------------------------------------------
    // ESTIMATES, fitted so the whole unit follows the published A-type encode
    // curve (about +10 dB below -40 dB re Dolby level in bands 1-3, +15 dB
    // above 9 kHz, +6 at -30, +3.5 at -20, +1 at Dolby level). The real gains
    // are trimmed on test (R417 "AOT" etc.).
    const double kCompGain   = 2.162;   // low-level compressor gain (band 2 = +10 dB encode)
    const double kCompSlope  = 30.0 ;   // FET attenuator: gain = 1 / (1 + S (env - Vth))^2
    const double kCompThresh = 0.0070;  // V, rectified output where the FETs start to conduct
    const double kLimitLevel = 0.25;    // V, overshoot limiter D400/D401 (smooth clamp)
    const double kFastStep   = 0.02;    // V above the integrator where the speed-up diodes conduct
    const double kFastTau    = 0.0001;  // s, charging through the speed-up diodes
    // Integrators: R436 10K / C410 100n (1 ms) charging, R435 270K + R436
    // discharging (28 ms); bands 1 and 2 have R136 20K / R135 560K (twice).
    const double kAttack[kNumBands]  = { 0.002, 0.002, 0.001, 0.001 };
    const double kRelease[kNumBands] = { 0.058, 0.058, 0.028, 0.028 };
    // Summing resistors, relative to R209 11K3
    const double kSum[kNumBands] = { 11.3 / 11.0, 11.3 / 11.3, 11.3 / 8.87, 11.3 / 9.53 };

    // ---- Transformers (ESTIMATES: part numbers only in the manual) -----------
    const double kT2LowHz = 3.0,  kT2HighHz = 80000.0, kT2Q = 0.70;
    const double kT3LowHz = 5.0,  kT3HighHz = 60000.0, kT3Q = 0.80;
    // Core: 1% third harmonic at 20 Hz, +20 dBu
    const double kCoreSat = 397.0;

    // ---- Dolby Tone ------------------------------------------------------------
    const double kToneHz       = 850.0;
    const double kToneWarbleHz = 930.0;    // manual 10.8: sweeps towards 930 Hz
    const double kToneWarbleMs = 30.0;     // in 30 ms pulses
    const double kTonePeriod   = 0.63;     // s, Q801 unijunction: R804 15K8 x C800 33u (estimate)
    const double kToneThd      = 0.02;     // about 2% THD (manual)

    inline double clampHz (double f, double fs) { const double m = 0.45 * fs; return f < m ? f : m; }

    void highpass2 (Biquad& b, double fc, double q, double fs)
    {
        const double w = 2.0 * kPi * clampHz (fc, fs);
        b.setAnalog (1.0 / (w * w), 0.0, 0.0, 1.0 / (w * w), 1.0 / (q * w), 1.0, fs);
    }

    // Smooth clamp: y = c / (1 + (|c|/L)^4)^(1/4), dy/dc = (1 + u^4)^(-5/4)
    inline double limitDiodes (double c, double& slope)
    {
        const double u = std::fabs (c) / kLimitLevel;
        const double u4 = u * u * u * u;
        const double r = std::pow (1.0 + u4, -0.25);
        slope = r / (1.0 + u4);
        return c * r;
    }

    inline double core (double v, double& flux, double coef)
    {
        // flux = integral of v (leaky below 1 Hz); magnetising loss ~ flux^3
        flux += (v - flux * 2.0 * kPi * 1.0) * coef;
        const double f2 = flux * flux;
        return v - kCoreSat * flux * f2 / (1.0 + 4.0 * f2);
    }
}

TypeA360::TypeA360()
    : fsBase (44100.0), fsOs (176400.0),
      toneOn (false), nrIn (true), record (true),
      inT (1.0), outT (1.0), inG (1.0), outG (1.0), stressGain (1.0),
      nrW (1.0), nrWT (1.0), toneW (0.0), toneWT (0.0), smCoef (0.0),
      t2Flux (0.0), t3Flux (0.0), fluxCoef (0.0),
      tonePhase (0.0), toneWarble (0.0), toneWarbleInc (0.0),
      met1 (0.0), met2 (0.0), metCoef (0.0)
{
    for (int b = 0; b < kNumBands; ++b)
    {
        bandOn[b] = true;
        bandPot[b] = 0.5;
        bandW[b] = bandWT[b] = kSum[b];
        lastBand[b] = lastComp[b] = 0.0;
    }
    prepare (44100.0);
}

void TypeA360::prepare (double sampleRate)
{
    fsBase = sampleRate > 1000.0 ? sampleRate : 44100.0;
    fsOs = fsBase * Oversampler4x::kFactor;
    setupFilters();
    reset();
}

void TypeA360::setupFilters()
{
    const double fs = fsOs;
    smCoef = std::exp (-1.0 / (0.010 * fs));
    metCoef = 1.0 - std::exp (-1.0 / (0.065 * fs));
    fluxCoef = 1.0 / fs;
    toneWarbleInc = 1.0 / (kTonePeriod * fs);

    // Transformers
    t2Hp.highpass (kT2LowHz, fs);
    t3Hp.highpass (kT3LowHz, fs);
    t2Lp.lowpass (clampHz (kT2HighHz, fs), kT2Q, fs);
    t3Lp.lowpass (clampHz (kT3HighHz, fs), kT3Q, fs);

    // Input amplifier: R506/C503 + R507/C504 (about 75 kHz together), then
    // the Sallen-Key Q502/Q503
    inPole.lowpass (clampHz (75000.0, fs), fs);
    {
        const double R1 = 4750.0, R2 = 4220.0, C1 = 2700e-12, C2 = 390e-12;
        const double w = 1.0 / std::sqrt (R1 * R2 * C1 * C2);
        const double q = std::sqrt (R1 * R2 * C1 * C2) / (C2 * (R1 + R2));
        inSk.lowpass (clampHz (w / (2.0 * kPi), fs), q, fs);
    }

    // Band 1: Sallen-Key low-pass (R180, R181, C180 feedback, C181 to ground)
    {
        const double R1 = 301e3, R2 = 274e3, C1 = 10e-9, C2 = 4.7e-9;
        const double w = 1.0 / std::sqrt (R1 * R2 * C1 * C2);
        const double q = std::sqrt (R1 * R2 * C1 * C2) / (C2 * (R1 + R2));
        bandLp1.lowpass (w / (2.0 * kPi), q, fs);
    }
    // Bands 3 and 4: equal-C Sallen-Key high-pass; Ra feedback, Rb to the 10 V bias
    {
        const double C = 6.8e-9, Ra = 5620.0, Rb = 11e3;
        const double w = 1.0 / (C * std::sqrt (Ra * Rb));
        const double q = std::sqrt (Ra * Rb) / (2.0 * Ra);
        highpass2 (bandHp3, w / (2.0 * kPi), q, fs);
    }
    {
        const double C = 2.2e-9, Ra = 4750.0, Rb = 12e3;
        const double w = 1.0 / (C * std::sqrt (Ra * Rb));
        const double q = std::sqrt (Ra * Rb) / (2.0 * Ra);
        highpass2 (bandHp4, w / (2.0 * kPi), q, fs);
    }

    for (int b = 0; b < kNumBands; ++b)
    {
        comp[b].atk  = 1.0 - std::exp (-1.0 / (kAttack[b] * fs));
        comp[b].rel  = 1.0 - std::exp (-1.0 / (kRelease[b] * fs));
        comp[b].fast = 1.0 - std::exp (-1.0 / (kFastTau * fs));
    }
}

void TypeA360::reset()
{
    up.reset(); down.reset();
    t2Hp.reset(); t3Hp.reset(); t2Lp.reset(); t3Lp.reset();
    t2Flux = t3Flux = 0.0;
    inPole.reset(); inSk.reset();
    bandLp1.reset(); bandHp3.reset(); bandHp4.reset();
    for (int b = 0; b < kNumBands; ++b)
    {
        comp[b].reset();
        lastBand[b] = lastComp[b] = 0.0;
        bandW[b] = bandWT[b];
    }
    inG = inT; outG = outT;
    nrW = nrWT; toneW = toneWT;
    tonePhase = 0.0; toneWarble = 0.0;
    met1 = met2 = 0.0;
}

//==============================================================================
// Controls

static double dbToGain (double dB) { return std::pow (10.0, dB / 20.0); }

// Band pot: off at 0, audio taper up to the stock summing resistor at 0.5
// (half way there = -12 dB), then +18 dB more at 1 (a smaller resistor)
double TypeA360::bandLevelGain (double pot)
{
    if (! (pot > 0.0)) return 0.0;
    if (pot > 1.0) pot = 1.0;
    if (pot <= 0.5)
    {
        const double x = pot / 0.5;
        return x * x;
    }
    return std::pow (10.0, (18.0 * (pot - 0.5) / 0.5) / 20.0);
}

double TypeA360::bandLevelDb (double pot)
{
    const double g = bandLevelGain (pot);
    return g > 0.0 ? 20.0 * std::log10 (g) : -200.0;
}

void TypeA360::updateSmoothed()
{
    for (int b = 0; b < kNumBands; ++b)
        bandWT[b] = bandOn[b] ? kSum[b] * bandLevelGain (bandPot[b]) * stressGain : 0.0;
    nrWT = nrIn ? 1.0 : 0.0;
    toneWT = (toneOn && record) ? 1.0 : 0.0;
}

void TypeA360::setDolbyTone (bool on)  { toneOn = on; updateSmoothed(); }
void TypeA360::setNrIn (bool in)       { nrIn = in; updateSmoothed(); }
void TypeA360::setRecord (bool rec)    { record = rec; updateSmoothed(); }

void TypeA360::setBandOn (int band, bool on)
{
    if (band < 0 || band >= kNumBands) return;
    bandOn[band] = on;
    updateSmoothed();
}

void TypeA360::setBandLevel (int band, double v)
{
    if (band < 0 || band >= kNumBands) return;
    if (! (v == v)) return;
    bandPot[band] = v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v);
    updateSmoothed();
}

void TypeA360::setInputGainDb (double dB)  { inT = dbToGain (dB); }
void TypeA360::setStressDb (double dB)     { stressGain = dbToGain (dB < 0.0 ? 0.0 : dB); updateSmoothed(); }
void TypeA360::setOutputGainDb (double dB) { outT = dbToGain (dB); }

double TypeA360::meterDb() const
{
    const double ref = kDolbyOut1 * 0.900316;       // average of a Dolby-level sine
    const double m = met2 > 1e-9 ? met2 : 1e-9;
    return 20.0 * std::log10 (m / ref);
}

double TypeA360::bandGainReduction (int band) const
{
    return (band >= 0 && band < kNumBands) ? comp[band].gain : 1.0;
}

//==============================================================================
// Noise reduction signal for main-path signal u, with the current filter and
// compressor states (nothing is advanced). d = dNR/du, for the decoder's
// Newton solve.

double TypeA360::nrSignal (double u, double& d) const
{
    const double h1 = bandLp1.b0, h3 = bandHp3.b0, h4 = bandHp4.b0;
    const double x1 = h1 * u + bandLp1.z1;
    const double x3 = h3 * u + bandHp3.z1;
    const double x4 = h4 * u + bandHp4.z1;
    const double x[kNumBands] = { x1, u - x1 - x3, x3, x4 };
    const double h[kNumBands] = { h1, 1.0 - h1 - h3, h3, h4 };

    double nr = 0.0;
    d = 0.0;
    for (int b = 0; b < kNumBands; ++b)
    {
        if (bandW[b] == 0.0) continue;
        const double g = kCompGain * comp[b].gain;
        double slope;
        const double c = limitDiodes (g * x[b], slope);
        nr += bandW[b] * c;
        d  += bandW[b] * slope * g * h[b];
    }
    nr *= nrW;
    d  *= nrW;
    return nr;
}

// Advance the band filters with u and update each compressor from its output
void TypeA360::commitBands (double u)
{
    const double x1 = bandLp1.tick (u);
    const double x3 = bandHp3.tick (u);
    const double x4 = bandHp4.tick (u);
    lastBand[0] = x1; lastBand[1] = u - x1 - x3; lastBand[2] = x3; lastBand[3] = x4;

    for (int b = 0; b < kNumBands; ++b)
    {
        BandCompressor& k = comp[b];
        double slope;
        const double c = limitDiodes (kCompGain * k.gain * lastBand[b], slope);
        lastComp[b] = c;
        const double r = std::fabs (c);                 // full-wave rectifier D402/D403
        if (r > k.env + kFastStep)  k.env += (r - k.env) * k.fast;   // speed-up diodes
        else if (r > k.env)         k.env += (r - k.env) * k.atk;
        else                        k.env += (r - k.env) * k.rel;
        const double over = k.env - kCompThresh;
        const double a = over > 0.0 ? 1.0 + kCompSlope * over : 1.0;
        k.gain = 1.0 / (a * a);
    }
}

//==============================================================================

double TypeA360::tickOs (double xLine)
{
    // Smoothed controls
    const double s = smCoef, s1 = 1.0 - smCoef;
    inG = inG * s + inT * s1;
    outG = outG * s + outT * s1;
    nrW = nrW * s + nrWT * s1;
    toneW = toneW * s + toneWT * s1;
    for (int b = 0; b < kNumBands; ++b)
    {
        bandW[b] = bandW[b] * s + bandWT[b] * s1;
        if (std::fabs (bandW[b] - bandWT[b]) < 1e-9) bandW[b] = bandWT[b];
    }
    if (std::fabs (nrW - nrWT) < 1e-9) nrW = nrWT;
    if (std::fabs (toneW - toneWT) < 1e-9) toneW = toneWT;

    // Line input -> T2 -> input level -> input amplifier low-pass
    double v = t2Hp.tick (xLine * inG);
    v = core (v, t2Flux, fluxCoef);
    v = t2Lp.tick (v);
    v *= kInputPot;
    v = inSk.tick (inPole.tick (v));

    // Dolby Tone (record only): replaces the programme, NR signal shorted
    if (toneW > 0.0)
    {
        toneWarble += toneWarbleInc;
        if (toneWarble >= 1.0) toneWarble -= 1.0;
        const double tw = toneWarble * kTonePeriod * 1000.0;     // ms into the period
        double f = kToneHz;
        if (tw < kToneWarbleMs)
            f += (kToneWarbleHz - kToneHz) * 0.5 * (1.0 - std::cos (2.0 * kPi * tw / kToneWarbleMs));
        tonePhase += f / fsOs;
        if (tonePhase >= 1.0) tonePhase -= 1.0;
        const double ph = 2.0 * kPi * tonePhase;
        const double tone = kDolbyOut1 * 1.41421356 * (std::sin (ph) + kToneThd * std::sin (3.0 * ph))
                          / std::sqrt (1.0 + kToneThd * kToneThd);
        v = v * (1.0 - toneW) + tone * toneW;
    }
    const double nrScale = 1.0 - toneW;     // D530 shorts the NR signal while the tone runs
    const double nrSave = nrW;
    nrW *= nrScale;

    double out1, out2;
    if (record)
    {
        double d;
        const double nr = nrSignal (v, d);
        out1 = v;
        out2 = v + nr;
        commitBands (v);
    }
    else
    {
        // Decode: out1 = v - NR(out1), solved by Newton
        double o = v, d = 0.0;
        for (int it = 0; it < 8; ++it)
        {
            const double nr = nrSignal (o, d);
            const double f = o + nr - v;
            double den = 1.0 + d;
            if (den < 0.1) den = 0.1;
            o -= f / den;
            if (std::fabs (f) < 1e-10) break;
        }
        out1 = o;
        out2 = o;
        commitBands (o);
    }
    nrW = nrSave;

    // Meter on Output 1 (average responding, two poles: VU-like)
    met1 += (std::fabs (out1) - met1) * metCoef;
    met2 += (met1 - met2) * metCoef;

    // Output 2 amp, output level, line amplifier, T3
    double line = railClip (out2 * kOut2ToLineAmp, kLineAmpClip);
    line *= kT3Ratio;
    line = t3Hp.tick (line);
    line = core (line, t3Flux, fluxCoef);
    line = t3Lp.tick (line);
    // Output trim, plus make-up for the input trim (the input sets only how
    // hard the compressors are driven, not the level out)
    return line * outG / inG;
}

void TypeA360::process (const float* in, float* out, int numSamples)
{
    double os[Oversampler4x::kFactor];
    for (int i = 0; i < numSamples; ++i)
    {
        double x = in[i];
        if (! (x == x)) x = 0.0;
        up.upsample (x * kLineVoltsPerFS, os);
        for (int k = 0; k < Oversampler4x::kFactor; ++k)
            os[k] = tickOs (os[k]);
        double y = down.downsample (os) / kLineVoltsPerFS;
        if (! (y == y)) y = 0.0;
        out[i] = (float) y;
    }
}

} // namespace tbtypea
