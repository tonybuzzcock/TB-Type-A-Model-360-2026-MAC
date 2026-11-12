// TypeA360.h
// A-type noise reduction unit, Model 360 frame with the Cat. No. 22 module
// (drawing ABC 641/3, 1971), plus the "Dolby A trick" band controls.
//
// Signal path (volts, 4x oversampled):
//
//   LINE IN -> input transformer (T2, A3D426; estimated) -> input level
//      (Dolby level, +4 dBu at the line input, becomes 300 mV at Output 1)
//   -> input amplifier Q500-Q503 with its 34 kHz low-pass:
//      R506 1K / C503 1n5, R507 6K65 / C504 220p (passive), then the
//      Sallen-Key Q502/Q503: R510 4K75, R511 4K22, C505 2n7, C506 390p
//      (34.6 kHz, Q 1.31)                                     = Output 1
//   -> filter driver (unity) -> the four band filters:
//      Band 1  Sallen-Key low-pass, Q180 FET follower:
//              R180 301K, R181 274K, C180 10n, C181 4n7 (80.8 Hz, Q 0.73)
//      Band 3  Sallen-Key high-pass, Q380: C380 = C381 = 6n8,
//              R380 11K, R382 5K62 (2.98 kHz, Q 0.70)
//      Band 4  Sallen-Key high-pass, Q480: C480 = C481 = 2n2,
//              R480 12K, R482 4K75 (9.58 kHz, Q 0.80)
//      Band 2  the wideband signal minus bands 1 and 3 (Q280-Q283)
//   -> four compressors, one per band: two shunt FET attenuators
//      (R406 10K / Q401, R408 47K / Q402; R208 10K in band 2), FET gain
//      stage Q403/Q404, overshoot limiter diodes D400/D401 behind R420,
//      emitter follower Q405 = band output. The control voltage comes from
//      the band's OUTPUT (feedback): phase splitter Q407/Q408, full-wave
//      rectifier D402/D403, integrator R436 10K / C410 100n into Q409
//      (bands 1 and 2: R136 20K, so twice the time constants), driving
//      the FET gates.
//   -> summing resistors R109 11K0, R209 11K3, R309 8K87, R409 9K53 into
//      the addition stage Q530/Q531 = the noise reduction signal
//      (THE BAND MOD: each summing resistor is an on/off switch and a pot,
//      off .. stock .. +18 dB, and STRESS turns the whole NR signal up to
//      +24 dB more: the "vocal stresser" blend)
//   Record:   Output 2 = Output 1 + NR signal   (encode)
//   Play:     NR signal fed back to the input:  Output 1 = in - NR(Output 1),
//             Output 2 = Output 1                (decode)
//   NR out:   NR signal earthed: a fixed-gain amplifier.
//   -> Output 2 amp (Q541/Q542), output level, line amplifier Q700-Q707
//      (clips at about +21 dBm into 600 ohms on the 18 V rail), output
//      transformer T3 (A3D435, 1:2; estimated) -> LINE OUT (+4 dBu at
//      Dolby level).
//   Dolby Tone: Q802/Q803 850 Hz multivibrator, Q805/Q806 filter (about 2%
//      THD), Q800/Q801 unijunction warble; injected in Record only (Q807
//      interlock), with the NR signal shorted (D530).
//
// Estimates (named constants in TypeA360.cpp, listed in the README): the
// compressors' gain law and threshold (fitted to the published A-type
// encode curve, since the gain-setting resistors are "adjusted on test"),
// their time constants beyond the integrator RCs, the limiter level, the
// transformers, the tone's warble timing.
//
// Plugin level: -18 dBFS sine in = Dolby level; Dolby level out = -18 dBFS.
// The input trim's gain is made back up after the line amplifier, so it
// changes how hard the circuit is driven, not the level out.
// Plain C++98, no allocation after construction.

#ifndef TBTYPEA_TYPEA360_H
#define TBTYPEA_TYPEA360_H

#include "Filters.h"
#include "Oversampler4x.h"

namespace tbtypea
{

enum { kNumBands = 4 };

// One band compressor of the Cat. 22 (feedback type)
struct BandCompressor
{
    double env;         // integrator (the FET gate drive), volts of the rectified output
    double gain;        // attenuator gain 0..1, from env
    double atk, rel, fast;      // per-sample coefficients

    BandCompressor() : env (0.0), gain (1.0), atk (0.0), rel (0.0), fast (0.0) {}
    void reset() { env = 0.0; gain = 1.0; }
};

class TypeA360
{
public:
    TypeA360();

    void prepare (double sampleRate);
    void reset();

    // Front panel
    void setDolbyTone (bool on);
    void setNrIn (bool in);
    void setRecord (bool record);              // true = REC (encode), false = PLAY (decode)
    void setBandOn (int band, bool on);        // band 0..3 = BAND 1..4
    void setBandLevel (int band, double v);    // 0..1 pot: off .. stock (at 0.5) .. +18 dB
    void setStressDb (double dB);              // more of the whole NR signal (0 = stock)
    // Plugin only
    void setInputGainDb (double dB);     // drive into the circuit; made back up at the output
    void setOutputGainDb (double dB);

    // Mono in, mono out
    void process (const float* in, float* out, int numSamples);

    static int latencySamples() { return Oversampler4x::latencySamples(); }

    // Introspection for the panel, the wrapper and the tests
    static double bandLevelGain (double pot);          // pot -> band signal against stock (1 at 0.5)
    static double bandLevelDb (double pot);            // the same in dB (-200 for off)
    double meterDb() const;                            // Output 1, dB re Dolby level (VU ballistics)
    double bandGainReduction (int band) const;         // 0..1 attenuator gain of a band compressor
    bool   recording() const { return record; }

private:
    double tickOs (double xLineVolts);
    double nrSignal (double u, double& d) const;       // NR(u) and dNR/du for the current states
    void   commitBands (double u);
    void   updateSmoothed();
    void   setupFilters();

    double fsBase, fsOs;
    Oversampler4x up, down;

    // Controls
    bool   toneOn, nrIn, record;
    bool   bandOn[kNumBands];
    double bandPot[kNumBands];
    double inT, outT, inG, outG;
    double stressGain;
    double bandW[kNumBands], bandWT[kNumBands];        // smoothed summing weights
    double nrW, nrWT, toneW, toneWT;
    double smCoef;

    // Transformers (estimated): high-pass, core flux, HF resonance
    FirstOrder t2Hp, t3Hp;
    double t2Flux, t3Flux, fluxCoef;
    Biquad t2Lp, t3Lp;

    // Input amplifier low-pass
    FirstOrder inPole;
    Biquad     inSk;

    // Band filters and compressors
    Biquad bandLp1, bandHp3, bandHp4;
    BandCompressor comp[kNumBands];
    double lastBand[kNumBands];      // band inputs this sample (for commit)
    double lastComp[kNumBands];      // limited compressor outputs this sample

    // Dolby Tone
    double tonePhase, toneWarble, toneWarbleInc;

    // Meter (average-responding, VU-like)
    double met1, met2, metCoef;
};

} // namespace tbtypea

#endif
